#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

using namespace std;

void my_thread(void);

// int count = 0;
mutex my_mutex;
atomic<int> count(0); //模板类，原子类型的变量，可以保证变量的操作是原子的

int main(void)
{
	thread t1(my_thread);
	thread t2(my_thread);

	auto start = chrono::high_resolution_clock::now();

	t1.join();
	t2.join();

	auto stop = chrono::high_resolution_clock::now();
	auto durations = chrono::duration<double, ratio<1, 1>>(stop - start).count();

	cout << "count = " << count << ", it takes " << durations << " seconds." << endl;

	return 0;
}

void my_thread(void)
{
	for(int j = 0; j < 5; j++)
	{
		for(int i = 0; i < 10000000; i++)
		{
			// my_mutex.lock();
			count++; //一条语句转换为机器码时不一定是一条指令，可能是多条指令
			// 多条指令执行时可能会被打断，造成数据不一致
			// 互斥锁可以解决这个问题，但是效率低，大量的时间浪费在加锁和解锁上
			// 因此最好的方法是定义原子类型的变量，“原子”：不可再细分的最小单位
			// 互斥锁作用在代码块上，原子类型的变量作用在变量上，这是两者的本质区别，根据使用场景选择
			// my_mutex.unlock();
		}
	}
}
