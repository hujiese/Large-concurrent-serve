## 用面向对象的方法封装一个Thread类 ##

### 一、类图 ###

![](https://i.imgur.com/BXAzgCj.png)

### 二、实现 ###

首先定义一个基础的Thread类：

#### Thread.h: ####

    #ifndef _THREAD_H_
	#define _THREAD_H_
	
	#include <pthread.h>
	
	class Thread
	{
	public:
		Thread();
		virtual ~Thread();
	
		void Start();
		void Join();
	
		void SetAutoDelete(bool autoDelete);
	
	private:
		static void* ThreadRoutine(void* arg);
		virtual void Run() = 0;
		pthread_t threadId_;
		bool autoDelete_;
	};
	
	#endif // _THREAD_H_

#### Thread.cpp ####

	#include "Thread.h"
	#include <iostream>
	using namespace std;
	
	
	Thread::Thread() : autoDelete_(false)
	{
		cout<<"Thread ..."<<endl;
	}
	
	Thread::~Thread()
	{
		cout<<"~Thread ..."<<endl;
	}
	
	void Thread::Start()
	{
		pthread_create(&threadId_, NULL, ThreadRoutine, this);
	}
	
	void Thread::Join()
	{
		pthread_join(threadId_, NULL);
	}
	
	void* Thread::ThreadRoutine(void* arg)
	{
		Thread* thread = static_cast<Thread*>(arg);
		thread->Run();
		if (thread->autoDelete_)
			delete thread;
		return NULL;
	}
	
	void Thread::SetAutoDelete(bool autoDelete)
	{
		autoDelete_ = autoDelete;
	}

该类中提供了构造和析构函数，析构函数使用了virtual关键字标记为虚函数，为了子类能够彻底析构对象。

该基类也提供了SetAutoDelete函数来让实例化对象在该对象线程执行完后及时地自动化析构自身，默认情况下线程的实例化对象是开启自动析构的，也就是说默认情况下线程对象调用完毕后将自动销毁自身。

该基类提供了Start函数来开启一个线程，线程的回调函数为ThradRuntine，ThreadRuntine设置为私有函数，这里将该函数设置为静态的。为什么设置为静态的是有原因的，理论上Run函数才是线程回调该调用的函数，然而Run函数是需要子类来覆写实现自己的业务逻辑，也为此Run函数使用了virtual关键字。Run函数是Thread基类的一个普通的成员函数，所以其实Run函数本质上是void Run(this)的，在形参中隐藏了this指针，然而pthread_create函数需要的是一个普通的函数，函数定义如下： void *(*start_runtine)(void*)。所以才需要将ThreadRuntine定义为全局或者静态的，定义为全局将将该函数全部暴露，所以定义为静态。

在ThreadRoutine中运行实际业务的Run函数，这里需要注意的是pthead_create函数传递了this指针给ThreadRuntine函数，因为该函数是静态的，它不能操作非静态的函数或者变量，所以在该函数中通过传入的this指针来调用Run函数，然后在调用完毕之后delete掉this，自动销毁自身对象。

### 三、测试 ###

	#include "Thread.h"
	#include <unistd.h>
	#include <iostream>
	using namespace std;
	
	class TestThread : public Thread
	{
	public:
		TestThread(int count) : count_(count)
		{
			cout<<"TestThread ..."<<endl;
		}
	
		~TestThread()
		{
			cout<<"~TestThread ..."<<endl;
		}
	
	private:
		void Run()
		{
			while (count_--)
			{
				cout<<"this is a test ..."<<endl;
				sleep(1);
			}
		}
	
		int count_;
	};
	
	int main(void)
	{
		/*
		TestThread t(5);
		t.Start();
	
		t.Join();
		*/
	
		TestThread* t2 = new TestThread(5);
		t2->SetAutoDelete(true);
		t2->Start();
		t2->Join();
	
		for (; ; )
			pause();
	
		return 0;
	}

测试代码中定义了一个TestThread类，继承自Thread类，然后在自身业务实现的Run方法中每隔一秒打印一条条信息，一共count条。

在main函数中动态创建了一个TestThread对象，然后调用该线程的Start函数执行业务，结果如下：

![](https://i.imgur.com/MXvUx3i.png)

从打印信息可以看到，创建一个TestThread对象，首先调用基类的构造函数，然后调用自己的构造函数，然后由于调用了Start函数，Start函数调用了Run函数，Run函数打印了五条信息后析构自身然后调用父类的析构函数，主线程由于死循环卡死在主程序中。