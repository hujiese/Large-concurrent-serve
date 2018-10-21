## muduo源码分析--ThreadLocal ##

### 一、线程特定数据 ###

在单线程程序中，我们经常要用到"全局变量"以实现多个函数间共享数据。但是在多线程环境下，由于数据空间是共享的，因此全局变量也为所有线程所共有。 然而时应用程序设计中有必要提供线程私有的全局变量，仅在某个线程中有效，但却可以跨多个函数访问。

POSIX线程库通过维护一定的数据结构来解决这个问题，这个些数据称为（Thread-specific Data，或 TSD）。
线程特定数据也称为线程本地存储TLS（Thread-local storage），对于POD类型的线程本地存储，可以用__thread关键字。

下面说一下线程存储的具体用法。

- 创建一个类型为pthread_key_t类型的变量。

- 调用pthread_key_create()来创建该变量。该函数有两个参数，第一个参数就是上面声明的pthread_key_t变量，第二个参数是一个清理函数，用来在线程释放该线程存储的时候被调用。该函数指针可以设成 NULL，这样系统将调用默认的清理函数。该函数成功返回0.其他任何返回值都表示出现了错误。

- 当线程中需要存储特殊值的时候，可以调用 pthread_setspcific() 。该函数有两个参数，第一个为前面声明的pthread_key_t变量，第二个为void*变量，这样你可以存储任何类型的值。

- 如果需要取出所存储的值，调用pthread_getspecific()。该函数的参数为前面提到的pthread_key_t变量，该函数返回void *类型的值。

下面是前面提到的函数的原型：


	int pthread_setspecific(pthread_key_t key, const void *value);

	void *pthread_getspecific(pthread_key_t key);

	int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));

下面是一个使用例子：

	#include <stdio.h>
	#include <stdlib.h>
	#include <unistd.h>
	#include <pthread.h>
	
	pthread_key_t key; 
	
	struct test_struct { // 用于测试的结构
	    int i;
	    float k;
	};
	
	void *child1(void *arg)
	{
	    struct test_struct struct_data; // 首先构建一个新的结构
	    struct_data.i = 10;
	    struct_data.k = 3.1415;
	    pthread_setspecific(key, &struct_data); 
	    printf("child1--address of struct_data is --> 0x%p\n", &(struct_data));
	    printf("child1--from pthread_getspecific(key) get the pointer and it points to --> 0x%p\n", (struct test_struct *)pthread_getspecific(key));
	    printf("child1--from pthread_getspecific(key) get the pointer and print it's content:\nstruct_data.i:%d\nstruct_data.k: %f\n", 
	        ((struct test_struct *)pthread_getspecific(key))->i, ((struct test_struct *)pthread_getspecific(key))->k);
	    printf("------------------------------------------------------\n");
	}
	void *child2(void *arg)
	{
	    int temp = 20;
	    sleep(2);
	    printf("child2--temp's address is 0x%p\n", &temp);
	    pthread_setspecific(key, &temp); // 好吧，原来这个函数这么简单
	    printf("child2--from pthread_getspecific(key) get the pointer and it points to --> 0x%p\n", (int *)pthread_getspecific(key));
	    printf("child2--from pthread_getspecific(key) get the pointer and print it's content --> temp:%d\n", *((int *)pthread_getspecific(key)));
	}
	int main(void)
	{
	    pthread_t tid1, tid2;
	    pthread_key_create(&key, NULL); // 这里是构建一个pthread_key_t类型，确实是相当于一个key
	    pthread_create(&tid1, NULL, child1, NULL);
	    pthread_create(&tid2, NULL, child2, NULL);
	    pthread_join(tid1, NULL);
	    pthread_join(tid2, NULL);
	    pthread_key_delete(key);
	    return (0);
	}

运行结果如下：

	child1--address of struct_data is --> 0x0x7ffff77eff40
	child1--from pthread_getspecific(key) get the pointer and it points to --> 0x0x7ffff77eff40
	child1--from pthread_getspecific(key) get the pointer and print it's content:
	struct_data.i:10
	struct_data.k: 3.141500
	------------------------------------------------------
	child2--temp's address is 0x0x7ffff6feef44
	child2--from pthread_getspecific(key) get the pointer and it points to --> 0x0x7ffff6feef44
	child2--from pthread_getspecific(key) get the pointer and print it's content --> temp:20

### 二、ThreadLocal源码 ###

