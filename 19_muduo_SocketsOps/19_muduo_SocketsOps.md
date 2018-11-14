##  muduo源码分析之SocketsOps  ##

###  一、补充知识 ###

下面是几个被封装的函数原型：

	#include <arpe/inet.h>
	int inet_pton(int family, const char *strptr, void *addrptr);     //将点分十进制的ip地址转化为用于网络传输的数值格式 返回值：若成功则为1，若输入不是有效的表达式则为0，若出错则为-1
	const char * inet_ntop(int family, const void *addrptr, char *strptr, size_t len);     //将数值格式转化为点分十进制的ip地址格式返回值：若成功则为指向结构的指针，若出错则为NULL

- 这两个函数的family参数既可以是AF_INET（ipv4）也可以是AF_INET6（ipv6）。如果，以不被支持的地址族作为family参数，这两个函数都返回一个错误，并将errno置为EAFNOSUPPORT.
- 第一个函数尝试转换由strptr指针所指向的字符串，并通过addrptr指针存放二进制结果，若成功则返回值为1，否则如果所指定的family而言输入字符串不是有效的表达式格式，那么返回值为0.

- inet_ntop进行相反的转换，从数值格式（addrptr）转换到表达式（strptr)。inet_ntop函数的strptr参数不可以是一个空指针。调用者必须为目标存储单元分配内存并指定其大小，调用成功时，这个指针就是该函数的返回值。len参数是目标存储单元的大小，以免该函数溢出其调用者的缓冲区。如果len太小，不足以容纳表达式结果，那么返回一个空指针，并置为errno为ENOSPC。

还有：

	#include<sys/socket.h>
	int getsockname(int sockfd, struct sockaddr *localaddr, socklen_t *addrlen); //用于获取与某个套接字关联的本地协议地址
	int getpeername(int sockfd, struct sockaddr *peeraddr, socklen_t *addrlen); //用于获取与某个套接字关联的外地协议地址

对于这两个函数，如果函数调用成功，则返回0，如果调用出错，则返回-1。使用这两个函数，我们可以通过套接字描述符来获取自己的IP地址和连接对端的IP地址，如在未调用bind函数的TCP客户端程序上，可以通过调用getsockname()函数获取由内核赋予该连接的本地IP地址和本地端口号，还可以在TCP的服务器端accept成功后，通过getpeername()函数来获取当前连接的客户端的IP地址和端口号。

### 二、源码分析 ###

该类其实在文件中定义不是SocketsOps，而是sockets，主要封装了一些套接字的操作函数。该类的定义如下：

	#ifndef MUDUO_NET_SOCKETSOPS_H
	#define MUDUO_NET_SOCKETSOPS_H

	#include <arpa/inet.h>

	namespace muduo
	{
	namespace net
	{
	namespace sockets
	{

	///
	/// Creates a non-blocking socket file descriptor,
	/// abort if any error.
	int createNonblockingOrDie();

	int  connect(int sockfd, const struct sockaddr_in& addr);
	void bindOrDie(int sockfd, const struct sockaddr_in& addr);
	void listenOrDie(int sockfd);
	int  accept(int sockfd, struct sockaddr_in* addr);
	ssize_t read(int sockfd, void *buf, size_t count);
	ssize_t readv(int sockfd, const struct iovec *iov, int iovcnt);
	ssize_t write(int sockfd, const void *buf, size_t count);
	void close(int sockfd);
	void shutdownWrite(int sockfd);

	void toIpPort(char* buf, size_t size,
		      const struct sockaddr_in& addr);
	void toIp(char* buf, size_t size,
		  const struct sockaddr_in& addr);
	void fromIpPort(const char* ip, uint16_t port,
		          struct sockaddr_in* addr);

	int getSocketError(int sockfd);

	struct sockaddr_in getLocalAddr(int sockfd);
	struct sockaddr_in getPeerAddr(int sockfd);
	bool isSelfConnect(int sockfd);

	}
	}
	}

	#endif  // MUDUO_NET_SOCKETSOPS_H
	
