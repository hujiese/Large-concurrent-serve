## muduo Reactor ##

### 一、结构 ###

muduo的Reactor模式主要由三个类--Channel、EventLoop和Poller构成，它们之间的调用关系如下图所示：

![](https://i.imgur.com/fAwdZ7q.png)

由于调用有些复杂，下面将根据具体的一个案例来分析整个运作流程，具体的代码可以参考src目录下文件。

### 二、分析 ###

#### 1、补充timerfd_create ####

timerfd 是在Linux内核2.6.25版本中添加的接口，其是Linux为用户提供的一个定时器接口。这个接口基于文件描述符，所以可以被用于select/poll/epoll的场景。当使用timerfd API创建多个定时器任务并置于poll中进行事件监听，当没有可响应的事件，则程序阻塞在poll中，当有事件发生，通过poll的这个事件入口，对产生的事件进行响应，从而构成了一个事件轮询程序，其相关函数如下所示：

	#include <time.h>
	int clock_gettime(clockid_t clockid, struct timespec *tp);

- clock_gettime函数主要用于获取系统时间，精确到纳秒级别。在编译时需要添加-lrt库，clockid_t clockid指定用何种模式获取时间，struct timespec *tp用于存储获取到的时间。其中clockid主要有如下常用的参数： 
- CLOCK_REALTIME:系统实时时间,随系统实时时间改变而改变,即从UTC1970-1-1 0:0:0开始计时,中间时刻如果系统时间被用户改成其他,则对应的时间相应改变 
- CLOCK_MONOTONIC:从系统启动这一刻起开始计时,不受系统时间被用户改变的影响 
- CLOCK_PROCESS_CPUTIME_ID:本进程到当前代码系统CPU花费的时间 
- CLOCK_THREAD_CPUTIME_ID:本线程到当前代码系统CPU花费的时间

第二组：

	#include <sys/timerfd.h>
	int timerfd_create(int clockid, int flags);
	int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,struct itimerspec *old_value);
	int timerfd_gettime(int fd, struct itimerspec *curr_value);

- timerfd_create函数主要用于生成一个定时器对象，返回与之关联的文件描述符，clockid可以设置CLOCK_REALTIME和CLOCK_MONOTONIC，flags可以设置为TFD_NONBLOCK（非阻塞），TFD_CLOEXEC（同O_CLOEXEC）
- timerfd_settime用于启动和停止定时器，fd为timerfd_create获得的定时器文件描述符，flags为0表示是相对定时器，为TFD_TIMER_ABSTIME表示是绝对定时器。const struct itimerspec *new_value表示设置超时的时间。

其数据结构如下：

	struct timespec {
	  time_t tv_sec;                /* Seconds */
	  long   tv_nsec;               /* Nanoseconds */
	};
	
	struct itimerspec {
	 struct timespec it_interval;  /* Interval for periodic timer */
	 struct timespec it_value;     /* Initial expiration */
	};


需要注意的是itimerspec 结构成员表示的意义： 

- it_value是首次超时时间，需要填写从clock_gettime获取的时间，并加上要超时的时间。 
- it_interval是后续周期性超时时间，是多少时间就填写多少。 
- it_interval不为0则表示是周期性定时器。 
- it_value和it_interval都为0表示停止定时器。

timerfd_gettime此函数用于获得定时器距离下次超时还剩下的时间。如果调用时定时器已经到期，并且该定时器处于循环模式（设置超时时间时struct itimerspec::it_interval不为0），那么调用此函数之后定时器重新开始计时。

#### 2、测试案例 ####

	#include "Channel.h"
	#include "EventLoop.h"
	
	#include <stdio.h>
	#include <sys/timerfd.h>
	
	muduo::EventLoop* g_loop;
	
	void timeout()
	{
	  printf("Timeout!\n");
	  g_loop->quit();
	}
	
	int main()
	{
	  muduo::EventLoop loop;
	  g_loop = &loop;
	
	  int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	  muduo::Channel channel(&loop, timerfd);
	  channel.setReadCallback(timeout);
	  channel.enableReading();
	
	  struct itimerspec howlong;
	  bzero(&howlong, sizeof howlong);
	  howlong.it_value.tv_sec = 5;
	  ::timerfd_settime(timerfd, 0, &howlong, NULL);
	
	  loop.loop();
	
	  ::close(timerfd);
	}

#### 3、追踪分析 ####

首先，在主函数中定义了一个变量loop：

	muduo::EventLoop loop;

然后将其赋值全局的EventLoop：

	g_loop = &loop;

全局EventLoop定义如下：

	muduo::EventLoop* g_loop;

