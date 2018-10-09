## muduo源码分析--Thread线程 ##

### 一、线程标识符 ###

Linux中，每个进程有一个pid，类型pid_t，由getpid()取得。Linux下的POSIX线程也有一个id，类型pthread_t，由pthread_self()取得，该id由线程库维护，其id空间是各个进程独立的（即不同进程中的线程可能有相同的id）。Linux中的POSIX线程库实现的线程其实也是一个进程（LWP），只是该进程与主进程（启动线程的进程）共享一些资源而已，比如代码段，数据段等。

有时候我们可能需要知道线程的真实pid。比如进程P1要向另外一个进程P2中的某个线程发送信号时，既不能使用P2的pid，更不能使用线程的pthread id，而只能使用该线程的真实pid，称为tid。

有一个函数gettid()可以得到tid，但glibc并没有实现该函数，只能通过Linux的系统调用syscall来获取。
return syscall(SYS_gettid)

### 二、pthread_atfork ###

	#include <pthread.h>
	int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));

调用fork时，内部创建子进程前在父进程中会调用prepare，内部创建子进程成功后，父进程会调用parent ，子进程会调用child。

案例如下：

	#include <stdio.h>
	#include <time.h>
	#include <pthread.h>
	#include <unistd.h>
	
	void prepare(void)
	{
		printf("pid = %d prepare ...\n", static_cast<int>(getpid()));
	}
	
	void parent(void)
	{
		printf("pid = %d parent ...\n", static_cast<int>(getpid()));
	}
	
	void child(void)
	{
		printf("pid = %d child ...\n", static_cast<int>(getpid()));
	}
	
	
	int main(void)
	{
		printf("pid = %d Entering main ...\n", static_cast<int>(getpid()));
	
		pthread_atfork(prepare, parent, child);
	
		fork();
	
		printf("pid = %d Exiting main ...\n",static_cast<int>(getpid()));
	
		return 0;
	}

编译运行结果如下：

