// async()函数的使用：使用函数创建异步任务
// 子线程的线程函数的返回值：全局变量 + 互斥锁(同步) 或 future + async(异步)
// synchronization：同步，asynchronization：异步
#include <iostream>
#include <future> //async()函数所在头文件，future是类模板
#include <unistd.h>

using namespace std;

bool is_prime(int x);

int main(void)
{
	future<bool> fut = async(is_prime, 313222313); //赋给一个模板类的对象
	// 创建任务对象，is_prime是任务函数，313222313是任务函数的参数
	cout << "Checking whether 313222313 is prime." << endl; //并没有等待异步任务运行结束就打印了这条语句

	bool ret = fut.get(); //获取任务函数的返回值，若异步任务函数未完成，则阻塞等待

	if(ret)
		cout << "It is prime" << endl;
	else
		cout << "It is not prime" << endl;
}

bool is_prime(int x)
{
	cout << "Calculating. Please wait....." << endl;
	sleep(5);

	for(int i = 2; i < x; i++)
	{
		if(x % i == 0)
			return false;
	}
	return true;
}
