#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#else
#include <windows.h>
#endif
#include "mc.h"
#include "lingpatlab.h"

#define WORKER_LIMIT (256u * 1024u * 1024u)
struct mc_spacy {
    char *python, *script, *model;
    unsigned timeout;
    J *info;
    const Graph *graph;
#ifdef _WIN32
    HANDLE process, input, output, job, worker_process;
#else
    pid_t pid;
    int fd;
#endif
};
static uint64_t millis(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
#endif
}
static void stop_worker(mc_spacy *w) {
#ifdef _WIN32
    if (w->input)
        CloseHandle(w->input);
    if (w->process) {
        if (WaitForSingleObject(w->process, 1000) == WAIT_TIMEOUT) {
            TerminateJobObject(w->job, 1);
            WaitForSingleObject(w->process, 1000);
        }
        CloseHandle(w->process);
    }
    if (w->output)
        CloseHandle(w->output);
    if (w->job) {
        /* Termination is asynchronous, including a venv launcher's child.
           Keep the job open until every associated process has exited. */
        TerminateJobObject(w->job, 1);
        uint64_t deadline = millis() + 1000;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
        while (QueryInformationJobObject(w->job, JobObjectBasicAccountingInformation, &accounting,
                                         sizeof(accounting), NULL) &&
               accounting.ActiveProcesses) {
            if (millis() >= deadline)
                break;
            Sleep(2);
        }
        CloseHandle(w->job);
    }
    if (w->worker_process) {
        WaitForSingleObject(w->worker_process, 1000);
        CloseHandle(w->worker_process);
    }
    w->input = w->output = w->process = w->job = w->worker_process = NULL;
#else
    if (w->fd >= 0)
        close(w->fd);
    w->fd = -1;
    if (w->pid > 0) {
        int done = 0;
        for (int i = 0; i < 100; i++) {
            pid_t r = waitpid(w->pid, NULL, WNOHANG);
            if (r == w->pid || (r < 0 && errno == ECHILD)) {
                done = 1;
                break;
            }
            struct timespec t = {0, 10000000};
            nanosleep(&t, NULL);
        }
        if (!done) {
            kill(w->pid, SIGKILL);
            while (waitpid(w->pid, NULL, 0) < 0 && errno == EINTR) {
            }
        }
    }
    w->pid = 0;
#endif
    DEL(w->info);
    w->info = NULL;
    w->graph = NULL;
}
void spacy_free(mc_spacy *w) {
    if (!w)
        return;
    stop_worker(w);
    free(w->python);
    free(w->script);
    free(w->model);
    free(w);
}
static int configure_worker(mc_spacy **slot, const char *python, const char *script,
                            const char *model, unsigned timeout_ms, mc_error *error) {
    mc_error local_error = {0};
    mc_error *err = error ? error : &local_error;
    memset(err, 0, sizeof(*err));
    if (!slot || !python || !*python || !script || !*script || !model || !*model ||
        timeout_ms > 3600000) {
        fail(err, 2,
             "spaCy requires engine, Python executable, worker script, model and timeout <= "
             "3600000 ms");
        return 0;
    }
    mc_spacy *w = calloc(1, sizeof(*w));
    if (!w) {
        fail(err, 1, "Out of memory");
        return 0;
    }
#ifndef _WIN32
    w->fd = -1;
#endif
    w->python = copy(python);
    w->script = copy(script);
    w->model = copy(model);
    w->timeout = timeout_ms ? timeout_ms : 120000;
    if (!w->python || !w->script || !w->model) {
        spacy_free(w);
        fail(err, 1, "Out of memory");
        return 0;
    }
    spacy_free(*slot);
    *slot = w;
    return 1;
}
int mc_use_spacy(mc_engine *e, const char *python, const char *script, const char *model,
                 unsigned timeout, mc_error *err) {
    return configure_worker(e ? &e->spacy : NULL, python, script, model, timeout, err);
}
int mc_use_sparql(mc_engine *e, const char *python, const char *script, unsigned timeout,
                  mc_error *err) {
    return configure_worker(e ? &e->sparql : NULL, python, script, "sparql", timeout, err);
}
void sparql_invalidate(mc_spacy *w) {
    if (w)
        w->graph = NULL;
}
#ifdef _WIN32
static wchar_t *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *out = n ? malloc((size_t)n * sizeof(*out)) : NULL;
    if (out)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, n);
    return out;
}
/* Windows argv quoting, including backslashes immediately before a quote. */
static void argument(Buf *b, const char *s) {
    if (b->n)
        buf_put(b, " ");
    buf_put(b, "\"");
    while (*s) {
        size_t slashes = 0;
        while (*s == '\\') {
            slashes++;
            s++;
        }
        size_t count = (*s == '"' || !*s) ? slashes * 2 : slashes;
        while (count--)
            buf_put(b, "\\");
        if (*s == '"')
            buf_put(b, "\\");
        if (*s)
            buf_add(b, s++, 1);
    }
    buf_put(b, "\"");
}
static int launch(mc_spacy *w, mc_error *err) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE child_in = NULL, child_out = NULL, child_err = NULL;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.StartupInfo.cb = sizeof(si);
    int ok = 0;
    wchar_t *command = NULL;
    if (!CreatePipe(&child_in, &w->input, &sa, 0) ||
        !SetHandleInformation(w->input, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&w->output, &child_out, &sa, 0) ||
        !SetHandleInformation(w->output, HANDLE_FLAG_INHERIT, 0))
        goto end;
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (!stderr_handle || stderr_handle == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(), stderr_handle, GetCurrentProcess(), &child_err, 0,
                         TRUE, DUPLICATE_SAME_ACCESS))
        child_err = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                OPEN_EXISTING, 0, NULL);
    if (!child_err || child_err == INVALID_HANDLE_VALUE)
        goto end;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    si.lpAttributeList = malloc(bytes);
    if (!si.lpAttributeList)
        goto end;
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes)) {
        free(si.lpAttributeList);
        si.lpAttributeList = NULL;
        goto end;
    }
    HANDLE inherited[] = {child_in, child_out, child_err};
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited, sizeof(inherited), NULL, NULL))
        goto end;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = child_in;
    si.StartupInfo.hStdOutput = child_out;
    si.StartupInfo.hStdError = child_err;
    Buf b = {0};
    argument(&b, w->python);
    argument(&b, "-u");
    argument(&b, w->script);
    argument(&b, "--model");
    argument(&b, w->model);
    command = wide(b.p);
    free(b.p);
    if (!command)
        goto end;
    w->job = CreateJobObjectW(NULL, NULL);
    if (!w->job)
        goto end;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(w->job, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        goto end;
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, NULL,
                        NULL, &si.StartupInfo, &pi))
        goto end;
    w->process = pi.hProcess;
    if (!AssignProcessToJobObject(w->job, w->process)) {
        TerminateProcess(w->process, 1);
        CloseHandle(pi.hThread);
        goto end;
    }
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        CloseHandle(pi.hThread);
        goto end;
    }
    CloseHandle(pi.hThread);
    ok = 1;