一旦定义了一个EventLoop对象，那么将调用该对象的构造函数：

	EventLoop::EventLoop()
	  : looping_(false),
	    quit_(false),
	    threadId_(CurrentThread::tid()),
	    poller_(new Poller(this))
	{
	  LOG_TRACE << "EventLoop created " << this << " in thread " << threadId_;
	  if (t_loopInThisThread)
	  {
	    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
	              << " exists in this thread " << threadId_;
	  }
	  else
	  {
	    t_loopInThisThread = this;
	  }
	}

构造函数初始化了四个成员变量，它们是：

	typedef std::vector<Channel*> ChannelList;
	
	bool looping_; /* atomic */
	bool quit_; /* atomic */
	const pid_t threadId_;
	boost::scoped_ptr<Poller> poller_;

looping_设置为false，表示loop()函数没有被调用，没有进入事件监听状态；quit_设置为false，没有退出；threadId设置为CurrentThread::tid()，当前线程的tid；创建一个新的Poller给poller_。下面看Poller创建发生了什么。

Poller创建将调用其构造函数：

	Poller(EventLoop* loop);
	
	Poller::Poller(EventLoop* loop)
	  : ownerLoop_(loop)
	{
	}

其构造函数接受一个EventLoop*指针（上面传递的是this），赋值对象是ownerLoop，ownerLoop定义如下：

	EventLoop* ownerLoop_;

继续回到EventLoop构造函数中。在初始化四个变量后先打印该EventLoop对象的信息：

	LOG_TRACE << "EventLoop created " << this << " in thread " << threadId_;

然后做了个判断：

	if (t_loopInThisThread)
	{
		LOG_FATAL << "Another EventLoop " << t_loopInThisThread
	          << " exists in this thread " << threadId_;
	}
	else
	{
		t_loopInThisThread = this;
	}

t_loopInThisThread定义如下：

	__thread EventLoop* t_loopInThisThread = 0;

如果该变量为0，说明该EventLoop对象是刚刚创建的，将该对象的this指针赋值给t_loopInThisThread;如果该变量不为0，说明该变量已经被赋值了，该EventLoop对象已经存在，该构造函数如果被调用多次将出现这种情况。

回到测试函数，在初始化全局EventLoop对象后，测试函数初始化了一个定时器文件描述符：

	int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)

然后创建了一个Channel对象：

	muduo::Channel channel(&loop, timerfd);

Channel构造函数如下：

	Channel(EventLoop* loop, int fd);
	
	Channel::Channel(EventLoop* loop, int fdArg)
	  : loop_(loop),
	    fd_(fdArg),
	    events_(0),
	    revents_(0),
	    index_(-1)
	{
	}

其构造函数接受两个参数，一个是EventLoop指针，一个是文件描述符。构造函数初始化了五个成员变量：

	EventLoop* loop_;
	const int  fd_;
	int        events_;
	int        revents_;
	int        index_; // used by Poller.

loop_用于保存传入的EventLoop指针；fd_用来保存传入的文件描述符；events_用来保存需要监听的事件，暂时初始化为0；revents_用来保存当前活跃的事件，暂时初始化为0；index_供Poller调用，Poller中有一个Channel数组，这个index_用来保存该Channel在Poller的Channel数组中的索引，初始化为-1。

回到测试函数，在初始化一个Channel之后，测试函数调用了：

	channel.setReadCallback(timeout);

该函数在Channel类中的定义如下：

	void setReadCallback(const EventCallback& cb)
	  { readCallback_ = cb; }

该函数接受一个const EventCallback&类型的参数，该参数的定义如下：

	typedef boost::function<void()> EventCallback

一个普通的函数对象，该函数将传入的cb函数对象赋值给readCallback_，readCallback_定义如下：

	EventCallback readCallback_

经过channel.setReadCallback(timeout);调用，将timeout传入给了Channel内部的readCallback_，然而在测试函数中timeout函数的定义如下：

	void timeout()
	{
	  printf("Timeout!\n");
	  g_loop->quit();
	}

该函数将调用全局EventLoop g_loop的quit()函数，然而EventLoop中的quit函数定义如下：

	void EventLoop::quit()
	{
	  quit_ = true;
	  // wakeup();
	}

设置quit_标志位为0。

回到测试函数，测试函数设置了channel的读回调函数之后使能了读操作：

	channel.enableReading()

使能读操作定义如下：

	void enableReading() { events_ |= kReadEvent; update(); }

在该函数中将events_与kReadEvent做了或运算，kReadEvent定义如下：

	static const int kNoneEvent

