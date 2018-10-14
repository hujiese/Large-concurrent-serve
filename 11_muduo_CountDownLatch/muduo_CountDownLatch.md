## muduo源码分析--muduo_CountDownLatch ##

### 一、源码 ###

![](https://i.imgur.com/FnwokAt.png)

CountDownLatch.h:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
	#define MUDUO_BASE_COUNTDOWNLATCH_H
	
	#include <muduo/base/Condition.h>
	#include <muduo/base/Mutex.h>
	
	#include <boost/noncopyable.hpp>
	
	namespace muduo
	{
	
	class CountDownLatch : boost::noncopyable
	{
	 public:
	
	  explicit CountDownLatch(int count);
	
	  void wait();
	
	  void countDown();
	
	  int getCount() const;
	
	 private:
	  mutable MutexLock mutex_;
	  Condition condition_;
	  int count_;
	};
	
	}
	#endif  // MUDUO_BASE_COUNTDOWNLATCH_H

CountDownLatch.cc:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#include <muduo/base/CountDownLatch.h>
	
	using namespace muduo;
	
	CountDownLatch::CountDownLatch(int count)
	  : mutex_(),
	    condition_(mutex_),
	    count_(count)
	{
	}
	
	void CountDownLatch::wait()
	{
	  MutexLockGuard lock(mutex_);
	  while (count_ > 0) {
	    condition_.wait();
	  }
	}
	
	void CountDownLatch::countDown()
	{
	  MutexLockGuard lock(mutex_);
	  --count_;
	  if (count_ == 0) {
	    condition_.notifyAll();
	  }
	}
	
	int CountDownLatch::getCount() const
	{
	  MutexLockGuard lock(mutex_);
	  return count_;
	}

从源码可以看出，该类结合和互斥锁和条件变量。