![](https://i.imgur.com/IYlMYeV.png)

ThreadLocal.h：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_THREADLOCAL_H
	#define MUDUO_BASE_THREADLOCAL_H
	
	#include <boost/noncopyable.hpp>
	#include <pthread.h>
	
	namespace muduo
	{
	
	template<typename T>
	class ThreadLocal : boost::noncopyable
	{
	 public:
	  ThreadLocal()
	  {
	    pthread_key_create(&pkey_, &ThreadLocal::destructor);
	  }
	
	  ~ThreadLocal()
	  {
	    pthread_key_delete(pkey_);
	  }
	
	  T& value()
	  {
	    T* perThreadValue = static_cast<T*>(pthread_getspecific(pkey_));
	    if (!perThreadValue) {
	      T* newObj = new T();
	      pthread_setspecific(pkey_, newObj);
	      perThreadValue = newObj;
	    }
	    return *perThreadValue;
	  }
	
	 private:
	
	  static void destructor(void *x)
	  {
	    T* obj = static_cast<T*>(x);
	    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
	    delete obj;
	  }
	
	 private:
	  pthread_key_t pkey_;
	};
	
	}
	#endif

下面通过一个程序来分析ThreadLocal的源码。

ThreadLocal_test.cc：

	#include <muduo/base/ThreadLocal.h>
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
	
	  const std::string& name() const { return name_; }
	  void setName(const std::string& n) { name_ = n; }
	
	 private:
	  std::string name_;
	};
	
	muduo::ThreadLocal<Test> testObj1;
	muduo::ThreadLocal<Test> testObj2;
	
	void print()
	{
	  printf("tid=%d, obj1 %p name=%s\n",
	         muduo::CurrentThread::tid(),
		 &testObj1.value(),
	         testObj1.value().name().c_str());
	  printf("tid=%d, obj2 %p name=%s\n",
	         muduo::CurrentThread::tid(),
		 &testObj2.value(),
	         testObj2.value().name().c_str());
	}
	
	void threadFunc()
	{
	  print();
	  testObj1.value().setName("changed 1");
	  testObj2.value().setName("changed 42");
	  print();
	}
	
	int main()
	{
	  testObj1.value().setName("main one");
	  print();
	  muduo::Thread t1(threadFunc);
	  t1.start();
	  t1.join();
	  testObj2.value().setName("main two");
	  print();
	
	  pthread_exit(0);
	}

首先测试函数定义了一个Test类：

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
	
	  const std::string& name() const { return name_; }
	  void setName(const std::string& n) { name_ = n; }
	
	 private:
	  std::string name_;
	};

该类的构造函数打印创建该类的线程tid和自身信息，在析构函数中也做了同样的事，但析构函数还打印了变量name_的值。该类通过setName()函数设置name_的值，通过name()函数获取name_的值。

紧接着，测试代码定义了两个ThreadLocal变量：

	muduo::ThreadLocal<Test> testObj1;
	muduo::ThreadLocal<Test> testObj2;

初始化ThreadLocal对象将调用其构造函数：

	ThreadLocal()
	{
		pthread_key_create(&pkey_, &ThreadLocal::destructor);
	}

在该构造函数中调用了pthread_key_create函数创建了一个pkey_。该函数有两个参数，第一个参数就是上面声明的pthread_key_t变量，第二个参数是一个清理函数，用来在线程释放该线程存储的时候被调用,而ThreadLocal::destructor的定义如下：

	private:
	
	static void destructor(void *x)
	{
		T* obj = static_cast<T*>(x);
		typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
		delete obj;
	}

该函数用于释放创建value对象的空间。

在测试函数主函数中：

	testObj1.value().setName("main one");

testObj1调用value()函数，而value()函数的定义如下：

	T& value()
	{
		T* perThreadValue = static_cast<T*>(pthread_getspecific(pkey_));
		if (!perThreadValue) {
		  T* newObj = new T();
		  pthread_setspecific(pkey_, newObj);
		  perThreadValue = newObj;
		}
		return *perThreadValue;
	}

首先该函数将调用pthread_getspecific函数获取pkey_对应的value值，如果不存在则创建该值，然后调用pthread_setspecific存储该值并返回。很明显，第一次调用肯定要创建。

上面代码执行后获取到一个T类型的对象，这个T类型对象在这个测试函数中就是Test类的一个实例，然后调用该实例的setName函数设置字段信息。其余部分分析大致类似，最后的结果如下：

![](https://i.imgur.com/hiLTih8.png)