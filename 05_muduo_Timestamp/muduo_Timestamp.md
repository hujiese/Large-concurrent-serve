## muduo_base库源码分析 -- Timestamp ##

### 一、Timestamp类封装 ###

- <base/types.h>头文件
- less_than_comparable，要求实现<运算符，可自动实现>,<=,>=
- BOOST_STATIC_ASSERT编译时检查错误
- 使用PRId64，实现跨平台
- Timestamp实现及测试

#### 1.Timestamp类图 ####

![](https://i.imgur.com/wIiupg3.png)


#### 2.Timestamp类实现源码 ####

Timestamp.h:

	#ifndef MUDUO_BASE_TIMESTAMP_H
	#define MUDUO_BASE_TIMESTAMP_H
	
	#include <muduo/base/copyable.h>
	#include <muduo/base/Types.h>
	
	#include <boost/operators.hpp>
	
	namespace muduo
	{
	
	///
	/// Time stamp in UTC, in microseconds resolution.
	///
	/// This class is immutable.
	/// It's recommended to pass it by value, since it's passed in register on x64.
	///
	class Timestamp : public muduo::copyable,
	                  public boost::equality_comparable<Timestamp>,
	                  public boost::less_than_comparable<Timestamp>
	{
	 public:
	  ///
	  /// Constucts an invalid Timestamp.
	  ///
	  Timestamp()
	    : microSecondsSinceEpoch_(0)
	  {
	  }
	
	  ///
	  /// Constucts a Timestamp at specific time
	  ///
	  /// @param microSecondsSinceEpoch
	  explicit Timestamp(int64_t microSecondsSinceEpochArg)
	    : microSecondsSinceEpoch_(microSecondsSinceEpochArg)
	  {
	  }
	
	  void swap(Timestamp& that)
	  {
	    std::swap(microSecondsSinceEpoch_, that.microSecondsSinceEpoch_);
	  }
	
	  // default copy/assignment/dtor are Okay
	
	  string toString() const;
	  string toFormattedString(bool showMicroseconds = true) const;
	
	  bool valid() const { return microSecondsSinceEpoch_ > 0; }
	
	  // for internal usage.
	  int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }
	  time_t secondsSinceEpoch() const
	  { return static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond); }
	
	  ///
	  /// Get time of now.
	  ///
	  static Timestamp now();
	  static Timestamp invalid()
	  {
	    return Timestamp();
	  }
	
	  static Timestamp fromUnixTime(time_t t)
	  {
	    return fromUnixTime(t, 0);
	  }
	
	  static Timestamp fromUnixTime(time_t t, int microseconds)
	  {
	    return Timestamp(static_cast<int64_t>(t) * kMicroSecondsPerSecond + microseconds);
	  }
	
	  static const int kMicroSecondsPerSecond = 1000 * 1000;
	
	 private:
	  int64_t microSecondsSinceEpoch_;
	};
	
	inline bool operator<(Timestamp lhs, Timestamp rhs)
	{
	  return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
	}
	
	inline bool operator==(Timestamp lhs, Timestamp rhs)
	{
	  return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
	}
	
	///
	/// Gets time difference of two timestamps, result in seconds.
	///
	/// @param high, low
	/// @return (high-low) in seconds
	/// @c double has 52-bit precision, enough for one-microsecond
	/// resolution for next 100 years.
	inline double timeDifference(Timestamp high, Timestamp low)
	{
	  int64_t diff = high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
	  return static_cast<double>(diff) / Timestamp::kMicroSecondsPerSecond;
	}
	
	///
	/// Add @c seconds to given timestamp.
	///
	/// @return timestamp+seconds as Timestamp
	///
	inline Timestamp addTime(Timestamp timestamp, double seconds)
	{
	  int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
	  return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
	}
	
	}
	#endif  // MUDUO_BASE_TIMESTAMP_H

