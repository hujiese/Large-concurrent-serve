## 基于对象风格的Thread类封装 ##

### 一、boost bind/function ###

boost bind/function库的出现，替代了stl中的mem_fun,ptr_fun,bind1st,bin2nd等函数。使用的一个案例如下所示：

	#include <iostream>
	#include <boost/function.hpp>
	#include <boost/bind.hpp>
	using namespace std;
	class Foo
	{
	public:
		void memberFunc(double d, int i, int j)
		{
			cout << d << endl;//打印0.5
			cout << i << endl;//打印100       
			cout << j << endl;//打印10
		}
	};
	int main()
	{
		Foo foo;
		boost::function<void (int, int)> fp = boost::bind(&Foo::memberFunc, &foo, 0.5, _1, _2);
		fp(100, 200);
		boost::function<void (int, int)> fp2 = boost::bind(&Foo::memberFunc, boost::ref(foo), 0.5, _1, _2);
		fp2(55, 66);
		return 0;
	}


编译运行结果如下：

![](https://i.imgur.com/oJPhLUM.png)

### 二、基于对象风格的Thread ###

#### 1、Thread类图 ####

	typedef boost::function<void ()> ThreadFunc;

![](https://i.imgur.com/czc3Cc4.png)


#### 2、实现 ####

Thread.h:

	#ifndef _THREAD_H_
	#define _THREAD_H_
	
	#include <pthread.h>
	#include <boost/function.hpp>
	
	class Thread
	{
	public:
		typedef boost::function<void ()> ThreadFunc;
		explicit Thread(const ThreadFunc& func);
	
		void Start();
		void Join();
	
		void SetAutoDelete(bool autoDelete);
	
	private:
		static void* ThreadRoutine(void* arg);
		void Run();
		ThreadFunc func_;
		pthread_t threadId_;
		bool autoDelete_;
	};
	
	#endif // _THREAD_H_

Thread.cpp:

	#include "Thread.h"
	#include <iostream>
	using namespace std;
	
	
	Thread::Thread(const ThreadFunc& func) : func_(func), autoDelete_(false)
	{
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
	
	void Thread::Run()
	{
		func_();
	}

从上面代码看出，基于对象的Thread类内部绑定了一个ThreadFunc函数来执行用户的业务函数，Run函数本质上也就是调用了ThreadFunc函数。

测试代码：

	#include "Thread.h"
	#include <boost/bind.hpp>
	#include <unistd.h>
	#include <iostream>
	using namespace std;
	
	class Foo
	{
	public:
		Foo(int count) : count_(count)
		{
		}
	
		void MemberFun()
		{
			while (count_--)
			{
				cout<<"this is a test ..."<<endl;
				sleep(1);
			}
		}
	
		void MemberFun2(int x)
		{
			while (count_--)
			{
				cout<<"x="<<x<<" this is a test2 ..."<<endl;
				sleep(1);
			}
		}
	
		int count_;
	};
	
	void ThreadFunc()
	{
		cout<<"ThreadFunc ..."<<endl;
	}
	
	void ThreadFunc2(int count)
	{
		while (count--)
		{
			cout<<"ThreadFunc2 ..."<<endl;
			sleep(1);
		}
	}
	
	
	int main(void)
	{
		Thread t1(ThreadFunc);
		Thread t2(boost::bind(ThreadFunc2, 3));
		Foo foo(3);
		Thread t3(boost::bind(&Foo::MemberFun, &foo));
		Foo foo2(3);
		Thread t4(boost::bind(&Foo::MemberFun2, &foo2, 1000));
	
		t1.Start();
		t2.Start();
		t3.Start();
		t4.Start();
	
		t1.Join();
		t2.Join();
		t3.Join();
		t4.Join();
	
	
		return 0;
	}

测试结果如下：

![](https://i.imgur.com/UEH4viH.png)

在测试程序中先创建了一个线程类t1，t1调用ThreadFunc函数打印了一次信息；

然后程序创建了t2线程，传入函数绑定了ThreadFunc2，其中传入了参数3，然后执行三次打印信息；

在主程序中创建了Foo对象，然后分别绑定该Foo对象的两个函数，打印相关信息。


### 三、总结 ###

以回射服务器EchoServer为例，该EchoServer需要在内部实现OnConnection、OnMessage和OnClose三个函数，对于三种不同风格的的编程来说：

- C编程风格：注册三个全局函数到网络库，网路库通过函数指针来回调
- 面向对象风格：用一个EchoServer继承TcpServer(抽象类)，实现三个接口的具体业务逻辑
- 基于对象风格：用一个EchoServer包含一个TcpServer(具体类)对象，在构造函数中用boost::bind来注册三个成员函数

例如：

	class EchoServer
	{
	public:
		EchoServer()
	   {
			server.SetConnectionCallback(boost::bind(onConnection));
			server.SetOnMessageCallback(boost::bind(onConnection));
			server.SetOnCloseCallback(boost::bind(onConnection));
	   }
	
		void OnConnection(){...}
		void OnMessage();
		voie OnClose();
	
		TcpServer server;
	}