![](https://i.imgur.com/uzWrPON.png)

### 三、Thread实现 ###

![](https://i.imgur.com/DmbEBvy.png)

Thread.h:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_THREAD_H
	#define MUDUO_BASE_THREAD_H
	
	#include <muduo/base/Atomic.h>
	#include <muduo/base/Types.h>
	
	#include <boost/function.hpp>
	#include <boost/noncopyable.hpp>
	#include <pthread.h>
	
	namespace muduo
	{
	
	class Thread : boost::noncopyable
	{
	 public:
	  typedef boost::function<void ()> ThreadFunc;
	
	  explicit Thread(const ThreadFunc&, const string& name = string());
	  ~Thread();
	
	  void start();
	  int join(); // return pthread_join()
	
	  bool started() const { return started_; }
	  // pthread_t pthreadId() const { return pthreadId_; }
	  pid_t tid() const { return tid_; }
	  const string& name() const { return name_; }
	
	  static int numCreated() { return numCreated_.get(); }
	
	 private:
	  static void* startThread(void* thread);
	  void runInThread();
	
	  bool       started_;
	  pthread_t  pthreadId_;
	  pid_t      tid_;
	  ThreadFunc func_;
	  string     name_;
	
	  static AtomicInt32 numCreated_;
	};
	
	}
	#endif

Thread.cc:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#include <muduo/base/Thread.h>
	#include <muduo/base/CurrentThread.h>
	#include <muduo/base/Exception.h>
	//#include <muduo/base/Logging.h>
	
	#include <boost/static_assert.hpp>
	#include <boost/type_traits/is_same.hpp>
	
	#include <errno.h>
	#include <stdio.h>
	#include <unistd.h>
	#include <sys/syscall.h>
	#include <sys/types.h>
	#include <linux/unistd.h>
	
	namespace muduo
	{
	namespace CurrentThread
	{
	  // __thread修饰的变量是线程局部存储的。
	  __thread int t_cachedTid = 0;		// 线程真实pid（tid）的缓存，
										// 是为了减少::syscall(SYS_gettid)系统调用的次数
										// 提高获取tid的效率
	  __thread char t_tidString[32];	// 这是tid的字符串表示形式
	  __thread const char* t_threadName = "unknown";
	  const bool sameType = boost::is_same<int, pid_t>::value;
	  BOOST_STATIC_ASSERT(sameType);
	}
	
	namespace detail
	{
	
	pid_t gettid()
	{
	  return static_cast<pid_t>(::syscall(SYS_gettid));
	}
	
	void afterFork()
	{
	  muduo::CurrentThread::t_cachedTid = 0;
	  muduo::CurrentThread::t_threadName = "main";
	  CurrentThread::tid();
	  // no need to call pthread_atfork(NULL, NULL, &afterFork);
	}
	
	class ThreadNameInitializer
	{
	 public:
	  ThreadNameInitializer()
	  {
	    muduo::CurrentThread::t_threadName = "main";
	    CurrentThread::tid();
	    pthread_atfork(NULL, NULL, &afterFork);
	  }
	};
	
	ThreadNameInitializer init;
	}
	}
	
	using namespace muduo;
	
	void CurrentThread::cacheTid()
	{
	  if (t_cachedTid == 0)
	  {
	    t_cachedTid = detail::gettid();
	    int n = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
	    assert(n == 6); (void) n;
	  }
	}
	
	bool CurrentThread::isMainThread()
	{
	  return tid() == ::getpid();
	}
	
	AtomicInt32 Thread::numCreated_;
	
	Thread::Thread(const ThreadFunc& func, const string& n)
	  : started_(false),
	    pthreadId_(0),
	    tid_(0),
	    func_(func),
	    name_(n)
	{
	  numCreated_.increment();
	}
	
	Thread::~Thread()
	{
	  // no join
	}
	
	void Thread::start()
	{
	  assert(!started_);
	  started_ = true;
	  errno = pthread_create(&pthreadId_, NULL, &startThread, this);
	  if (errno != 0)
	  {
	    //LOG_SYSFATAL << "Failed in pthread_create";
	  }
	}
	
	int Thread::join()
	{
	  assert(started_);
	  return pthread_join(pthreadId_, NULL);
	}
	
	void* Thread::startThread(void* obj)
	{
	  Thread* thread = static_cast<Thread*>(obj);
	  thread->runInThread();
	  return NULL;
	}
	
	void Thread::runInThread()
	{
	  tid_ = CurrentThread::tid();
	  muduo::CurrentThread::t_threadName = name_.c_str();
	  try
	  {
	    func_();
	    muduo::CurrentThread::t_threadName = "finished";
	  }
	  catch (const Exception& ex)
	  {
	    muduo::CurrentThread::t_threadName = "crashed";
	    fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
	    abort();
	  }
	  catch (const std::exception& ex)
	  {
	    muduo::CurrentThread::t_threadName = "crashed";
	    fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    abort();
	  }
	  catch (...)
	  {
	    muduo::CurrentThread::t_threadName = "crashed";
	    fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
	    throw; // rethrow
	  }
	}

之前封装过一个“基于对象的”Thread类，通过boost::bind绑定函数来实现的，这里也同样是运用了这种方法和技巧：

	typedef boost::function<void ()> ThreadFunc;

可以通过下面这个案例来跟踪了解Thread类的一个大致的工作流程：

	#include <muduo/base/Thread.h>
	#include <muduo/base/CurrentThread.h>
	
	#include <string>
	#include <boost/bind.hpp>
	#include <stdio.h>
	
	void threadFunc()
	{
	  printf("tid=%d\n", muduo::CurrentThread::tid());
	}
	
	void threadFunc2(int x)
	{
	  printf("tid=%d, x=%d\n", muduo::CurrentThread::tid(), x);
	}
	
	class Foo
	{
	 public:
	  explicit Foo(double x)
	    : x_(x)
	  {
	  }
	
	  void memberFunc()
	  {
	    printf("tid=%d, Foo::x_=%f\n", muduo::CurrentThread::tid(), x_);
	  }
	
	  void memberFunc2(const std::string& text)
	  {
	    printf("tid=%d, Foo::x_=%f, text=%s\n", muduo::CurrentThread::tid(), x_, text.c_str());
	  }
	
	 private:
	  double x_;
	};
	
	int main()
	{
	  printf("pid=%d, tid=%d\n", ::getpid(), muduo::CurrentThread::tid());
	
	  muduo::Thread t1(threadFunc);
	  t1.start();
	  t1.join();
	
	  muduo::Thread t2(boost::bind(threadFunc2, 42),
	                   "thread for free function with argument");
	  t2.start();
	  t2.join();
	
	  Foo foo(87.53);
	  muduo::Thread t3(boost::bind(&Foo::memberFunc, &foo),
	                   "thread for member function without argument");
	  t3.start();
	  t3.join();
	
	  muduo::Thread t4(boost::bind(&Foo::memberFunc2, boost::ref(foo), std::string("Shuo Chen")));
	  t4.start();
	  t4.join();
	
	  printf("number of created threads %d\n", muduo::Thread::numCreated());
	}

编译运行结果如下：

![](https://i.imgur.com/KCkXGgE.png)

以t1线程为例：

	muduo::Thread t1(threadFunc);

该行代码将调用构造函数，而构造函数定义和实现如下：

	typedef boost::function<void ()> ThreadFunc;
	
	explicit Thread(const ThreadFunc&, const string& name = string());
	
	Thread::Thread(const ThreadFunc& func, const string& n)
	  : started_(false),
	    pthreadId_(0),
	    tid_(0),
	    func_(func),
	    name_(n)
	{
	  numCreated_.increment();
	}

除了初始化一些pid、tid和func以外，主要就是让创建线程数目原子加一，numCreated_的定义如下：

	static AtomicInt32 numCreated_;

AtomicInt32在前面已经解析过了，原子操作。

	t1.start();

调用到：

	void start();

	void Thread::start()
	{
	  assert(!started_);
	  started_ = true;
	  errno = pthread_create(&pthreadId_, NULL, &startThread, this);
	  if (errno != 0)
	  {
	    LOG_SYSFATAL << "Failed in pthread_create";
	  }
	}

其中也是通过pthread_create创建了一个线程，传入线程的id，运行函数和自身指针。这里的startThread定义和实现如下：

	static void* startThread(void* thread);
	
	void* Thread::startThread(void* obj)
	{
	  Thread* thread = static_cast<Thread*>(obj);
	  thread->runInThread();
	  return NULL;
	}

定义为静态的了，至于为什么，前面的基于对象的Thread例子里有说明。然后再startThread函数内部调用了runInThread函数，该函数如下：

	void runInThread();
	
	void Thread::runInThread()
	{
	  tid_ = CurrentThread::tid();
	  muduo::CurrentThread::t_threadName = name_.c_str();
	  try
	  {
	    func_();
	    muduo::CurrentThread::t_threadName = "finished";
	  }
	  catch (const Exception& ex)
	  {
	    muduo::CurrentThread::t_threadName = "crashed";
	    fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
	    abort();
	  }
	  catch (const std::exception& ex)
	  {
	    muduo::CurrentThread::t_threadName = "crashed";
	    fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
	    fprintf(stderr, "reason: %s\n", ex.what());
	    abort();
	  }
	  catch (...)
	  {
	    muduo::CurrentThread::t_threadName = "crashed";
	    fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
	    throw; // rethrow
	  }
	}

该函数其实主要就是调用了传入的func_函数，在这里就是threadFunc函数：

	void threadFunc()
	{
	  printf("tid=%d\n", muduo::CurrentThread::tid());
	}

再接着：

	t1.join();

调用到：

	int join(); // return pthread_join()
	
	int Thread::join()
	{
	  assert(started_);
	  return pthread_join(pthreadId_, NULL);
	}

也就是调用了pthread_join函数。

### 三、__thread补充 ###

在Thread.cc中有如下代码：

	namespace CurrentThread
	{
	  __thread int t_cachedTid = 0;
	  __thread char t_tidString[32];
	  __thread const char* t_threadName = "unknown";
	  const bool sameType = boost::is_same<int, pid_t>::value;
	  BOOST_STATIC_ASSERT(sameType);
	}

其中有__thread修饰，这个修饰是gcc内置的线程局部存储设施，__thread只能修饰POD类型。POD类型（plain old data），与C兼容的原始数据，例如，结构和整型等C语言中的类型是 POD 类型，但带有用户定义的构造函数或虚函数的类则不是，例如：

	__thread string t_obj1(“cppcourse”);	// 错误，不能调用对象的构造函数
	__thread string* t_obj2 = new string;	// 错误，初始化只能是编译期常量
	__thread string* t_obj3 = NULL;	// 正确


