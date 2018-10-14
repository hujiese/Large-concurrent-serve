## muduo源码分析--Condition条件变量 ##

### 一、condition条件变量回顾 ###

![](https://i.imgur.com/I7rEivJ.png)

条件变量比较关键的地方就是while循环中的等待，如果条件不满足，那么条件变量将解锁，然后在while循环中一直等待条件变量满足，于此同时，其他线程通过某种方式改变了条件，然后发出信号通知等待线程条件满足，然后等待线程加锁推出while循环。

### 二、源码 ###

![](https://i.imgur.com/b6ECPIO.png)

Condition.h:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_CONDITION_H
	#define MUDUO_BASE_CONDITION_H
	
	#include <muduo/base/Mutex.h>
	
	#include <boost/noncopyable.hpp>
	#include <pthread.h>
	
	namespace muduo
	{
	
	class Condition : boost::noncopyable
	{
	 public:
	  explicit Condition(MutexLock& mutex)
	    : mutex_(mutex)
	  {
	    pthread_cond_init(&pcond_, NULL);
	  }
	
	  ~Condition()
	  {
	    pthread_cond_destroy(&pcond_);
	  }
	
	  void wait()
	  {
	    pthread_cond_wait(&pcond_, mutex_.getPthreadMutex());
	  }
	
	  // returns true if time out, false otherwise.
	  bool waitForSeconds(int seconds);
	
	  void notify()
	  {
	    pthread_cond_signal(&pcond_);
	  }
	
	  void notifyAll()
	  {
	    pthread_cond_broadcast(&pcond_);
	  }
	
	 private:
	  MutexLock& mutex_;
	  pthread_cond_t pcond_;
	};
	
	}
	#endif  // MUDUO_BASE_CONDITION_H

Condition.cc：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#include <muduo/base/Condition.h>
	
	#include <errno.h>
	
	// returns true if time out, false otherwise.
	bool muduo::Condition::waitForSeconds(int seconds)
	{
	  struct timespec abstime;
	  clock_gettime(CLOCK_REALTIME, &abstime);
	  abstime.tv_sec += seconds;
	  return ETIMEDOUT == pthread_cond_timedwait(&pcond_, mutex_.getPthreadMutex(), &abstime);
	}

可见，muduo中的条件变量也就是对UNIX中的条件变量封装一层而已。