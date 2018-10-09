## muduo源码分析--Exception异常 ##

### 一、Exception类实现 ###

- backtrace，栈回溯，保存各个栈帧的地址
- backtrace_symbols，根据地址，转成相应的函数符号
- abi::__cxa_demangle

#### 1、类图 ####

![](https://i.imgur.com/mAxS1nj.png)

#### 2、源码实现 ####

Exception.h:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_EXCEPTION_H
	#define MUDUO_BASE_EXCEPTION_H
	
	#include <muduo/base/Types.h>
	#include <exception>
	
	namespace muduo
	{
	
	class Exception : public std::exception
	{
	 public:
	  explicit Exception(const char* what);
	  explicit Exception(const string& what);
	  virtual ~Exception() throw();
	  virtual const char* what() const throw();
	  const char* stackTrace() const throw();
	
	 private:
	  void fillStackTrace();
	
	  string message_;
	  string stack_;
	};
	
	}
	
	#endif  // MUDUO_BASE_EXCEPTION_H

Exception.cc:

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#include <muduo/base/Exception.h>
	
	//#include <cxxabi.h>
	#include <execinfo.h>
	#include <stdlib.h>
	
	using namespace muduo;
	
	Exception::Exception(const char* msg)
	  : message_(msg)
	{
	  fillStackTrace();
	}
	
	Exception::Exception(const string& msg)
	  : message_(msg)
	{
	  fillStackTrace();
	}
	
	Exception::~Exception() throw ()
	{
	}
	
	const char* Exception::what() const throw()
	{
	  return message_.c_str();
	}
	
	const char* Exception::stackTrace() const throw()
	{
	  return stack_.c_str();
	}
	
	void Exception::fillStackTrace()
	{
	  const int len = 200;
	  void* buffer[len];
	  int nptrs = ::backtrace(buffer, len);
	  char** strings = ::backtrace_symbols(buffer, nptrs);
	  if (strings)
	  {
	    for (int i = 0; i < nptrs; ++i)
	    {
	      // TODO demangle funcion name with abi::__cxa_demangle
	      stack_.append(strings[i]);
	      stack_.push_back('\n');
	    }
	    free(strings);
	  }
	}

从上面源码可以看到，私有成员变量message_用来保存一条普通异常信息，stack_用来保存整个异常错误的回溯栈。打印异常信息的what和stackTrace函数都调用了函数fillStackTrace函数，该函数如下：

	void Exception::fillStackTrace()
	{
	  const int len = 200;
	  void* buffer[len];
	  int nptrs = ::backtrace(buffer, len);
	  char** strings = ::backtrace_symbols(buffer, nptrs);
	  if (strings)
	  {
	    for (int i = 0; i < nptrs; ++i)
	    {
	      // TODO demangle funcion name with abi::__cxa_demangle
	      stack_.append(strings[i]);
	      stack_.push_back('\n');
	    }
	    free(strings);
	  }
	}

比较关键的部位就是两个函数backtrace和backtrace_symbols，下面将介绍这两个函数的用法。

在头文件"execinfo.h"中声明了三个函数用于获取当前线程的函数调用堆栈：

	#include <execinfo.h>
	
	 int backtrace(void **buffer, int size);
	
	 char **backtrace_symbols(void *const *buffer, int size);
	
	 void backtrace_symbols_fd(void *const *buffer, int size, int fd);

下面将具体介绍用法：

	int backtrace(void **buffer,int size)

该函数用与获取当前线程的调用堆栈,获取的信息将会被存放在buffer中,它是一个指针数组。参数 size 用来指定buffer中可以保存多少个void* 元素。函数返回值是实际获取的指针个数,最大不超过size大小在buffer中的指针实际是从堆栈中获取的返回地址,每一个堆栈框架有一个返回地址。

注意某些编译器的优化选项对获取正确的调用堆栈有干扰,另外内联函数没有堆栈框架;删除框架指针也会使无法正确解析堆栈内容。

	char ** backtrace_symbols (void *const *buffer, int size)

backtrace_symbols将从backtrace函数获取的信息转化为一个字符串数组. 参数buffer应该是从backtrace函数获取的数组指针,size是该数组中的元素个数(backtrace的返回值)，函数返回值是一个指向字符串数组的指针,它的大小同buffer相同.每个字符串包含了一个相对于buffer中对应元素的可打印信息.它包括函数名，函数的偏移地址,和实际的返回地址

现在,只有使用ELF二进制格式的程序和苦衷才能获取函数名称和偏移地址.在其他系统,只有16进制的返回地址能被获取.另外,你可能需要传递相应的标志给链接器,以能支持函数名功能(比如,在使用GNU ld的系统中,你需要传递(-rdynamic))

backtrace_symbols生成的字符串都是malloc出来的，但是不要最后一个一个的free，因为backtrace_symbols是根据backtrace给出的call stack层数，一次性的malloc出来一块内存来存放结果字符串的，所以，像上面代码一样，只需要在最后，free backtrace_symbols的返回指针就OK了。这一点backtrace的manual中也是特别提到的。

注意:如果不能为字符串获取足够的空间函数的返回值将会为NULL。

    void backtrace_symbols_fd (void *const *buffer, int size, int fd)

backtrace_symbols_fd与backtrace_symbols 函数具有相同的功能,不同的是它不会给调用者返回字符串数组,而是将结果写入文件描述符为fd的文件中,每个函数对应一行.它不需要调用malloc函数,因此适用于有可能调用该函数会失败的情况。

下面这段代码是一个使用实例：

	#include <execinfo.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <unistd.h>
	
	void
	myfunc3(void)
	{
	   int j, nptrs;
	#define SIZE 100
	   void *buffer[100];
	   char **strings;
	
	   nptrs = backtrace(buffer, SIZE);
	   printf("backtrace() returned %d addresses\n", nptrs);
	
	   /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
	      would produce similar output to the following: */
	
	   strings = backtrace_symbols(buffer, nptrs);
	   if (strings == NULL) {
	       perror("backtrace_symbols");
	       exit(EXIT_FAILURE);
	   }
	
	   for (j = 0; j < nptrs; j++)
	       printf("%s\n", strings[j]);
	
	   free(strings);
	}
	
	static void   /* "static" means don't export the symbol... */
	myfunc2(void)
	{
	   myfunc3();
	}
	
	void
	myfunc(int ncalls)
	{
	   if (ncalls > 1)
	       myfunc(ncalls - 1);
	   else
	       myfunc2();
	}
	
	int
	main(int argc, char *argv[])
	{
	   if (argc != 2) {
	       fprintf(stderr, "%s num-calls\n", argv[0]);
	       exit(EXIT_FAILURE);
	   }
	
	   myfunc(atoi(argv[1]));
	   exit(EXIT_SUCCESS);
	}

所以，fillStackTrace函数将异常栈的所有信息都保存在stack_中。

### 二、测试 ###

测试代码如下：

	#include <muduo/base/Exception.h>
	#include <stdio.h>
	
	class Bar
	{
	 public:
	  void test()
	  {
	    throw muduo::Exception("oops");
	  }
	};
	
	void foo()
	{
	  Bar b;
	  b.test();
	}
	
	int main()
	{
	  try
	  {
	    foo();
	  }
	  catch (const muduo::Exception& ex)
	  {
	    printf("reason: %s\n", ex.what());
	    printf("stack trace: %s\n", ex.stackTrace());
	  }
	}

测试结果如下：

![](https://i.imgur.com/xUwLjSs.png)

打印异常原因之后打印异常栈，然后一直追踪到 ./echo(_ZN3Bar4testEv+0x29) [0x4013a7]。

### 三、__cxa_demangle打印具体函数信息 ###

上面测试可以总结出：

- 1. backtrace可以在程序运行的任何地方被调用，返回各个调用函数的返回地址，可以限制最大调用栈返回层数。
- 2. 在backtrace拿到函数返回地址之后，backtrace_symbols可以将其转换为编译符号，这些符号是编译期间就确定的

但是从测试的结果看，打印的异常栈内容都是给编译器看的，里面都有编译符号，但是根据backtrace_symbols返回的编译符号，abi::__cxa_demangle可以找到具体地函数方法：

	char* abi::__cxa_demangle(const char * mangled_name, char* output_buffer, size_t* length, int* status)

下面这段代码将可以将带编译符号的信息转换出我们需要的函数信息：

	string Exception::demangle(const char* symbol)
	{
	  size_t size;
	  int status;
	  char temp[128];
	  char* demangled;
	  //first, try to demangle a c++ name
	  if (1 == sscanf(symbol, "%*[^(]%*[^_]%127[^)+]", temp)) {
	    if (NULL != (demangled = abi::__cxa_demangle(temp, NULL, &size, &status))) {
	      string result(demangled);
	      free(demangled);
	      return result;
	    }
	  }
	  //if that didn't work, try to get a regular c symbol
	  if (1 == sscanf(symbol, "%127s", temp)) {
	    return temp;
	  }
	 
	  //if all else fails, just return the symbol
	  return symbol;
	}

这就需要修改源码了，修改后的代码如下:

Exception.h：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#ifndef MUDUO_BASE_EXCEPTION_H
	#define MUDUO_BASE_EXCEPTION_H
	
	#include <muduo/base/Types.h>
	#include <exception>
	
	namespace muduo
	{
	
	class Exception : public std::exception
	{
	 public:
	  explicit Exception(const char* what);
	  explicit Exception(const string& what);
	  virtual ~Exception() throw();
	  virtual const char* what() const throw();
	  const char* stackTrace() const throw();
	
	 private:
	  void fillStackTrace();
	  string demangle(const char* symbol);
	
	  string message_;
	  string stack_;
	};
	
	}
	
	#endif  // MUDUO_BASE_EXCEPTION_H

Exception.cc：

	// Use of this source code is governed by a BSD-style license
	// that can be found in the License file.
	//
	// Author: Shuo Chen (chenshuo at chenshuo dot com)
	
	#include <muduo/base/Exception.h>
	
	#include <cxxabi.h>
	#include <execinfo.h>
	#include <stdlib.h>
	#include <stdio.h>
	
	using namespace muduo;
	
	Exception::Exception(const char* msg)
	  : message_(msg)
	{
	  fillStackTrace();
	}
	
	Exception::Exception(const string& msg)
	  : message_(msg)
	{
	  fillStackTrace();
	}
	
	Exception::~Exception() throw ()
	{
	}
	
	const char* Exception::what() const throw()
	{
	  return message_.c_str();
	}
	
	const char* Exception::stackTrace() const throw()
	{
	  return stack_.c_str();
	}
	
	void Exception::fillStackTrace()
	{
	  const int len = 200;
	  void* buffer[len];
	  int nptrs = ::backtrace(buffer, len);
	  char** strings = ::backtrace_symbols(buffer, nptrs);
	  if (strings)
	  {
	    for (int i = 0; i < nptrs; ++i)
	    {
	      // TODO demangle funcion name with abi::__cxa_demangle
	      //stack_.append(strings[i]);
		  stack_.append(demangle(strings[i]));
	      stack_.push_back('\n');
	    }
	    free(strings);
	  }
	}
	
	string Exception::demangle(const char* symbol)
	{
	  size_t size;
	  int status;
	  char temp[128];
	  char* demangled;
	  //first, try to demangle a c++ name
	  if (1 == sscanf(symbol, "%*[^(]%*[^_]%127[^)+]", temp)) {
	    if (NULL != (demangled = abi::__cxa_demangle(temp, NULL, &size, &status))) {
	      string result(demangled);
	      free(demangled);
	      return result;
	    }
	  }
	  //if that didn't work, try to get a regular c symbol
	  if (1 == sscanf(symbol, "%127s", temp)) {
	    return temp;
	  }
	 
	  //if all else fails, just return the symbol
	  return symbol;
	}

测试程序不变，运行后结果如下：

![](https://i.imgur.com/LNachch.png)

可见，现在的打印结果去掉了符号信息，看起来清楚多了。