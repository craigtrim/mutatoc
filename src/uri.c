#ifdef _WIN32
#include <windows.h>
#else
#define _XOPEN_SOURCE 700
#include <unistd.h>
#endif
#include "mc.h"
typedef struct {
    char *storage, *scheme, *authority, *path, *query, *fragment;
} Uri;
static Uri parts(const char *s) {
    Uri u = {0};
    u.storage = copy(s);
    char *p = u.storage, *q;
    if ((q = strchr(p, '#')) != NULL) {
        *q = 0;
        u.fragment = q + 1;
    }
    if ((q = strchr(p, '?')) != NULL) {
        *q = 0;
        u.query = q + 1;
    }
    q = p;
    if (isalpha((unsigned char)*q)) {
        for (++q; isalnum((unsigned char)*q) || *q == '+' || *q == '-' || *q == '.'; ++q) {
        }
        if (*q == ':') {
            *q = 0;
            u.scheme = p;
            p = q + 1;
        }
    }
    if (p[0] == '/' && p[1] == '/') {
        u.authority = p + 2;
        q = strchr(p + 2, '/');
        if (q) { /* Keep the path's first slash in a separate owned string. */
            u.path = copy(q);
            *q = 0;
        } else
            u.path = copy("");
    } else
        u.path = copy(p);
    return u;
}
static void release(Uri *u) {
    free(u->storage);
    free(u->path);
}
static void pop_segment(Buf *b) {
    while (b->n && b->p[b->n - 1] != '/')
        --b->n;
    if (b->n)
        --b->n;
    if (b->p)
        b->p[b->n] = 0;
}
static char *remove_dots(const char *p) {
    Buf b = {0};
    while (*p) {
        if (!strncmp(p, "../", 3))
            p += 3;
        else if (!strncmp(p, "./", 2))
            p += 2;
        else if (!strncmp(p, "/./", 3))
            p += 2;
        else if (!strcmp(p, "/.")) {
            p += 2;
            buf_put(&b, "/");
        } else if (!strncmp(p, "/../", 4)) {
            p += 3;
            pop_segment(&b);
        } else if (!strcmp(p, "/..")) {
            p += 3;
            pop_segment(&b);
            buf_put(&b, "/");
        } else if (!strcmp(p, ".") || !strcmp(p, ".."))
            break;
        else {
            const char *q = p;
            if (*q == '/')
                ++q;
            while (*q && *q != '/')
                ++q;
            buf_add(&b, p, (size_t)(q - p));
            p = q;
        }
    }
    return buf_take(&b);
}
char *uri_resolve(const char *base, const char *reference) {
    Uri b = parts(base ? base : ""), r = parts(reference);
    const char *scheme = r.scheme ? r.scheme : b.scheme, *authority, *query;
    char *path;
    if (r.scheme) {
        authority = r.authority;
        path = remove_dots(r.path);
        query = r.query;
    } else if (r.authority) {
        authority = r.authority;
        path = remove_dots(r.path);
        query = r.query;
    } else {
        authority = b.authority;
        if (!*r.path) {
            path = copy(b.path);
            query = r.query ? r.query : b.query;
        } else {
            query = r.query;
            if (*r.path == '/')
                path = remove_dots(r.path);
            else {
                Buf merged = {0};
                const char *last = strrchr(b.path, '/');
                if (b.authority && !*b.path)
                    buf_put(&merged, "/");
                else if (last)
                    buf_add(&merged, b.path, (size_t)(last - b.path + 1));
                buf_put(&merged, r.path);
                path = remove_dots(merged.p);
                free(merged.p);
            }
        }
    }
    Buf out = {0};
    if (scheme) {
        buf_put(&out, scheme);
        buf_put(&out, ":");
    }
    if (authority) {
        buf_put(&out, "//");
        buf_put(&out, authority);
    }
    buf_put(&out, path);
    if (query) {
        buf_put(&out, "?");
        buf_put(&out, query);
    }
    if (r.fragment) {
        buf_put(&out, "#");
        buf_put(&out, r.fragment);
    }
    free(path);
    release(&b);
    release(&r);
    return buf_take(&out);
}
char *file_uri(const char *path) {
    char *absolute = NULL;
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!n)
        return copy("");
    wchar_t *w = malloc((size_t)n * sizeof(*w));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, n);
    DWORD size = GetFullPathNameW(w, 0, NULL, NULL);
    wchar_t *full = malloc(((size_t)size + 1) * sizeof(*full));
    if (size && GetFullPathNameW(w, size + 1, full, NULL)) {
        n = WideCharToMultiByte(CP_UTF8, 0, full, -1, NULL, 0, NULL, NULL);
        absolute = malloc((size_t)n);
        WideCharToMultiByte(CP_UTF8, 0, full, -1, absolute, n, NULL, NULL);
    }
    free(w);
    free(full);
#else
    absolute = realpath(path, NULL);
#endif
    if (!absolute)
        return copy("");
    Buf b = {0};
    buf_put(&b, "file://");
#ifdef _WIN32
    for (char *p = absolute; *p; p++)
        if (*p == '\\')
            *p = '/';
    if (absolute[0] == '/' && absolute[1] == '/')
        memmove(absolute, absolute + 2, strlen(absolute + 2) + 1);
    else
        buf_put(&b, "/");
#endif
    for (const unsigned char *p = (const unsigned char *)absolute; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            strchr("-._~/:", *p)) {
            char c = (char)*p;
            buf_add(&b, &c, 1);
        } else {
            char escape[4];
            snprintf(escape, sizeof(escape), "%%%02X", *p);
            buf_put(&b, escape);
        }
    }
    free(absolute);
    return buf_take(&b);
}