初始化为：

	const int Channel::kReadEvent = POLLIN | POLLPRI;

设置为POLLIN读事件。

enableReading函数除了设置监听事件以外还调用了update()函数，该函数定义如下：

	void Channel::update()
	{
	  loop_->updateChannel(this);
	}

在Channel的update函数中调用了EventLoop的updateChannel函数，传入了Channel的this指针，跳转到EventLoop类，其updateChannel函数的定义如下：

	void EventLoop::updateChannel(Channel* channel)
	{
	  assert(channel->ownerLoop() == this);
	  assertInLoopThread();
	  poller_->updateChannel(channel);
	}

该函数接收一个Channel指针，然后判断该Channel是否是该EventLoop对象中（按照上面的Channel初始化方法，断言判断是正确的，通过），然后调用EventLoop的assertInLoopThread函数，该函数定义如下：

void assertInLoopThread()
{
	if (!isInLoopThread())
	{
	  abortNotInLoopThread();
	}
}

该函数又调用了isInLoopThread函数，该函数定义如下：

	bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

该函数判断当前EventLoop是否在当前线程中（一个线程只能调用一个EventLoop，一个线程不能调用其他线程的EventLoop对象），否则调用abortNotInLoopThread函数打印错误信息：

	void EventLoop::abortNotInLoopThread()
	{
	  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
	            << " was created in threadId_ = " << threadId_
	            << ", current thread id = " <<  CurrentThread::tid();
	}

再回到EventLoop::updateChannel函数，在判断完该EventLoop是否处于当前线程后调用了Poller的updateChannel函数，传入Channel指针，该函数定义如下：

	void Poller::updateChannel(Channel* channel)
	{
	  assertInLoopThread();
	  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
	  if (channel->index() < 0) {
	    // a new one, add to pollfds_
	    assert(channels_.find(channel->fd()) == channels_.end());
	    struct pollfd pfd;
	    pfd.fd = channel->fd();
	    pfd.events = static_cast<short>(channel->events());
	    pfd.revents = 0;
	    pollfds_.push_back(pfd);
	    int idx = static_cast<int>(pollfds_.size())-1;
	    channel->set_index(idx);
	    channels_[pfd.fd] = channel;
	  } else {
	    // update existing one
	    assert(channels_.find(channel->fd()) != channels_.end());
	    assert(channels_[channel->fd()] == channel);
	    int idx = channel->index();
	    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
	    struct pollfd& pfd = pollfds_[idx];
	    assert(pfd.fd == channel->fd() || pfd.fd == -1);
	    pfd.events = static_cast<short>(channel->events());
	    pfd.revents = 0;
	    if (channel->isNoneEvent()) {
	      // ignore this pollfd
	      pfd.fd = -1;
	    }
	  }
	}

该函数首先判断该Channel是否在当前EventLoop线程中，判断该Channel调用其index()函数，该函数如下：

	int index() { return index_; }

index_的作用之前也提到过，在下面的代码中将更加具体地说明。

很明显，第一次创建Channel时候，回到前面的Channel初始化构造函数代码：

	Channel::Channel(EventLoop* loop, int fdArg)
	  : loop_(loop),
	    fd_(fdArg),
	    events_(0),
	    revents_(0),
	    index_(-1)
	{
	}

index_的初值是设置为-1的，所以channel->index() < 0成立，执行第一个分支代码：

    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);
    channels_[pfd.fd] = channel;

在该分支中首先判断该Channel的文件描述符fd_是否在channels_中，Poller的channels_定义如下：

	typedef std::map<int, Channel*> ChannelMap;
	ChannelMap channels_;

该map将一个Channel的文件描述符fd_作为key，将该Channel的一个指针作为value传入。很明显，当前状态下map里面什么东西都没有，所以该断言成立，执行下面的代码：

	struct pollfd pfd;
	pfd.fd = channel->fd();
	pfd.events = static_cast<short>(channel->events());
	pfd.revents = 0;

该部分定义了一个pollfd结构体，并将该Channel的一些文件描述符、监听事件和活跃事件信息传递给该pollfd结构体。然后执行：

	pollfds_.push_back(pfd);

将该结构体保持到pollfds_中，其中pollfds_为：

	typedef std::vector<struct pollfd> PollFdList;
	PollFdList pollfds_;

该vector等价于一个pollfd数组，在调用poll函数时需要传入。接下来：

    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);

获取到pollfds_数组的长度并减一，赋值到idx中，将该Channel的index_设置为idx，也就是在该数组中的索引。由于一开始只有一个pollfd，并且执行了pollfds_.push_back(pfd)操作，所以pollfds_.size()为1，所以如果要成为数组的第一个元素需要进行减一操作。

