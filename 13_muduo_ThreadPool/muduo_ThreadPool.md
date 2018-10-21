## muduo源码分析--ThreadPool线程池 ##

### 一、源码 ###

![](https://i.imgur.com/jeH21Qk.png)

ThreadPool.h：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_THREADPOOL_H
	#define MUDUO_BASE_THREADPOOL_H
	
	#include <muduo/base/Condition.h>
	#include <muduo/base/Mutex.h>
	#include <muduo/base/Thread.h>
	#include <muduo/base/Types.h>
	
	#include <boost/function.hpp>
	#include <boost/noncopyable.hpp>
	#include <boost/ptr_container/ptr_vector.hpp>
	
	#include <deque>
	
	namespace muduo
	{
	
	class ThreadPool : boost::noncopyable
	{
	 public:
	  typedef boost::function<void ()> Task;
	
	  explicit ThreadPool(const string& name = string());
	  ~ThreadPool();
	
	  void start(int numThreads);
	  void stop();
	
	  void run(const Task& f);
	
	 private:
	  void runInThread();
	  Task take();
	
	  MutexLock mutex_;
	  Condition cond_;
	  string name_;
	  boost::ptr_vector<muduo::Thread> threads_;
	  std::deque<Task> queue_;
	  bool running_;
	};
	
	}
	
	#endif

ThreadPool.cc:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#include <muduo/base/ThreadPool.h>
	
	#include <muduo/base/Exception.h>
	
	#include <boost/bind.hpp>
	#include <assert.h>
	#include <stdio.h>
	
	using namespace muduo;
	
	ThreadPool::ThreadPool(const string& name)
	  : mutex_(),
	    cond_(mutex_),
	    name_(name),
	    running_(false)
	{
	}
	
	ThreadPool::~ThreadPool()
	{
	  if (running_)
	  {
	    stop();
	  }
	}
	
	void ThreadPool::start(int numThreads)
	{
	  assert(threads_.empty());
	  running_ = true;
	  threads_.reserve(numThreads);
	  for (int i = 0; i < numThreads; ++i)
	  {
	    char id[32];
	    snprintf(id, sizeof id, "%d", i);
	    threads_.push_back(new muduo::Thread(
	          boost::bind(&ThreadPool::runInThread, this), name_+id));
	    threads_[i].start();
	  }
	}
	
	void ThreadPool::stop()
	{
	  {
	  MutexLockGuard lock(mutex_);
	  running_ = false;
	  cond_.notifyAll();
	  }
	  for_each(threads_.begin(),
	           threads_.end(),
	           boost::bind(&muduo::Thread::join, _1));
	}
	
	void ThreadPool::run(const Task& task)
	{
	  if (threads_.empty())
	  {
	    task();
	  }
	  else
	  {
	    MutexLockGuard lock(mutex_);
	    queue_.push_back(task);
	    cond_.notify();
	  }
	}
	
	ThreadPool::Task ThreadPool::take()
	{
	  MutexLockGuard lock(mutex_);
	  // always use a while-loop, due to spurious wakeup
	  while (queue_.empty() && running_)
	  {
	    cond_.wait();
	  }
	  Task task;
	  if(!queue_.empty())
	  {
	    task = queue_.front();
	    queue_.pop_front();
	  }
	  return task;
	}
	
	void ThreadPool::runInThread()
	{
	  try
	  {
	    while (running_)
	    {
	      Task task(take());
	      if (task)
	      {
	        task();
	      }
	    }
	  }
	  catch (const Exception& ex)
	  {
	    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
	    abort();
	  }
	  catch (const std::exception& ex)
	  {
	    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    abort();
	  }
	  catch (...)
	  {
	    fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
	    throw; // rethrow
	  }
	}

### 二、从一个测试程序分析源码  ###

ThreadPool_test.cc:

	#include <muduo/base/ThreadPool.h>
	#include <muduo/base/CountDownLatch.h>
	#include <muduo/base/CurrentThread.h>
	
	#include <boost/bind.hpp>
	#include <stdio.h>
	
	void print()
	{
	  printf("tid=%d\n", muduo::CurrentThread::tid());
	}
	
	void printString(const std::string& str)
	{
	  printf("tid=%d, str=%s\n", muduo::CurrentThread::tid(), str.c_str());
	}
	
	int main()
	{
	  muduo::ThreadPool pool("MainThreadPool");
	  pool.start(5);
	
	  pool.run(print);
	  pool.run(print);
	  for (int i = 0; i < 10; ++i)
	  {
	    char buf[32];
	    snprintf(buf, sizeof buf, "task %d", i);
	    pool.run(boost::bind(printString, std::string(buf)));
	  }
	
	  muduo::CountDownLatch latch(1);
	  pool.run(boost::bind(&muduo::CountDownLatch::countDown, &latch));
	  latch.wait();
	  pool.stop();
	}

编译运行结果如下：

![](https://i.imgur.com/7kE4nQF.png)

该测试程序定义了两个函数print和printString，print之打印主线程的tid，printString除了打印主线程tid外还打印了传入的字符串参数。

首先程序创建线程池：

	muduo::ThreadPool pool("MainThreadPool");

这一步将调用线程池类的构造函数：

	explicit ThreadPool(const string& name = string());
	
	ThreadPool::ThreadPool(const string& name)
	  : mutex_(),
	    cond_(mutex_),
	    name_(name),
	    running_(false)
	{
	}

初始化了互斥锁和条件变量，线程池名和运行标记。

接下来执行：

	pool.start(5);

这一步调用了线程池的start函数：

	void start(int numThreads);
	
	void ThreadPool::start(int numThreads)
	{
	  assert(threads_.empty());
	  running_ = true;
	  threads_.reserve(numThreads);
	  for (int i = 0; i < numThreads; ++i)
	  {
	    char id[32];
	    snprintf(id, sizeof id, "%d", i);
	    threads_.push_back(new muduo::Thread(
	          boost::bind(&ThreadPool::runInThread, this), name_+id));
	    threads_[i].start();
	  }
	}

该函数传入参数为线程池创建线程的数量，也就是说该线程池中的线程在线程池创建后就确定了，无法修改。在函数内部首先判断线程数组是否为空，线程数组如下：

	boost::ptr_vector<muduo::Thread> threads_;

该数组使用了boost的向量。

紧接着设置running_为true，也就是说该线程池在运行中，接下来的循环创建numThreads个线程：

	for (int i = 0; i < numThreads; ++i)
	{
		char id[32];
		snprintf(id, sizeof id, "%d", i);
		threads_.push_back(new muduo::Thread(
		      boost::bind(&ThreadPool::runInThread, this), name_+id));
		threads_[i].start();
	}

该部分将创建的线程保存到线程数组中，然后对每一个线程数组中的成员调用start函数启动线程。需要注意的是每个线程绑定的函数：ThreadPool::runInThread，该函数定义如下：

	void ThreadPool::runInThread()
	{
	  try
	  {
	    while (running_)
	    {
	      Task task(take());
	      if (task)
	      {
	        task();
	      }
	    }
	  }
	  catch (const Exception& ex)
	  {
	    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
	    abort();
	  }
	  catch (const std::exception& ex)
	  {
	    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    abort();
	  }
	  catch (...)
	  {
	    fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
	    throw; // rethrow
	  }
	}

由于上一步已经将running_设置为true了，所以执行
	
	Task task(take());

Task定义如下：

	typedef boost::function<void ()> Task;

take函数定义如下：

	ThreadPool::Task ThreadPool::take()
	{
	  MutexLockGuard lock(mutex_);
	  // always use a while-loop, due to spurious wakeup
	  while (queue_.empty() && running_)
	  {
	    cond_.wait();
	  }
	  Task task;
	  if(!queue_.empty())
	  {
	    task = queue_.front();
	    queue_.pop_front();
	  }
	  return task;
	}

该函数使用了互斥锁，一进入该函数就锁住，然后判断任务队列中是否有执行的任务以及是否线程池在运行，如果不满足条件，则通过条件变量等待，很明显，这里一开始是没有任务的，任务队列为空；当然，如果条件满足，如果任务队列不为空就将任务队列的第一个任务取出返回。

现在回到void ThreadPool::runInThread()函数，执行：

	if (task)
	{
		task();
	}

如果上一步取出的任务不为空，则执行该任务。

回到测试程序，执行：

	pool.run(print);

这一步中run函数如下：

	void run(const Task& f);
	
	void ThreadPool::run(const Task& task)
	{
	  if (threads_.empty())
	  {
	    task();
	  }
	  else
	  {
	    MutexLockGuard lock(mutex_);
	    queue_.push_back(task);
	    cond_.notify();
	  }
	}

run函数接受一个Task任务作为参数传入，然后判断线程数组是否为空，如果为空，那么久直接执行该任务，否则将该任务送入任务队列中，然后唤醒等待函数ThreadPool::take()。上面分析到ThreadPool::take函数如果没有任务执行将一直处于等待状态。

所以总结就是：void run(const Task& f)类似于“生产者”，生产线程，而ThreadPool::Task ThreadPool::take()则是“消费者”的角色，负责消费（取出）线程。

回到测试程序，其中：

	pool.run(print);
	for (int i = 0; i < 100; ++i)
	{
		char buf[32];
		snprintf(buf, sizeof buf, "task %d", i);
		pool.run(boost::bind(printString, std::string(buf)));
	}
	
	muduo::CountDownLatch latch(1);
	pool.run(boost::bind(&muduo::CountDownLatch::countDown, &latch));
	latch.wait();

只是方便测试演示，运作过程和上面分析一样，下面看到：

	pool.stop();

该函数定义如下：

	void ThreadPool::stop()
	{
	  {
	  MutexLockGuard lock(mutex_);
	  running_ = false;
	  cond_.notifyAll();
	  }
	  for_each(threads_.begin(),
	           threads_.end(),
	           boost::bind(&muduo::Thread::join, _1));
	}

首先该函数将运行标志设置为false然后唤醒等待函数ThreadPool::take()，该函数将返回一个空值，也就是说不会再有新的任务加入任务队列了，然后对所有线程调用join函数，让主线程等待所有线程执行完后再退出。程序执行到此结束了。