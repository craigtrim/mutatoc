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
	return 0;
}

void parallel_pair(void (*first)(void *), void *first_data,
		   void (*second)(void *), void *second_data, int enabled)
{
	if (!enabled) {
		first(first_data);
		second(second_data);
		return;
	}
	ParallelJob job = { first, first_data };
#ifdef _WIN32
	SYSTEM_INFO system;
	GetSystemInfo(&system);
	uintptr_t thread =
		system.dwNumberOfProcessors > 1 ?
			      _beginthreadex(NULL, 0, parallel_start, &job, 0, NULL) :
			      0;
	if (thread) {
		second(second_data);
		WaitForSingleObject((HANDLE)thread, INFINITE);
		CloseHandle((HANDLE)thread);
		return;
	}
#else
	pthread_t thread;
	if (sysconf(_SC_NPROCESSORS_ONLN) > 1 &&
	    !pthread_create(&thread, NULL, parallel_start, &job)) {
		second(second_data);
		pthread_join(thread, NULL);
		return;
	}
#endif
	first(first_data);
	second(second_data);
}
