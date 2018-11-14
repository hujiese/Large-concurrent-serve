## muduo源码库分析--TimesQueue ##

注：该部分分析是删减版的代码，后续将不断加入完善。

### 一、Timer类 ###

Timer.h:

	#ifndef MUDUO_NET_TIMER_H
	#define MUDUO_NET_TIMER_H
	
	#include <boost/noncopyable.hpp>
	
	#include "datetime/Timestamp.h"
	#include "Callbacks.h"
	
	namespace muduo
	{
	
	///
	/// Internal class for timer event.
	///
	class Timer : boost::noncopyable
	{
	 public:
	  Timer(const TimerCallback& cb, Timestamp when, double interval)
	    : callback_(cb),
	      expiration_(when),
	      interval_(interval),
	      repeat_(interval > 0.0)
	  { }
	
	  void run() const
	  {
	    callback_();
	  }
	
	  Timestamp expiration() const  { return expiration_; }
	  bool repeat() const { return repeat_; }
	
	  void restart(Timestamp now);
	
	 private:
	  const TimerCallback callback_;//定时器回调函数
	  Timestamp expiration_;//时间戳
	  const double interval_;//多少时间触发
	  const bool repeat_;//是否循环定时触发
	};
	
	}
	#endif  // MUDUO_NET_TIMER_H

Timer.cc:

	#include "Timer.h"
	
	using namespace muduo;
	
	void Timer::restart(Timestamp now)
	{
	  if (repeat_)
	  {
	    expiration_ = addTime(now, interval_);
	  }
	  else
	  {
	    expiration_ = Timestamp::invalid();
	  }
	}

### 二、TimerId类 ###

	#ifndef MUDUO_NET_TIMERID_H
	#define MUDUO_NET_TIMERID_H
	
	#include "datetime/copyable.h"
	
	namespace muduo
	{
	
	class Timer;
	
	///
	/// An opaque identifier, for canceling Timer.
	///
	class TimerId : public muduo::copyable
	{
	 public:
	  explicit TimerId(Timer* timer)
	    : value_(timer)
	  {
	  }
	
	  // default copy-ctor, dtor and assignment are okay
	
	 private:
	  Timer* value_;
	};
	
	}
	
	#endif  // MUDUO_NET_TIMERID_H

### 三、TimerQueue类 ###

TimerQueue类的代码较长，具体可以参考附录代码，下面将从构造函数开始说明该类。

该类构造函数定义如下：

	TimerQueue(EventLoop* loop);

	TimerQueue::TimerQueue(EventLoop* loop)
	  : loop_(loop),
	    timerfd_(createTimerfd()),
	    timerfdChannel_(loop, timerfd_),
	    timers_()
	{
	  timerfdChannel_.setReadCallback(
	      boost::bind(&TimerQueue::handleRead, this));
	  // we are always reading the timerfd, we disarm it with timerfd_settime.
	  timerfdChannel_.enableReading();
	}

该构造函数接收一个EventLoop指针，初始化了四个成员变量，这四个成员变量定义如下：

	EventLoop* loop_;
	const int timerfd_;
	Channel timerfdChannel_;
	// Timer list sorted by expiration
	TimerList timers_;

loop_初始化为传入的EventLoop指针loop。

timerfd_的初始化调用了createTimerfd函数，该函数定义如下：

	int createTimerfd()
	{
	  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
	                                 TFD_NONBLOCK | TFD_CLOEXEC);
	  if (timerfd < 0)
	  {
	    LOG_SYSFATAL << "Failed in timerfd_create";
	  }
	  return timerfd;
	}

通过timerfd_create函数创建了一个timerfd文件描述符。

timerfdChannel_则初始化，传入前面的loop和timerfd_。最后初始化了timers_，其定义如下：

	typedef std::pair<Timestamp, Timer*> Entry;
	typedef std::set<Entry> TimerList

通过时间戳和定时器对象地址以及使用set数据结构可以保证定时器事件不会重复。

回到构造函数，在构造函数体内部设置了TimerQueue::handleRead事件并使能读：

	timerfdChannel_.setReadCallback(
	  boost::bind(&TimerQueue::handleRead, this));
	// we are always reading the timerfd, we disarm it with timerfd_settime.
	timerfdChannel_.enableReading();

下面可以看看TimerQueue::handleRead函数：

	void TimerQueue::handleRead()
	{
	  loop_->assertInLoopThread();
	  Timestamp now(Timestamp::now());
	  readTimerfd(timerfd_, now);
	
	  std::vector<Entry> expired = getExpired(now);
	
	  // safe to callback outside critical section
	  for (std::vector<Entry>::iterator it = expired.begin();
	      it != expired.end(); ++it)
	  {
	    it->second->run();
	  }
	
	  reset(expired, now);
	}

在该函数中创建了当前时间的TimeStamp对象，然后调用readTimerfd函数，传入timerfd_和当前时间戳，该函数定义如下：

	void readTimerfd(int timerfd, Timestamp now)
	{
	  uint64_t howmany;
	  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
	  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
	  if (n != sizeof howmany)
	  {
	    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
	  }
	}

然后再获取到超时时间：

	std::vector<Entry> expired = getExpired(now)

其中getExpired函数定义如下：

	std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
	{
	  std::vector<Entry> expired;
	  Entry sentry = std::make_pair(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
	  TimerList::iterator it = timers_.lower_bound(sentry);
	  assert(it == timers_.end() || now < it->first);
	  std::copy(timers_.begin(), it, back_inserter(expired));
	  timers_.erase(timers_.begin(), it);
	
	  return expired;
	}

该函数接收一个TimeStamp对象，然后在timers_中查找第一个大于传入的now时间（lower_bound），返回一个迭代器，然后assert(it == timers_.end() || now < it->first)判断，如果在timers_中所有时间都比当前时间少，或者小于当前时间比timers_中某时间小，那么说明存在超时时间，然后将该段超时时间拷贝到expired中，然后删除timers_中的这些时间，并返回超时时间。

handleRead函数获取到超时时间数组后直接遍历这些数组，然后调用其it->second->run();函数，该函数在Timer中定义如下：

	void run() const
	{
		callback_();
	}

直接调用注册时的超时时间函数。

至于TimerQueue的其他代码可见src下的代码注解。