当然，如果channel->index()不为负数，也就是说该Channel已经在pollfds数组中登记过，那么执行下面代码：

	// update existing one
	assert(channels_.find(channel->fd()) != channels_.end());
	assert(channels_[channel->fd()] == channel);
	int idx = channel->index();
	assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
	struct pollfd& pfd = pollfds_[idx];
	assert(pfd.fd == channel->fd() || pfd.fd == -1);
	pfd.events = static_cast<short>(channel->events());
	pfd.revents = 0;
	if (channel->isNoneEvent()) {
	  // ignore this pollfd
	  pfd.fd = -1;
	}

其实也就是给数组中的pollfd结构重新赋值、更新。

回到测试函数，接下来设置定时器：

	struct itimerspec howlong;
	bzero(&howlong, sizeof howlong);
	howlong.it_value.tv_sec = 5;
	::timerfd_settime(timerfd, 0, &howlong, NULL);

然后调用：

	loop.loop();

该函数定义如下：

	void EventLoop::loop()
	{
	  assert(!looping_);
	  assertInLoopThread();
	  looping_ = true;
	  quit_ = false;
	
	  while (!quit_)
	  {
	    activeChannels_.clear();
	    poller_->poll(kPollTimeMs, &activeChannels_);
	    for (ChannelList::iterator it = activeChannels_.begin();
	        it != activeChannels_.end(); ++it)
	    {
	      (*it)->handleEvent();
	    }
	  }
	
	  LOG_TRACE << "EventLoop " << this << " stop looping";
	  looping_ = false;
	}

该函数调用了：

	poller_->poll(kPollTimeMs, &activeChannels_);

而Poller得poll函数定义如下：

	Timestamp Poller::poll(int timeoutMs, ChannelList* activeChannels)
	{
	  // XXX pollfds_ shouldn't change
	  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
	  Timestamp now(Timestamp::now());
	  if (numEvents > 0) {
	    LOG_TRACE << numEvents << " events happended";
	    fillActiveChannels(numEvents, activeChannels);
	  } else if (numEvents == 0) {
	    LOG_TRACE << " nothing happended";
	  } else {
	    LOG_SYSERR << "Poller::poll()";
	  }
	  return now;
	}

该函数调用系统的poll函数，poll函数返回一个numEvents事件个数，如果有事件发生，则调用fillActiveChannels函数，该函数定义如下：

	void Poller::fillActiveChannels(int numEvents,
	                                ChannelList* activeChannels) const
	{
	  for (PollFdList::const_iterator pfd = pollfds_.begin();
	      pfd != pollfds_.end() && numEvents > 0; ++pfd)
	  {
	    if (pfd->revents > 0)
	    {
	      --numEvents;
	      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
	      assert(ch != channels_.end());
	      Channel* channel = ch->second;
	      assert(channel->fd() == pfd->fd);
	      channel->set_revents(pfd->revents);
	      // pfd->revents = 0;
	      activeChannels->push_back(channel);
	    }
	  }
	}

该函数将遍历PollFdList数组pollfds_，找到时间集合中中需要处理事件的pollfd结构体，然后通过该结构体中保存的文件描述符在ChannelMap中找到该结构体对应的Channel，，并设置该Channel的响应事件revents，然后将该Channel加入到ChannelList中，ChannelList定义如下：

	typedef std::vector<Channel*> ChannelList;
	ChannelList activeChannels_;

用于保存一个EventLoop中需要处理响应的Channel。

回到EventLoop::loop函数，解析来执行：

	for (ChannelList::iterator it = activeChannels_.begin();
	it != activeChannels_.end(); ++it)
	{
	  (*it)->handleEvent();
	}

从上面的ChannelList取出所有的Channel并调用其handleEvent函数处理需要响应的事件，该函数定义如下：

	void Channel::handleEvent()
	{
	  if (revents_ & POLLNVAL) {
	    LOG_WARN << "Channel::handle_event() POLLNVAL";
	  }
	
	  if (revents_ & (POLLERR | POLLNVAL)) {
	    if (errorCallback_) errorCallback_();
	  }
	  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
	    if (readCallback_) readCallback_();
	  }
	  if (revents_ & POLLOUT) {
	    if (writeCallback_) writeCallback_();
	  }
	}

至此，整个Reactor流程分析也结束了。

### 三、总结 ###

![](https://i.imgur.com/9IQnqd2.png)

![](https://i.imgur.com/NWo2a5a.png)