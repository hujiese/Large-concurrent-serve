#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

pthread_key_t key; // 好吧，这个玩意究竟是干什么的呢？

struct test_struct { // 用于测试的结构
    int i;
    float k;
};

void *child1(void *arg)
{
    struct test_struct struct_data; // 首先构建一个新的结构
    struct_data.i = 10;
    struct_data.k = 3.1415;
    pthread_setspecific(key, &struct_data); // 设置对应的东西吗？
    printf("child1--address of struct_data is --> 0x%p\n", &(struct_data));
    printf("child1--from pthread_getspecific(key) get the pointer and it points to --> 0x%p\n", (struct test_struct *)pthread_getspecific(key));
    printf("child1--from pthread_getspecific(key) get the pointer and print it's content:\nstruct_data.i:%d\nstruct_data.k: %f\n", 
        ((struct test_struct *)pthread_getspecific(key))->i, ((struct test_struct *)pthread_getspecific(key))->k);
    printf("------------------------------------------------------\n");
}
void *child2(void *arg)
{
    int temp = 20;
    sleep(2);
    printf("child2--temp's address is 0x%p\n", &temp);
    pthread_setspecific(key, &temp); // 好吧，原来这个函数这么简单
    printf("child2--from pthread_getspecific(key) get the pointer and it points to --> 0x%p\n", (int *)pthread_getspecific(key));
    printf("child2--from pthread_getspecific(key) get the pointer and print it's content --> temp:%d\n", *((int *)pthread_getspecific(key)));
}
int main(void)
{
    pthread_t tid1, tid2;
    pthread_key_create(&key, NULL); // 这里是构建一个pthread_key_t类型，确实是相当于一个key
    pthread_create(&tid1, NULL, child1, NULL);
    pthread_create(&tid2, NULL, child2, NULL);
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    pthread_key_delete(key);
    return (0);
}