end:
    if (!ok)
        fail(err, 5, "Cannot start spaCy worker (Windows error %lu)", GetLastError());
    free(command);
    if (si.lpAttributeList) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
    }
    if (child_in)
        CloseHandle(child_in);
    if (child_out)
        CloseHandle(child_out);
    if (child_err && child_err != INVALID_HANDLE_VALUE)
        CloseHandle(child_err);
    if (!ok)
        stop_worker(w);
    return ok;
}
typedef struct {
    HANDLE pipe;
    const char *data;
    size_t n;
    DWORD error;
} WriteRequest;
static DWORD WINAPI writer(LPVOID arg) {
    WriteRequest *r = arg;
    while (r->n) {
        DWORD n = 0;
        DWORD chunk = (DWORD)(r->n > 65536 ? 65536 : r->n);
        if (!WriteFile(r->pipe, r->data, chunk, &n, NULL) || !n) {
            r->error = GetLastError();
            if (!r->error)
                r->error = ERROR_BROKEN_PIPE;
            return 1;
        }
        r->data += n;
        r->n -= n;
    }
    return 0;
}
#else
static int launch(mc_spacy *w, mc_error *err) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) {
        fail(err, 5, "Cannot create spaCy channel: %s", strerror(errno));
        return 0;
    }
    fcntl(pair[0], F_SETFD, FD_CLOEXEC);
    fcntl(pair[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (!pid) {
        close(pair[0]);
        if (dup2(pair[1], STDIN_FILENO) < 0 || dup2(pair[1], STDOUT_FILENO) < 0)
            _exit(126);
        if (pair[1] > STDOUT_FILENO)
            close(pair[1]);
        execlp(w->python, w->python, "-u", w->script, "--model", w->model, (char *)NULL);
        _exit(127);
    }
    close(pair[1]);
    if (pid < 0) {
        close(pair[0]);
        fail(err, 5, "Cannot start spaCy worker: %s", strerror(errno));
        return 0;
    }
    w->pid = pid;
    w->fd = pair[0];
    fcntl(w->fd, F_SETFL, fcntl(w->fd, F_GETFL) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(w->fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    return 1;
}
static int ready(mc_spacy *w, short events, uint64_t deadline, mc_error *err) {
    for (;;) {
        uint64_t now = millis();
        if (now >= deadline) {
            fail(err, 5, "spaCy worker timed out");
            return 0;
        }
        struct pollfd fd = {w->fd, events, 0};
        int r = poll(&fd, 1, (int)(deadline - now));
        if (r > 0)
            return 1;
        if (r < 0 && errno == EINTR)
            continue;
        fail(err, 5, r == 0 ? "spaCy worker timed out" : "spaCy channel poll failed");
        return 0;
    }
}
#endif
static int send_request(mc_spacy *w, const char *s, uint64_t deadline, mc_error *err) {
#ifdef _WIN32
    WriteRequest r = {w->input, s, strlen(s), 0};
    HANDLE thread = CreateThread(NULL, 0, writer, &r, 0, NULL);
    if (!thread) {
        fail(err, 5, "Cannot write to spaCy worker");
        return 0;
    }
    uint64_t now = millis();
    DWORD wait = WaitForSingleObject(thread, now < deadline ? (DWORD)(deadline - now) : 0);
    if (wait != WAIT_OBJECT_0) {
        TerminateJobObject(w->job, 1);
        CancelSynchronousIo(thread);
        WaitForSingleObject(thread, INFINITE);
        fail(err, 5, "spaCy worker write timed out");
    } else if (r.error)
        fail(err, 5, "spaCy worker disconnected while receiving request");
    CloseHandle(thread);
#else
    size_t n = strlen(s);
    while (n && !err->code) {
        if (!ready(w, POLLOUT, deadline, err))
            break;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t sent = send(w->fd, s, n, flags);
        if (sent > 0) {
            s += sent;
            n -= (size_t)sent;
        } else if (sent < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        else
            fail(err, 5, "spaCy worker disconnected while receiving request");
    }
#endif
    return !err->code;
}
static J *receive(mc_spacy *w, uint64_t deadline, mc_error *err) {
    Buf b = {0};
    int done = 0;
    while (!done && !err->code) {
        char block[16384];
        size_t n = 0;
#ifdef _WIN32
        if (millis() >= deadline) {
            fail(err, 5, "spaCy worker timed out");
            break;
        }
        DWORD available = 0, got = 0;
        if (!PeekNamedPipe(w->output, NULL, 0, NULL, &available, NULL)) {
            fail(err, 5, "spaCy worker exited without a response");
            break;
        }
        if (!available) {
            Sleep(2);
            continue;
        }
        if (!ReadFile(w->output, block, available < sizeof(block) ? available : sizeof(block), &got,
                      NULL) ||
            !got) {
            fail(err, 5, "Cannot read spaCy worker response");
            break;
        }
        n = got;
#else
        if (!ready(w, POLLIN, deadline, err))
            break;
        ssize_t got = recv(w->fd, block, sizeof(block), 0);
        if (got < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (got <= 0) {
            fail(err, 5, "spaCy worker exited without a response");
            break;
        }
        n = (size_t)got;
#endif
        if (b.n + n > WORKER_LIMIT) {
            fail(err, 5, "spaCy response exceeds 256 MiB");
            break;
        }
        char *newline = memchr(block, '\n', n);
        if (newline && newline != block + n - 1) {
            fail(err, 5, "Invalid spaCy response framing");
            break;
        }
        done = newline != NULL;
        buf_add(&b, block, n);
    }
    J *response = err->code ? NULL : json_parse(b.p ? b.p : "", err);
    free(b.p);
    J *result = NULL;
    if (response) {
        if (!cJSON_IsObject(response) || !cJSON_IsTrue(GET(response, "ok"))) {
            const char *message = S(GET(GET(response, "error"), "message"));
            fail(err, 5, "%s", *message ? message : "Invalid spaCy worker response");
        } else {
            result = cJSON_DetachItemFromObjectCaseSensitive(response, "result");
            if (!result)
                fail(err, 5, "spaCy worker response has no result");
        }
    }
    DEL(response);
    return result;
}
static mc_spacy *ensure_worker(mc_spacy *w, mc_error *err) {
    if (!w) {
        fail(err, 5,
             "Raw text requires spaCy: configure_spacy or mc_use_spacy must configure Python and "
             "the worker");
        return NULL;
    }
    if (w->info)
        return w;
    if (!launch(w, err))
        return NULL;
    w->info = receive(w, millis() + w->timeout, err);
    if (!err->code && (!cJSON_IsObject(w->info) || !cJSON_IsNumber(GET(w->info, "protocol")) ||
                       GET(w->info, "protocol")->valueint != 1))
        fail(err, 5, "Unsupported spaCy worker protocol");
    if (err->code) {
        stop_worker(w);
        return NULL;
    }
#ifdef _WIN32
    /* The venv launcher and Python interpreter have distinct process objects.
       Job accounting can reach zero before the interpreter handle is signaled. */
    J *pid = GET(w->info, "pid");
    if (cJSON_IsNumber(pid) && pid->valuedouble > 0 && pid->valuedouble <= 4294967295.0) {
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                     (DWORD)pid->valuedouble);
        int member = FALSE;
        if (process && IsProcessInJob(process, w->job, &member) && member)
            w->worker_process = process;
        else if (process)
            CloseHandle(process);
    }
#endif
    return w;
}
J *spacy_info(mc_engine *engine, mc_error *err) {
    mc_spacy *w = ensure_worker(engine->spacy, err);
    return w ? DUP(w->info) : NULL;
}
J *spacy_call(mc_engine *engine, const J *q, mc_error *err) {
    mc_spacy *w = ensure_worker(engine->spacy, err);
    if (!w)
        return NULL;
    char *wire = cJSON_PrintUnformatted(q);
    Buf b = {0};
    buf_put(&b, wire);
    buf_put(&b, "\n");
    free(wire);
    uint64_t deadline = millis() + w->timeout;
    J *result = NULL;
    if (send_request(w, b.p, deadline, err))
        result = receive(w, deadline, err);
    free(b.p);
    if (err->code) {
        DEL(result);
        stop_worker(w);
        return NULL;
    }
    return result;
}
J *spacy_tokens(mc_engine *engine, const char *text, mc_error *err) {
    /* Configuration is required even for empty input, matching the worker API. */
    if (!ensure_worker(engine->spacy, err))
        return NULL;
    J *tokens = lp_tokenize(text), *result = lp_parse_tokens(engine, tokens, err);
    DEL(tokens);
    return result;
}

J *sparql_query(mc_engine *engine, const J *request, mc_error *err) {
    if (!engine->sparql) {
        fail(err, 5, "Ad hoc SPARQL requires configure_sparql or mc_use_sparql");
        return NULL;
    }
    if (!engine->graph) {
        fail(err, 4, "Ad hoc SPARQL requires a loaded RDF graph");
        return NULL;
    }
    mc_spacy *w = ensure_worker(engine->sparql, err);
    if (!w)
        return NULL;
    J *q = DUP(request);
    set(q, "op", STR("sparql"));
    if (w->graph != engine->graph) {
        set(q, "graph", rdf_json(engine->graph));
        set(q, "prefixes", DUP(engine->graph->prefixes));
    }
    char *wire = cJSON_PrintUnformatted(q);
    DEL(q);
    Buf b = {0};
    buf_put(&b, wire);
    buf_put(&b, "\n");
    free(wire);
    uint64_t deadline = millis() + w->timeout;
    J *result = NULL;
    if (send_request(w, b.p, deadline, err))
        result = receive(w, deadline, err);
    free(b.p);
    if (err->code) {
        DEL(result);
        stop_worker(w);
        return NULL;
    }
    w->graph = engine->graph;
    J *out = sparql_transform(engine, request, result, err);
    DEL(result);
    return out;
}

/* Preserve RDFLib's date, duration, binary and XML coercion for these less common
   literal types. Graph parsing, indexing and Mutato extraction still run in C. */
int normalize_special_literals(mc_engine *engine, Graph *graph, mc_error *err) {
    J *values = ARR();
    for (size_t i = 0; i < graph->n; i++) {
        Term *t = &graph->ts[i].o;
        const char *dt = t->datatype;
        if (t->kind != 1 || !dt)
            continue;
        const char *types[] = {XSD "date",
                               XSD "dateTime",
                               XSD "time",
                               XSD "gYear",
                               XSD "gYearMonth",
                               XSD "duration",
                               XSD "dayTimeDuration",
                               XSD "yearMonthDuration",
                               XSD "base64Binary",
                               RDF "XMLLiteral"};
        int special = 0;
        for (size_t j = 0; j < sizeof(types) / sizeof(*types); j++)
            if (!strcmp(dt, types[j]))
                special = 1;
        if (!special)
            continue;
        J *value = OBJ();
        PUT(value, "kind", STR("literal"));
        PUT(value, "value", STR(t->value));
        PUT(value, "datatype", STR(dt));
        PUT(value, "index", NUM((double)i));
        ADD(values, value);
    }
    if (!SIZE(values)) {
        DEL(values);
        return 1;
    }
    if (!engine->sparql) {
        DEL(values);
        fail(err, 5,
             "Date, duration, base64 and XML literal compatibility requires configure_sparql or "
             "mc_use_sparql");
        return 0;
    }
    mc_spacy *w = ensure_worker(engine->sparql, err);
    if (!w) {
        DEL(values);
        return 0;
    }
    J *q = OBJ();
    PUT(q, "op", STR("normalize_literals"));
    PUT(q, "values", values);
    char *wire = cJSON_PrintUnformatted(q);
    Buf b = {0};
    buf_put(&b, wire);
    buf_put(&b, "\n");
    free(wire);
    uint64_t deadline = millis() + w->timeout;
    J *result = NULL;
    if (send_request(w, b.p, deadline, err))
        result = receive(w, deadline, err);
    free(b.p);
    if (!err->code && (!cJSON_IsArray(result) || SIZE(result) != SIZE(values)))
        fail(err, 5, "Invalid literal normalization response");
    if (!err->code) {
        int index = 0;
        EACH(value, values) {
            J *normalized = AT(result, index++);
            if (!cJSON_IsString(normalized)) {
                fail(err, 5, "Invalid normalized literal value");
                break;
            }
            Term *t = &graph->ts[(size_t)GET(value, "index")->valuedouble].o;
            free(t->value);
            t->value = copy(S(normalized));
        }
    }
    DEL(q);
    DEL(result);
    if (err->code)
        stop_worker(w);
    return err->code ? 0 : 2;
}
