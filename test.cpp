// ���ļ�����main()���������������Ŀ���Ӵ��ļ���ʼ���в�����

#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
using namespace std;

#include "threadpool.h"

/*
* ��ЩӦ�ó�����ϣ���ܹ���ȡ�߳�ִ������ķ���ֵ�����������
* ������1 + 2 + ... + 3000000000
*/

using uLong = unsigned long long;

// �������Զ��岻ͬ������������ִ�в�ͬ������
class MyTask : public Task
{
public:
	MyTask(int begin, int end)
		: begin_(begin)
		, end_(end) {}
	// ������run()�����ķ���ֵ�����Ա�ʾ���������?
	// Java��Python��Object�������������������͵Ļ���
	// C++17��׼��Any���ͣ����Խ����������������
	Any run() // run()�������վ����̳߳ط�����߳���ִ��
	{
		std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));

		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
			sum += i;

		std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;

		return sum;
	}
	// �û�����д������麯����ʵ���Զ��幦��

private:
	int begin_;
	int end_;
};

int main()
{
	{
		ThreadPool pool;
		pool.start(2); //�����̳߳�

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
	// ThreadPool��������֮���������̳߳����߳���ص���Դȫ�����գ�
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED); // �û��Զ����̳߳صĹ���ģʽ
		pool.start(4); //�����̳߳�

		// �����������Result���ƣ�Ҫ�󣺵��̺߳���û��ִ�����ʱ�����ó�Ա����get()�ᷢ������
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		// ��������ִ����ϣ�task�������٣�������task�����Result����Ҳ������
		uLong sum1 = res1.get().cast_<uLong>(); // get������һ��Any���ͣ���ôת�ɾ�������ͣ�
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		// Master -> Slave�߳�ģ�ͣ�1��Master�����ֽ�����Ȼ�������Slaver�̷�������
		// 2���ȴ�����Slave�߳�ִ�������񣬷��ؽ����3��Master�̺߳ϲ������������������ʾ
		std::cout << (sum1 + sum2 + sum3) << std::endl;
	}


	/*uLong sum = 0;
	for (uLong i = 1; i <= 300000000; i++)
		sum += i;
	std::cout << sum << std::endl;*/

	getchar();
#endif
}