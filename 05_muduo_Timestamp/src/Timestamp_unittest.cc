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

