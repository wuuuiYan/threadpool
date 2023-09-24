#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <thread>

using namespace std;

timed_mutex tmx; //限定等待时间的互斥锁类

void fun1(int id, const string &str)
{
	if(tmx.try_lock()) //尝试上锁，如果成功返回true，否则返回false
	{
		cout << "Thread " << id << "locked and do something" << endl;
		// do something
		for(int i = 0; i < 10; i++)
		{
			cout << str << endl;
			this_thread::sleep_for(chrono::seconds(1));
		}
		tmx.unlock();
	}
}

void fun2(int id, const string &str)
{
	this_thread::sleep_for(chrono::seconds(1)); //先让出互斥锁

	if(tmx.try_lock_for(chrono::seconds(5))) //等待5秒之内尝试加锁，如果成功返回true，否则返回false
	{
		cout << "Thread " << id << ": " << str << endl;
		tmx.unlock();
	}
	else
		cout << "Can not wait any more" << endl;
}

int main(void)
{
	thread t1(fun1, 1, "Hello world");
	thread t2(fun2, 2, "Good morning");

	t1.join();
	t2.join();

	return 0;
}
