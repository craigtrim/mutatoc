/* Bounded parallel construction for large vocabularies (craigtrim/axiom#129). */
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif
#include "mc.h"

typedef struct {
	void (*run)(void *);
	void (*after)(void *);
	void *data;
} ParallelJob;
#ifdef _WIN32
static unsigned __stdcall parallel_start(void *data)
#else
static void *parallel_start(void *data)
#endif
{
	ParallelJob *job = data;
	job->run(job->data);
	if (job->after)
		job->after(job->data);
	return 0;
}

void parallel_three(void (*first)(void *), void (*second)(void *),
		    void (*third)(void *), void *data, int enabled)
{
#ifdef _WIN32
	SYSTEM_INFO system;
	GetSystemInfo(&system);
	long processors = (long)system.dwNumberOfProcessors;
	uintptr_t threads[2] = { 0, 0 };
#else
	long processors = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_t threads[2];
#endif
	if (!enabled || processors < 2) {
		first(data);
		second(data);
		if (third)
			third(data);
		return;
	}
	/* Keep task ownership consistent across repeated loads. Two-core hosts
	 * combine the first and third jobs while the caller builds matching. */
	int workers = third && processors > 2 ? 2 : 1, started[2] = { 0, 0 };
	ParallelJob jobs[] = { { first, workers == 1 ? third : NULL, data },
			       { third, NULL, data } };
	for (int i = 0; i < workers; i++) {
#ifdef _WIN32
		threads[i] = _beginthreadex(NULL, 0, parallel_start, &jobs[i],
					    0, NULL);
		started[i] = threads[i] != 0;
#else
		started[i] = !pthread_create(&threads[i], NULL, parallel_start,
					     &jobs[i]);
#endif
	}
	second(data);
	for (int i = 0; i < workers; i++) {
		if (!started[i]) {
			parallel_start(&jobs[i]);
			continue;
		}
#ifdef _WIN32
		WaitForSingleObject((HANDLE)threads[i], INFINITE);
		CloseHandle((HANDLE)threads[i]);
#else
		pthread_join(threads[i], NULL);
#endif
	}
}
