#pragma once

#include <mutex>
#include <condition_variable>

class Semaphore
{
private:
  std::mutex mtx;
  std::condition_variable cond;
  unsigned long cnt = 0;

public:
  void post()
  {
    std::lock_guard<decltype(mtx)> lock(mtx);
    ++cnt;
    cond.notify_one();
  }

  void wait()
  {
    std::unique_lock<decltype(mtx)> lock(mtx);
    cond.wait(lock, [this](){ return cnt > 0; });
    --cnt;
  }

  // returns true if semaphore was signaled, false if timeout occurred
  bool waitForMsec(unsigned long ms)
  {
    std::unique_lock<decltype(mtx)> lock(mtx);
    bool res = cond.wait_for(lock, std::chrono::milliseconds(ms), [this](){ return cnt > 0; });
    if(res)
      --cnt;
    return res;
  }
};

// thread pool based on github.com/progschj/ThreadPool

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <future>
#include <functional>

class ThreadPool
{
public:
  ThreadPool(size_t nthreads);
  template<class F, class... Args>
  auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
  ~ThreadPool();

private:
  std::vector< std::thread > workers;
  std::queue< std::function<void()> > tasks;

  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t nthreads) : stop(false)
{
  if(nthreads == 0)
    nthreads = std::thread::hardware_concurrency();
  for(size_t ii = 0; ii < nthreads; ++ii)
    workers.emplace_back( [this](){
      for(;;) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(queue_mutex);
          condition.wait(lock, [this](){ return stop || !tasks.empty(); });
          if(stop && tasks.empty())
            return;
          task = std::move(tasks.front());
          tasks.pop();
        }
        task();
      }
    }
  );
}

// add new work item to the pool - returns a std::future, which has a wait() method
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
{
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared< std::packaged_task<return_type()> >(
    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
  );

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    if(!stop)
      tasks.emplace([task](){ (*task)(); });
  }
  condition.notify_one();
  return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  for(std::thread &worker: workers)
    worker.join();
}
