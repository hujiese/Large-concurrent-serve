## muduo 源码分析之socket ##

有了前面两节的基础和socket编程经验，再来分析muduo的socket类就比较容易了。

首先看Socket类的定义：

	#ifndef MUDUO_NET_SOCKET_H
	#define MUDUO_NET_SOCKET_H

	#include <boost/noncopyable.hpp>

	namespace muduo
	{
	///
	/// TCP networking.
	///
	namespace net
	{

	class InetAddress;

	///
	/// Wrapper of socket file descriptor.
	///
	/// It closes the sockfd when desctructs.
	/// It's thread safe, all operations are delagated to OS.
	class Socket : boost::noncopyable
	{
	 public:
	  explicit Socket(int sockfd)
	    : sockfd_(sockfd)
	  { }

	  // Socket(Socket&&) // move constructor in C++11
	  ~Socket();

	  int fd() const { return sockfd_; }

	  /// abort if address in use
	  void bindAddress(const InetAddress& localaddr);
	  /// abort if address in use
	  void listen();

	  /// On success, returns a non-negative integer that is
	  /// a descriptor for the accepted socket, which has been
	  /// set to non-blocking and close-on-exec. *peeraddr is assigned.
	  /// On error, -1 is returned, and *peeraddr is untouched.
	  int accept(InetAddress* peeraddr);

	  void shutdownWrite();

	  ///
	  /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
	  ///
	  // Nagle算法可以一定程度上避免网络拥塞
	  // TCP_NODELAY选项可以禁用Nagle算法
	  // 禁用Nagle算法，可以避免连续发包出现延迟，这对于编写低延迟的网络服务很重要
	  void setTcpNoDelay(bool on);

	  ///
	  /// Enable/disable SO_REUSEADDR
	  ///
	  void setReuseAddr(bool on);

	  ///
	  /// Enable/disable SO_KEEPALIVE
	  ///
	  // TCP keepalive是指定期探测连接是否存在，如果应用层有心跳的话，这个选项不是必需要设置的
	  void setKeepAlive(bool on);

	 private:
	  const int sockfd_;
	};

	}
	}
	#endif  // MUDUO_NET_SOCKET_H
	
构造函数、bind、listen和accept等都有定义，下面将具体分析其实现。

Socket类使用了RAII编程方法，在构造函数中创建资源，析构函数中释放资源：

	 explicit Socket(int sockfd)
	    : sockfd_(sockfd)
	  { }
	  
	  Socket::~Socket()
	{
	  sockets::close(sockfd_);
	}

结下来这三个函数：

	void Socket::bindAddress(const InetAddress& addr)
	{
	  sockets::bindOrDie(sockfd_, addr.getSockAddrInet());
	}

	void Socket::listen()
	{
	  sockets::listenOrDie(sockfd_);
	}

	void Socket::shutdownWrite()
	{
	  sockets::shutdownWrite(sockfd_);
	}

上面这几个函数都是直接调用了SocketsOps中封装的socket操作函数。

下面这个函数选项禁用Nagle算法：

	void Socket::setTcpNoDelay(bool on)
	{
	  int optval = on ? 1 : 0;
	  ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY,
		       &optval, sizeof optval);
	  // FIXME CHECK
	}

接下来这个函数设置地址复用：

	void Socket::setReuseAddr(bool on)
	{
	  int optval = on ? 1 : 0;
	  ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,
		       &optval, sizeof optval);
	  // FIXME CHECK
	}
	
接下来这个函数设置周期性测试连接是否依然存活：

	void Socket::setKeepAlive(bool on)
	{
	  int optval = on ? 1 : 0;
	  ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE,
		       &optval, sizeof optval);
	  // FIXME CHECK
	}

到这里，Socket类就分析完了。