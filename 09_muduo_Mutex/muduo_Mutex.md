## muduo源码分析--mutex锁 ##

### 一、muduo源码 ###

![](https://i.imgur.com/SAZcivI.png)

Mutex.h：

	class MutexLock : boost::noncopyable
	{
	 public:
	  MutexLock()
	    : holder_(0)
	  {
	    int ret = pthread_mutex_init(&mutex_, NULL);
	    assert(ret == 0); (void) ret;
	  }
	
	  ~MutexLock()
	  {
	    assert(holder_ == 0);
	    int ret = pthread_mutex_destroy(&mutex_);
	    assert(ret == 0); (void) ret;
	  }
	
	  bool isLockedByThisThread()
	  {
	    return holder_ == CurrentThread::tid();
	  }
	
	  void assertLocked()
	  {
	    assert(isLockedByThisThread());
	  }
	
	  // internal usage
	
	  void lock()
	  {
	    pthread_mutex_lock(&mutex_);
	    holder_ = CurrentThread::tid();
	  }
	
	  void unlock()
	  {
	    holder_ = 0;
	    pthread_mutex_unlock(&mutex_);
	  }
	
	  pthread_mutex_t* getPthreadMutex() /* non-const */
	  {
	    return &mutex_;
	  }
	
	 private:
	
	  pthread_mutex_t mutex_;
	  pid_t holder_;
	};

MutexLock类只是简单地对mutex进行封装而已。

MutexLockGuard类：

![](https://i.imgur.com/LiVeMh0.png)

	class MutexLockGuard : boost::noncopyable
	{
	 public:
	  explicit MutexLockGuard(MutexLock& mutex)
	    : mutex_(mutex)
	  {
	    mutex_.lock();
	  }
	
	  ~MutexLockGuard()
	  {
	    mutex_.unlock();
	  }
	
	 private:
	
	  MutexLock& mutex_;
	};

MutexLockGuard类和MutexLock是关联关系，间接地操作索对象，但这里使用了RAII技法。

RAII是C++的发明者Bjarne Stroustrup提出的概念，RAII全称是“Resource Acquisition is Initialization”，直译过来是“资源获取即初始化”，也就是说在构造函数中申请分配资源，在析构函数中释放资源。因为C++的语言机制保证了，当一个对象创建的时候，自动调用构造函数，当对象超出作用域的时候会自动调用析构函数。所以，在RAII的指导下，我们应该使用类来管理资源，将资源和对象的生命周期绑定。

智能指针（std::shared_ptr和std::unique_ptr）即RAII最具代表的实现，使用智能指针，可以实现自动的内存管理，再也不需要担心忘记delete造成的内存泄漏。毫不夸张的来讲，有了智能指针，代码中几乎不需要再出现delete了。

这里在MutexLockGuard对象创建时将锁锁上，在该对象析构时自动将所持有的锁对象解锁，从而避免了单独使用MutexLock对象忘记解锁的情况。

### 二、测试代码 ###

	#include <muduo/base/CountDownLatch.h>
	#include <muduo/base/Mutex.h>
	#include <muduo/base/Thread.h>
	#include <muduo/base/Timestamp.h>
	
	#include <boost/bind.hpp>
	#include <boost/ptr_container/ptr_vector.hpp>
	#include <vector>
	#include <stdio.h>
	
	using namespace muduo;
	using namespace std;
	
	MutexLock g_mutex;
	vector<int> g_vec;
	const int kCount = 10*1000*1000;
	
	void threadFunc()
	{
	  for (int i = 0; i < kCount; ++i)
	  {
	    MutexLockGuard lock(g_mutex);
	    g_vec.push_back(i);
	  }
	}
	
	int main()
	{
	  const int kMaxThreads = 8;
	  g_vec.reserve(kMaxThreads * kCount);
	
	  Timestamp start(Timestamp::now());
	  for (int i = 0; i < kCount; ++i)
	  {
	    g_vec.push_back(i);
	  }
	
	  printf("single thread without lock %f\n", timeDifference(Timestamp::now(), start));
	
	  start = Timestamp::now();
	  threadFunc();
	  printf("single thread with lock %f\n", timeDifference(Timestamp::now(), start));
	
	  for (int nthreads = 1; nthreads < kMaxThreads; ++nthreads)
	  {
	    boost::ptr_vector<Thread> threads;
	    g_vec.clear();
	    start = Timestamp::now();
	    for (int i = 0; i < nthreads; ++i)
	    {
	      threads.push_back(new Thread(&threadFunc));
	      threads.back().start();
	    }
	    for (int i = 0; i < nthreads; ++i)
	    {
	      threads[i].join();
	    }
	    printf("%d thread(s) with lock %f\n", nthreads, timeDifference(Timestamp::now(), start));
	  }
	}

测试结果如下：

![](https://i.imgur.com/JdpdMjl.png)

该程序主要就是显示在线程中加锁和不加锁的消耗情况，一开始不加锁耗时只有0.2s，加锁后同样的代码消耗达到了0.6s，然后几乎每一个线程加一个锁锁时间消耗加倍。