接下来可以分析这些定义的函数源码，整体源码可见src下附录。

首先将sockaddr定义为了SA类型：
	
	typedef struct sockaddr SA;
	
下面这两个函数将sockaddr_in*类型转换为sockaddr*类型：

	const SA* sockaddr_cast(const struct sockaddr_in* addr)
	{
	  return static_cast<const SA*>(implicit_cast<const void*>(addr));
	}

	SA* sockaddr_cast(struct sockaddr_in* addr)
	{
	  return static_cast<SA*>(implicit_cast<void*>(addr));
	}
	
下面这个函数设置套接字文件描述符为非阻塞close-on-exec，意思是调用套接字的进程如果创建了子进程，那么子进程将自动关闭这个拷贝的文件描述符，该文件的状态就不会带到子进程中，具体可以参考这篇文章，很详细： https://blog.csdn.net/gettogetto/article/details/52263660

	void setNonBlockAndCloseOnExec(int sockfd)
	{
	  // non-block
	  int flags = ::fcntl(sockfd, F_GETFL, 0);
	  flags |= O_NONBLOCK;
	  int ret = ::fcntl(sockfd, F_SETFL, flags);
	  // FIXME check

	  // close-on-exec
	  flags = ::fcntl(sockfd, F_GETFD, 0);
	  flags |= FD_CLOEXEC;
	  ret = ::fcntl(sockfd, F_SETFD, flags);
	  // FIXME check

	  (void)ret;
	}
	
接下来这个函数只是调用了socket函数返回一个文件描述符，同时也做了错误判断：

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
	
下面这两个函数分别绑定和监听了一个套接字：
	
	void sockets::bindOrDie(int sockfd, const struct sockaddr_in& addr)
	{
	  int ret = ::bind(sockfd, sockaddr_cast(&addr), sizeof addr);
	  if (ret < 0)
	  {
	    LOG_SYSFATAL << "sockets::bindOrDie";
	  }
	}

	void sockets::listenOrDie(int sockfd)
	{
	  int ret = ::listen(sockfd, SOMAXCONN);
	  if (ret < 0)
	  {
	    LOG_SYSFATAL << "sockets::listenOrDie";
	  }
	}