Timestamp.cc:

	#include <muduo/base/Timestamp.h>
	
	#include <sys/time.h>
	#include <stdio.h>
	
	#ifndef __STDC_FORMAT_MACROS
	#define __STDC_FORMAT_MACROS
	#endif
	
	#include <inttypes.h>
	
	#include <boost/static_assert.hpp>
	
	using namespace muduo;
	
	BOOST_STATIC_ASSERT(sizeof(Timestamp) == sizeof(int64_t));
	
	//以秒的形式输出Timestamp中时间，精确到微秒
	string Timestamp::toString() const
	{
	  char buf[32] = {0};
	  int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
	  int64_t microseconds = microSecondsSinceEpoch_ % kMicroSecondsPerSecond;
	  snprintf(buf, sizeof(buf)-1, "%" PRId64 ".%06" PRId64 "", seconds, microseconds);
	  return buf;
	}
	
	//将当前Timestamp时间对象格式化输出为年月日时分秒格式，如果选择了showMicroseconds，那么将精确到微秒
	string Timestamp::toFormattedString(bool showMicroseconds) const
	{
	  char buf[64] = {0};
	  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
	  struct tm tm_time;
	  gmtime_r(&seconds, &tm_time);
	
	  if (showMicroseconds)
	  {
	    int microseconds = static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
	    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
	             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
	             microseconds);
	  }
	  else
	  {
	    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d",
	             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
	  }
	  return buf;
	}
	
	// 获取当前时间，将当前时间转化为微秒并返回一个Timestamp对象
	Timestamp Timestamp::now()
	{
	  struct timeval tv;
	  gettimeofday(&tv, NULL);
	  int64_t seconds = tv.tv_sec;
	  return Timestamp(seconds * kMicroSecondsPerSecond + tv.tv_usec);
	}

测试：

	#include <muduo/base/Timestamp.h>
	#include <vector>
	#include <stdio.h>
	
	using muduo::Timestamp;
	
	void passByConstReference(const Timestamp& x)
	{
	  printf("%s\n", x.toString().c_str());
	}
	
	void passByValue(Timestamp x)
	{
	  printf("%s\n", x.toString().c_str());
	}
	
	void benchmark()
	{
	  const int kNumber = 1000*1000;
	
	  // 创建一个KNumber大小的stamps
	  std::vector<Timestamp> stamps;
	  stamps.reserve(kNumber);
	  //将kNumber大小个个Timestamp放置到该向量中，时间为当前时间
	  for (int i = 0; i < kNumber; ++i)
	  {
	    stamps.push_back(Timestamp::now());
	  }
	  //打印第一个和最后一个Timestamp对象
	  printf("%s\n", stamps.front().toString().c_str());
	  printf("%s\n", stamps.back().toString().c_str());
	  //计算第一个和最后一个对象时间差
	  printf("%f\n", timeDifference(stamps.back(), stamps.front()));
	
	  //下面这些都是计算连续两个时间差，分时间差小于零，大于零但小于一百，大于一百三种情况，具体内容可见测试的log
	  int increments[100] = { 0 };
	  int64_t start = stamps.front().microSecondsSinceEpoch();
	  for (int i = 1; i < kNumber; ++i)
	  {
	    int64_t next = stamps[i].microSecondsSinceEpoch();
	    int64_t inc = next - start;
	    start = next;
	    if (inc < 0)
	    {
	      printf("reverse!\n");
	    }
	    else if (inc < 100)
	    {
	      ++increments[inc];
	    }
	    else
	    {
	      printf("big gap %d\n", static_cast<int>(inc));
	    }
	  }
	
	  for (int i = 0; i < 100; ++i)
	  {
	    printf("%2d: %d\n", i, increments[i]);
	  }
	}
	
	int main()
	{
	  Timestamp now(Timestamp::now());//构造函数初始化一个Timestamp，时间为当前时间
	  printf("%s\n", now.toString().c_str());//打印当前时间微秒数
	  passByValue(now);//打印当前时间微秒数
	  passByConstReference(now);//打印当前时间微秒数
	  benchmark();
	}

测试结果保存在timestamp.log中。

### 补充：使用PRId64 ###

int64_t用来表示64位整数，在32位系统中是long long int，在64位系统中是long int,所以打印int64_t的格式化方法是：

	printf(“%ld”, value);  // 64bit OS
	printf("%lld", value); // 32bit OS

跨平台的做法：

	#define __STDC_FORMAT_MACROS
	#include <inttypes.h>
	#undef __STDC_FORMAT_MACROS 
	printf("%" PRId64 "\n", value);  
