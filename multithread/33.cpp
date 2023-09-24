// 计算程序的执行时间，检测程序优化的方法
#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

void do_something(void)
{
	this_thread::sleep_for(chrono::duration<int, ratio<1, 1>>(2)); 
	// 使用了duration模板类的匿名对象，等待两个计时单元，一个计时单元为1秒
	// chrono::duration<int, ration<60, 1>> int类型个计时单元，一个计时单元为60秒
	// chrono::duration<double, <1, 1000>>  double类型个计时单元，一个计时单元为1/1000秒
	cout << "do something" << endl;
}

int main(void)
{
	// high_resolution_clock：最高精度的时钟，用于测量短时间间隔
	// 返回的是周期数，而不是秒数，因此最后还需要转化为熟悉的时间表现形式
	auto start = chrono::high_resolution_clock::now(); //获取开始时间
	do_something();
	auto stop = chrono::high_resolution_clock::now(); //获取结束时间

	auto durations = chrono::duration<double, ratio<1, 1>>(stop - start).count();

	cout << "do_something takes " << durations << " seconds." << endl;

	return 0;
}
