## muduo源码分析之InetAddress ##

### 一、源码 ###

InetAddress比较简单，主要也是调用了前面SocketsOps里的地址函数，具体内容可以参考src下源码和附录，这里不做分析说明。

### 二、测试 ###

该测试案例是muduo源码下net文件夹中的一个demo，代码如下：

	#include <muduo/net/InetAddress.h>

	//#define BOOST_TEST_MODULE InetAddressTest
	#define BOOST_TEST_MAIN
	#define BOOST_TEST_DYN_LINK
	#include <boost/test/unit_test.hpp>

	using muduo::string;
	using muduo::net::InetAddress;

	BOOST_AUTO_TEST_CASE(testInetAddress)
	{
	  InetAddress addr1(1234);
	  BOOST_CHECK_EQUAL(addr1.toIp(), string("0.0.0.0"));
	  BOOST_CHECK_EQUAL(addr1.toIpPort(), string("0.0.0.0:1234"));

	  InetAddress addr2("1.2.3.4", 8888);
	  BOOST_CHECK_EQUAL(addr2.toIp(), string("1.2.3.4"));
	  BOOST_CHECK_EQUAL(addr2.toIpPort(), string("1.2.3.4:8888"));

	  InetAddress addr3("255.255.255.255", 65535);
	  BOOST_CHECK_EQUAL(addr3.toIp(), string("255.255.255.255"));
	  BOOST_CHECK_EQUAL(addr3.toIpPort(), string("255.255.255.255:65535"));
	}

该案例是个单元测试案例，给出端口和IP给InetAddress类的构造函数，然后获取相关IP和端口信息。

