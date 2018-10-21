## muduo源码分析之 Singleton单例对象 ##

![](https://i.imgur.com/kzC4jNg.png)

Singleton.h：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_SINGLETON_H
	#define MUDUO_BASE_SINGLETON_H
	
	#include <boost/noncopyable.hpp>
	#include <pthread.h>
	#include <stdlib.h> // atexit
	
	namespace muduo
	{
	
	template<typename T>
	class Singleton : boost::noncopyable
	{
	 public:
	  static T& instance()
	  {
	    pthread_once(&ponce_, &Singleton::init);
	    return *value_;
	  }
	
	 private:
	  Singleton();
	  ~Singleton();
	
	  static void init()
	  {
	    value_ = new T();
	    ::atexit(destroy);
	  }
	
	  static void destroy()
	  {
	    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
	    delete value_;
	  }
	
	 private:
	  static pthread_once_t ponce_;
	  static T*             value_;
	};
	
	template<typename T>
	pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;
	
	template<typename T>
	T* Singleton<T>::value_ = NULL;
	
	}
	#endif

从上面可见，整个单例类都使用了static关键字，至于如何实现单例，可以通过下面这个例子来分析。

Singleton_test.cc：

	#include <muduo/base/Singleton.h>
	#include <muduo/base/CurrentThread.h>
	#include <muduo/base/Thread.h>
	
	#include <boost/noncopyable.hpp>
	#include <stdio.h>
	
	class Test : boost::noncopyable
	{
	 public:
	  Test()
	  {
	    printf("tid=%d, constructing %p\n", muduo::CurrentThread::tid(), this);
	  }
	
	  ~Test()
	  {
	    printf("tid=%d, destructing %p %s\n", muduo::CurrentThread::tid(), this, name_.c_str());
	  }
	
	  const muduo::string& name() const { return name_; }
	  void setName(const muduo::string& n) { name_ = n; }
	
	 private:
	  muduo::string name_;
	};
	
	void threadFunc()
	{
	  printf("tid=%d, %p name=%s\n",
	         muduo::CurrentThread::tid(),
	         &muduo::Singleton<Test>::instance(),
	         muduo::Singleton<Test>::instance().name().c_str());
	  muduo::Singleton<Test>::instance().setName("only one, changed");
	}
	
	int main()
	{
	  muduo::Singleton<Test>::instance().setName("only one");
	  muduo::Thread t1(threadFunc);
	  t1.start();
	  t1.join();
	  printf("tid=%d, %p name=%s\n",
	         muduo::CurrentThread::tid(),
	         &muduo::Singleton<Test>::instance(),
	         muduo::Singleton<Test>::instance().name().c_str());
	}

编译运行结果如下：

![](https://i.imgur.com/AHaH6Zi.png)

首先测试程序定义了一个Test类，该类如下：

	class Test : boost::noncopyable
	{
	 public:
	  Test()
	  {
	    printf("tid=%d, constructing %p\n", muduo::CurrentThread::tid(), this);
	  }
	
	  ~Test()
	  {
	    printf("tid=%d, destructing %p %s\n", muduo::CurrentThread::tid(), this, name_.c_str());
	  }
	
	  const muduo::string& name() const { return name_; }
	  void setName(const muduo::string& n) { name_ = n; }
	
	 private:
	  muduo::string name_;
	};

其中构造函数打印运行线程的tid和该类地址，析构函数也打印运行线程的tid和通过setName函数设置的name_。name函数获取name_的值。

函数：

	void threadFunc()
	{
	  printf("tid=%d, %p name=%s\n",
	         muduo::CurrentThread::tid(),
	         &muduo::Singleton<Test>::instance(),
	         muduo::Singleton<Test>::instance().name().c_str());
	  muduo::Singleton<Test>::instance().setName("only one, changed");
	}

也是做一些打印操作。

然后在主函数中创建了一个Test单例：

	muduo::Singleton<Test>::instance().setName("only one");

追踪到instance函数：

	static T& instance()
	{
		pthread_once(&ponce_, &Singleton::init);
		return *value_;
	}

pthread_once中两个对象是：

	private:
		static pthread_once_t ponce_;
		static T*             value_;

它们都是静态的，然后在类的外部初始化：

	template<typename T>
	pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;
	
	template<typename T>
	T* Singleton<T>::value_ = NULL;

其中ponce_是pthread_once_t类型的，这个类型将在后面说明。至于value_，则是需要单例创建的实体化对象，设置初值为NULL。

回到函数instance()，在该函数中调用了pthread_once函数，这个函数的原型如下：

	int pthread_once(pthread_once_t *once_control, void (*init_routine) (void))

函数使用初值为PTHREAD_ONCE_INIT的once_control变量保证init_routine()函数在本进程执行序列中仅执行一次。

LinuxThreads使用互斥锁和条件变量保证由pthread_once()指定的函数执行且仅执行一次，而once_control则表征是否执行过。如果once_control的初值不是PTHREAD_ONCE_INIT（LinuxThreads定义为0），pthread_once() 的行为就会不正常。在LinuxThreads中，实际"一次性函数"的执行状态有三种：NEVER（0）、IN_PROGRESS（1）、DONE （2），如果once初值设为1，则由于所有pthread_once()都必须等待其中一个激发"已执行一次"信号，因此所有pthread_once ()都会陷入永久的等待中；如果设为2，则表示该函数已执行过一次，从而所有pthread_once()都会立即返回0。

所以该函数执行完后&Singleton::init()只执行一次，而&Singleton::init()函数定义如下：

	static void init()
	{
		value_ = new T();
		::atexit(destroy);
	}

该函数内部实例化了一个T类型的对象，由于Singleton构造函数和析构函数都是私有的，只能通过init()函数实例化T对象，而init()函数在一个进程中只会调用一次，所以保证了T对象只会被创建一次。

回到测试程序：

	muduo::Thread t1(threadFunc);
	t1.start();
	t1.join();
	printf("tid=%d, %p name=%s\n",
	     muduo::CurrentThread::tid(),
	     &muduo::Singleton<Test>::instance(),
	     muduo::Singleton<Test>::instance().name().c_str());

这部分代码在线程和主程序中分别创建一个Test对象，然后打印对象的地址和信息，从测试的结果看，主程序和线程都是在修改同一个对象，这从打印的对象地址可以看到，而对象的析构函数也执行了一次，所以至始至终只创建了一个Test对象。