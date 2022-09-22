#ifndef THREADPOOL_H
#define THREADPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

int numCPUCores();
void poolInit(int nthreads);
void poolSubmit(void (*fn)(void*), void* arg);
void poolWait();

#ifdef __cplusplus
}
#endif

#endif
