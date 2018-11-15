## muduo源码分析之Acceptor ##

Acceptor用于accept(2)接受TCP连接。Acceptor的数据成员包括Socket、Channel，Acceptor的socket是listening socket（即server socket）。Channel用于观察此socket的readable事件，并回调Accptor::handleRead()，后者调用accept(2)来接受新连接，并回调用户callback。

下面将通过一个案例来分析该类的源码，该测试案例代码如下：

	void newConnection(int sockfd, const InetAddress& peerAddr)
	{
	  printf("newConnection(): accepted a new connection from %s\n",
		 peerAddr.toIpPort().c_str());
	  ::write(sockfd, "How are you?\n", 13);
	  sockets::close(sockfd);
	}

	int main()
	{
	  printf("main(): pid = %d\n", getpid());

	  InetAddress listenAddr(8888);
	  EventLoop loop;

	  Acceptor acceptor(&loop, listenAddr);
	  acceptor.setNewConnectionCallback(newConnection);
	  acceptor.listen();

	  loop.loop();
	}

在main函数中创建了一个地址：

	InetAddress listenAddr(8888);

这里由于没有设置IP，所以默认127.0.0.1本地回环。然后定义了一个EventLoop对象：

	EventLoop loop;
	
然后创建了一个Acceptor对象，将先前创建的EventLoop和InetAddress对象传入：

	Acceptor acceptor(&loop, listenAddr);
	
接下来可以看看该构造函数做了什么事：

	Acceptor(EventLoop* loop, const InetAddress& listenAddr);

	Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr)
	  : loop_(loop),
	    acceptSocket_(sockets::createNonblockingOrDie()),
	    acceptChannel_(loop, acceptSocket_.fd()),
	    listenning_(false),
	    idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
	{
	  assert(idleFd_ >= 0);
	  acceptSocket_.setReuseAddr(true);
	  acceptSocket_.bindAddress(listenAddr);
	  acceptChannel_.setReadCallback(
	      boost::bind(&Acceptor::handleRead, this));
	}
	
构造函数初始化了很多成员变量，这些变量定义如下：

	typedef boost::function<void (int sockfd, const InetAddress&)> NewConnectionCallback;

	EventLoop* loop_;
	Socket acceptSocket_;
	Channel acceptChannel_;
	NewConnectionCallback newConnectionCallback_;
	bool listenning_;
	int idleFd_;

其中：

	acceptSocket_(sockets::createNonblockingOrDie()),
	
通过调用sockets::createNonblockingOrDie()创建了一个非阻塞的套接字文件描述符，该函数定义如下：

	int sockets::createNonblockingOrDie()
	{
	  // socket
	#if VALGRIND
	  int sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	  if (sockfd < 0)
	  {
	    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
	  }

	  setNonBlockAndCloseOnExec(sockfd);
	#else
	  // Linux 2.6.27以上的内核支持SOCK_NONBLOCK与SOCK_CLOEXEC
	  int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
	  if (sockfd < 0)
	  {
	    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
	  }
	#endif
	  return sockfd;
	}
	
由于没有定义VALGRIND，所以这里执行#else中的代码，获取到一个sockfd。

接着：

	acceptChannel_(loop, acceptSocket_.fd()),
	
将EventLoop对象和上面的套接字传入到一个Channel中，之前对Channel分析过，该Channel将会与该套接字绑定，然后设置“监听中”为false，再初始化一个指向/dev/null黑洞的文件描述符：

	idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
	
在构造函数体内部设置了地址复用、绑定监听套接字地址和执行回调函数三个操作：

	assert(idleFd_ >= 0);
	acceptSocket_.setReuseAddr(true);
	acceptSocket_.bindAddress(listenAddr);
	acceptChannel_.setReadCallback(boost::bind(&Acceptor::handleRead, this));

其中事件绑定函数为Acceptor::handleRead，这个函数的定义如下所示：

	void Acceptor::handleRead()
	{
	  loop_->assertInLoopThread();
	  InetAddress peerAddr(0);
	  //FIXME loop until no more
	  int connfd = acceptSocket_.accept(&peerAddr);
	  if (connfd >= 0)
	  {
	    // string hostport = peerAddr.toIpPort();
	    // LOG_TRACE << "Accepts of " << hostport;
	    if (newConnectionCallback_)
	    {
	      newConnectionCallback_(connfd, peerAddr);
	    }
	    else
	    {
	      sockets::close(connfd);
	    }
	  }
	  else
	  {
	    // Read the section named "The special problem of
	    // accept()ing when you can't" in libev's doc.
	    // By Marc Lehmann, author of livev.
	    if (errno == EMFILE)
	    {
	      ::close(idleFd_);
	      idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
	      ::close(idleFd_);
	      idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
	    }
	  }
	}
	
该函数首先判断调用是否在当前线程中，然后创建了一个客户端监听地址，紧接着又调用了监听套接字的accept函数，获取一个connfd连接套接字，如果连接成功而且newConnectionCallback_不为空，那么就将该连接connfd和客户端地址传入该连接函数中，该连接函数定义如下：

	typedef boost::function<void (int sockfd, const InetAddress&)> NewConnectionCallback;
	
	NewConnectionCallback newConnectionCallback_;

没有设置连接处理函数，那么就直接关闭该连接套接字connfd；如果connfd<0，连接失败，就将该监听套接字连接到的客户端文件描述符送到idleFd中，在将idleFd指向"/dev/null"地狱。至于这个newConnectionCallback_如何设置，下面马上会看到。

回到测试函数，在定义好了一个Acceptor之后：

	acceptor.setNewConnectionCallback(newConnection);
	
设置了acceptor的处理客户端连接请求的回调函数，这个也就是对于客户端的业务函数，setNewConnectionCallback函数定义如下：

	 void setNewConnectionCallback(const NewConnectionCallback& cb)
	  { newConnectionCallback_ = cb; }

这个cb对应于测试函数就是：

	void newConnection(int sockfd, const InetAddress& peerAddr)
	{
	  printf("newConnection(): accepted a new connection from %s\n",
	     peerAddr.toIpPort().c_str());
	  ::write(sockfd, "How are you?\n", 13);
	  sockets::close(sockfd);
	}

接受一个socket文件描述符和客户端地址，函数里将打印客户端地址，然后发送给客户端一行字符串，最后关闭连接。

回到测试函数：

	acceptor.listen();

该函数定义如下：

	void Acceptor::listen()
	{
	  loop_->assertInLoopThread();
	  listenning_ = true;
	  acceptSocket_.listen();
	  acceptChannel_.enableReading();
	}

调用监听套接字的lisnten函数和使能acceptChannel_的读。

最后一切就绪，开始事件循环：

	loop.loop();

所以，总结起来就是当客户端有连接时，EventLoop对象将获取到活跃的连接套接字及其绑定的处理回调读函数Acceptor::handleRead()，该函数被调用后如果连接到客户端并获取客户端文件描述符后将调用回调函数newConnectionCallback_(connfd, peerAddr)，也就是调用了void newConnection(int sockfd, const InetAddress& peerAddr)函数，在该函数内部处理客户端业务。