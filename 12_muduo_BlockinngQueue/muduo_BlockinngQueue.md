## muduo源码分析--muduo_BlockinngQueue 和 BoundedBlockingQueue ##

### 一、muduo_BlockinngQueue ###

究其本质，muduo_BlockinngQueue利用了条件变量来进行阻塞队列的操作，比较简单。

![](https://i.imgur.com/qlTl11g.png)

muduo_BlockinngQueue.h：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_BLOCKINGQUEUE_H
	#define MUDUO_BASE_BLOCKINGQUEUE_H
	
	#include <muduo/base/Condition.h>
	#include <muduo/base/Mutex.h>
	
	#include <boost/noncopyable.hpp>
	#include <deque>
	#include <assert.h>
	
	namespace muduo
	{
	
	template<typename T>
	class BlockingQueue : boost::noncopyable
	{
	 public:
	  BlockingQueue()
	    : mutex_(),
	      notEmpty_(mutex_),
	      queue_()
	  {
	  }
	
	  void put(const T& x)
	  {
	    MutexLockGuard lock(mutex_);
	    queue_.push_back(x);
	    notEmpty_.notify(); // TODO: move outside of lock
	  }
	
	  T take()
	  {
	    MutexLockGuard lock(mutex_);
	    // always use a while-loop, due to spurious wakeup
	    while (queue_.empty())
	    {
	      notEmpty_.wait();
	    }
	    assert(!queue_.empty());
	    T front(queue_.front());
	    queue_.pop_front();
	    return front;
	  }
	
	  size_t size() const
	  {
	    MutexLockGuard lock(mutex_);
	    return queue_.size();
	  }
	
	 private:
	  mutable MutexLock mutex_;
	  Condition         notEmpty_;
	  std::deque<T>     queue_;
	};
	
	}
	
	#endif  // MUDUO_BASE_BLOCKINGQUEUE_H

### 二、BoundedBlockingQueue ###

BoundedBlockingQueue与muduo_BlockinngQueue相比最大的区别在于该队列是有长度限制的。

BoundedBlockingQueue.h：

![](https://i.imgur.com/ENHM4Fx.png)


	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
	#define MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
	
	#include <muduo/base/Condition.h>
	#include <muduo/base/Mutex.h>
	
	#include <boost/circular_buffer.hpp>
	#include <boost/noncopyable.hpp>
	#include <assert.h>
	
	namespace muduo
	{
	
	template<typename T>
	class BoundedBlockingQueue : boost::noncopyable
	{
	 public:
	  explicit BoundedBlockingQueue(int maxSize)
	    : mutex_(),
	      notEmpty_(mutex_),
	      notFull_(mutex_),
	      queue_(maxSize)
	  {
	  }
	
	  void put(const T& x)
	  {
	    MutexLockGuard lock(mutex_);
	    while (queue_.full())
	    {
	      notFull_.wait();
	    }
	    assert(!queue_.full());
	    queue_.push_back(x);
	    notEmpty_.notify(); // TODO: move outside of lock
	  }
	
	  T take()
	  {
	    MutexLockGuard lock(mutex_);
	    while (queue_.empty())
	    {
	      notEmpty_.wait();
	    }
	    assert(!queue_.empty());
	    T front(queue_.front());
	    queue_.pop_front();
	    notFull_.notify(); // TODO: move outside of lock
	    return front;
	  }
	
	  bool empty() const
	  {
	    MutexLockGuard lock(mutex_);
	    return queue_.empty();
	  }
	
	  bool full() const
	  {
	    MutexLockGuard lock(mutex_);
	    return queue_.full();
	  }
	
	  size_t size() const
	  {
	    MutexLockGuard lock(mutex_);
	    return queue_.size();
	  }
	
	  size_t capacity() const
	  {
	    MutexLockGuard lock(mutex_);
	    return queue_.capacity();
	  }
	
	 private:
	  mutable MutexLock          mutex_;
	  Condition                  notEmpty_;
	  Condition                  notFull_;
	  boost::circular_buffer<T>  queue_;
	};
	
	}
	
	#endif  // MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
