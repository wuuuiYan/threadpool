// 死锁的产生原因二：两个线程互相等待对方释放资源，导致程序都无法继续执行
#include <iostream>
#include <thread>
#include <mutex>
#include <unistd.h>

using namespace std;

void thread1(void);
void thread2(void);

// 定义两个互斥锁，分别用于锁定两个共享资源(互斥锁，一定是表示对某种资源的唯一访问权限)
mutex mt1;
mutex mt2;

int a = 100;
int b = 200;

int main(void)
{
	thread t1(thread1);
	thread t2(thread2);

	t1.join();
	t2.join();

	cout << "All threads end." << endl;
}

void thread1(void)
{
	cout << "Thread1 is running: " << endl;
	lock_guard<mutex> guard1(mt1);
	cout << "Thread1: Shared data ---> a = " << a << endl;
	sleep(1);
	lock_guard<mutex> guard2(mt2);
	cout << "Thread2: Shared data ---> b = " << b << endl;

	cout << "Thread1: Get shared data: a + b = " << a + b << endl;
}

void thread2(void)
{
	cout << "Thread2 is running: " << endl;
	lock_guard<mutex> guard2(mt2);
	cout << "Thread2: Shared data ---> b = " << b << endl;
	sleep(1);
	lock_guard<mutex> guard1(mt1);
	cout << "Thread1: Shared data ---> a = " << a << endl;

	cout << "Thread2: Get shared data: a + b = " << a + b << endl;
}
