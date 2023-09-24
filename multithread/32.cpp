// 限时等待
#include <iostream>
#include <future>

using namespace std;

void thread1(promise<int> &my_promise, int n);
void thread2(future<int> &fut);

int main(void)
{
	promise<int> pm;
	future<int> future;
	future = pm.get_future();

	thread t1(thread1, ref(pm), 5);
	thread t2(thread2, ref(future));

	t1.join();
	t2.join();

	return 0;
}

void thread1(promise<int> &my_promise, int n)
{
	cout << "thread1 input value: " << n << endl;
	n *= 100;
	this_thread::sleep_for(chrono::seconds(10)); //最里面的括号相当于使用了匿名类对象
	my_promise.set_value(n);
}

void thread2(future<int> &fut)
{
//	chrono::milliseconds(10000); //这种写法也是对的
	chrono::milliseconds span(20000); //创建类的对象，指定时间段为20000毫秒

	if(fut.wait_for(span) == future_status::timeout)
	{
		cout << "Time Out" << endl;
		cout << "Thread2 cannot wait any more, quit!" << endl;
		return;
	}

	cout << "thread2 get value: " << fut.get() << endl;
}
