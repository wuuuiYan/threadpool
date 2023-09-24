// 在多线程中使用互斥(互斥锁)
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <unistd.h>

using namespace std;

vector<int> ivec; //待写入数据的容器
mutex guard_mutex; //定义互斥锁，用于线程间的互斥访问
// 多个线程都想对临界区代码段进行操作，但是每次只允许一个线程进入临界区
// 保证临界区代码段的竟态条件成立，即保证临界区代码段的原子性
// 对访问的数据进行标记，当某个线程正在访问时，其他线程不允许访问

void write_to_vector(int id, int value);

int main(void)
{
	thread t1(write_to_vector, 1, 100);
	thread t2(write_to_vector, 2, 200);
	thread t3(write_to_vector, 3, 300);
	thread t4(write_to_vector, 4, 400);
	thread t5(write_to_vector, 5, 500);

	t1.join();
	t2.join();
	t3.join();
	t4.join();
	t5.join();

	for (auto &e : ivec)
		cout << e << " ";
	cout << endl;
	// 通过打印vector的结果，可以说明上述五个线程是并发执行的，彼此之间没有确定的顺序

	return 0;
}

void write_to_vector(int id, int value)
{
	cout << "Thread " << id << " is running!" << endl;

	// lock_guard是个模板类，用于管理互斥锁，构造时自动加锁(lock)，析构时自动解锁(unlock)，即RAII
	lock_guard<mutex> guard(guard_mutex); //用互斥锁初始化lock_guard对象
	cout << "Thread " << id << " lock to the resource!" << endl;
	ivec.push_back(value);
	sleep(2);
	// 当线程函数结束时，lock_guard类的对象guard析构，自动释放互斥锁
}

// class lock_guard
// {
// public:
//     explicit lock_guard(std::mutex& mtx) : mtx_(mtx){ 
//         mtx_.lock(); 
//     }
//     ~lock_guard() { mtx_.unlock(); }
//     // 禁止拷贝构造和拷贝赋值，因为该类中存在不允许拷贝的成员变量
//     lock_guard(const lock_guard &) = delete;
//     lock_guard &operator=(const lock_guard &) = delete;
// private:
//     std::mutex&  mtx_;
// };