下面这个函数接受一个连接，如果出错则会判断错误类型，如果错误比较普通，则直接退出，否则写入日志：

	int sockets::accept(int sockfd, struct sockaddr_in* addr)
	{
	  socklen_t addrlen = sizeof *addr;
	#if VALGRIND
	  int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
	  setNonBlockAndCloseOnExec(connfd);
	#else
	  int connfd = ::accept4(sockfd, sockaddr_cast(addr),
		                 &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
	#endif
	  if (connfd < 0)
	  {
	    int savedErrno = errno;
	    LOG_SYSERR << "Socket::accept";
	    switch (savedErrno)
	    {
	      case EAGAIN:
	      case ECONNABORTED:
	      case EINTR:
	      case EPROTO: // ???
	      case EPERM:
	      case EMFILE: // per-process lmit of open file desctiptor ???
		// expected errors
		errno = savedErrno;
		break;
	      case EBADF:
	      case EFAULT:
	      case EINVAL:
	      case ENFILE:
	      case ENOBUFS:
	      case ENOMEM:
	      case ENOTSOCK:
	      case EOPNOTSUPP:
		// unexpected errors
		LOG_FATAL << "unexpected error of ::accept " << savedErrno;
		break;
	      default:
		LOG_FATAL << "unknown error of ::accept " << savedErrno;
		break;
	    }
	  }
	  return connfd;
	}
	
下面这些函数都比较简单，都是对socket相关函数的简单封装：

	int sockets::connect(int sockfd, const struct sockaddr_in& addr)
	{
	  return ::connect(sockfd, sockaddr_cast(&addr), sizeof addr);
	}

	ssize_t sockets::read(int sockfd, void *buf, size_t count)
	{
	  return ::read(sockfd, buf, count);
	}

	// readv与read不同之处在于，接收的数据可以填充到多个缓冲区中
	ssize_t sockets::readv(int sockfd, const struct iovec *iov, int iovcnt)
	{
	  return ::readv(sockfd, iov, iovcnt);
	}

	ssize_t sockets::write(int sockfd, const void *buf, size_t count)
	{
	  return ::write(sockfd, buf, count);
	}

	void sockets::close(int sockfd)
	{
	  if (::close(sockfd) < 0)
	  {
	    LOG_SYSERR << "sockets::close";
	  }
	}
	
下面这个函数关闭传入套接字的写端：

	void sockets::shutdownWrite(int sockfd)
	{
	  if (::shutdown(sockfd, SHUT_WR) < 0)
	  {
	    LOG_SYSERR << "sockets::shutdownWrite";
	  }
	}
	
接下来是几个地址转换函数，网络传输地址与本地点分十进制地址转换，具体的封装函数可以参考第一节的补充内容：

	//主要调用toIP函数，将一个sockaddr_in.sin_addr转换为点分十进制地址，放到buf中
	void sockets::toIpPort(char* buf, size_t size,
		               const struct sockaddr_in& addr)
	{
	  char host[INET_ADDRSTRLEN] = "INVALID";
	  toIp(host, sizeof host, addr);
	  uint16_t port = sockets::networkToHost16(addr.sin_port);
	  snprintf(buf, size, "%s:%u", host, port);
	}

	void sockets::toIp(char* buf, size_t size,
		           const struct sockaddr_in& addr)
	{
	  assert(size >= INET_ADDRSTRLEN);
	  ::inet_ntop(AF_INET, &addr.sin_addr, buf, static_cast<socklen_t>(size));
	}

	//与上面两个函数相反，将点分十进制ip转换为一个sockaddr_in.sin_addr
	void sockets::fromIpPort(const char* ip, uint16_t port,
		                   struct sockaddr_in* addr)
	{
	  addr->sin_family = AF_INET;
	  addr->sin_port = hostToNetwork16(port);
	  if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
	  {
	    LOG_SYSERR << "sockets::fromIpPort";
	  }
	}
	
下面这个函数获取错误状况：

	int sockets::getSocketError(int sockfd)
	{
	  int optval;
	  socklen_t optlen = sizeof optval;

	  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
	  {
	    return errno;
	  }
	  else
	  {
	    return optval;
	  }
	}

下面这两个函数分别获取本地地址和连接客户端地址信息：

	struct sockaddr_in sockets::getLocalAddr(int sockfd)
	{
	  struct sockaddr_in localaddr;
	  bzero(&localaddr, sizeof localaddr);
	  socklen_t addrlen = sizeof(localaddr);
	  if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
	  {
	    LOG_SYSERR << "sockets::getLocalAddr";
	  }
	  return localaddr;
	}

	struct sockaddr_in sockets::getPeerAddr(int sockfd)
	{
	  struct sockaddr_in peeraddr;
	  bzero(&peeraddr, sizeof peeraddr);
	  socklen_t addrlen = sizeof(peeraddr);
	  if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
	  {
	    LOG_SYSERR << "sockets::getPeerAddr";
	  }
	  return peeraddr;
	}

最后防止自连接：

	// 自连接是指(sourceIP, sourcePort) = (destIP, destPort)
	// 自连接发生的原因:
	// 客户端在发起connect的时候，没有bind(2)
	// 客户端与服务器端在同一台机器，即sourceIP = destIP，
	// 服务器尚未开启，即服务器还没有在destPort端口上处于监听
	// 就有可能出现自连接，这样，服务器也无法启动了

	bool sockets::isSelfConnect(int sockfd)
	{
	  struct sockaddr_in localaddr = getLocalAddr(sockfd);
	  struct sockaddr_in peeraddr = getPeerAddr(sockfd);
	  return localaddr.sin_port == peeraddr.sin_port
	      && localaddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr;
	}
	
到这里该类就分析完了，还是比较简单的。