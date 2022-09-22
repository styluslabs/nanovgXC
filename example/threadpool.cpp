#include "threadpool.h"
#include "threadutil.h"

ThreadPool* threadPool = NULL;
std::vector< std::future<void> > futures;


int numCPUCores()
{
  return std::thread::hardware_concurrency();
}

void poolInit(int nthreads)
{
  threadPool = new ThreadPool(nthreads);
}

void poolSubmit(void (*fn)(void*), void* arg)
{
  futures.push_back(threadPool->enqueue(fn, arg));  //[fn, arg](){ fn(arg); }));
}

void poolWait()
{
  for(auto& future : futures)
    future.wait();
  futures.clear();
}
