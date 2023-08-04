// 该文件包含main()函数，因此整个项目将从此文件开始运行并结束

#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
using namespace std;

#include "threadpool.h"

/*
* 有些应用场景，希望能够获取线程执行任务的返回值，该如何做？
* 举例：1 + 2 + ... + 3000000000
*/

using uLong = unsigned long long;

// 甚至可以定义不同的派生类用于执行不同的任务
class MyTask : public Task
{
public:
	MyTask(int begin, int end)
		: begin_(begin)
		, end_(end) {}
	// 如何设计run()函数的返回值，可以表示任意的类型?
	// Java、Python：Object类型是其他所有类类型的基类
	// C++17标准：Any类型，可以接收任意的其他类型
	Any run() // run()方法最终就在线程池分配的线程中执行
	{
		std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));

		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
			sum += i;

		std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;

		return sum;
	}
	// 用户可重写基类的虚函数，实现自定义功能

private:
	int begin_;
	int end_;
};

int main()
{
	{
		ThreadPool pool;
		pool.start(2); //启动线程池

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));

		uLong sum1 = res1.get().cast_<uLong>();
		std::cout << sum1 << std::endl;
	}

	std::wcout << "Main thread is over!" << std::endl;
	getchar();

#if 0
	// ThreadPool对象析构之后，怎样把线程池中线程相关的资源全部回收？
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED); // 用户自定义线程池的工作模式
		pool.start(4); //启动线程池

		// 如何设计这里的Result机制？要求：当线程函数没有执行完毕时，调用成员函数get()会发生阻塞
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		// 随着任务执行完毕，task对象被销毁，依赖于task对象的Result对象也被销毁
		uLong sum1 = res1.get().cast_<uLong>(); // get返回了一个Any类型，怎么转成具体的类型？
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		// Master -> Slave线程模型：1）Master用来分解任务，然后给各个Slaver程分配任务
		// 2）等待各个Slave线程执行完任务，返回结果；3）Master线程合并各个任务结果，输出显示
		std::cout << (sum1 + sum2 + sum3) << std::endl;
	}


	/*uLong sum = 0;
	for (uLong i = 1; i <= 300000000; i++)
		sum += i;
	std::cout << sum << std::endl;*/

	getchar();
#endif
}