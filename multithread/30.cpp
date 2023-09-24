// packaged_task <---> future
#include <iostream>
#include <future>

using namespace std;

int add_func(int x, int y);

int main(void)
{
	cout << "main() thread id = " << this_thread::get_id() << endl;

	packaged_task<int(int, int)> ptk(add_func); //封装了可调用对象的类型，然后在实例化模板类的对象时传入具体的可调用对象
	future<int> future;
	future = ptk.get_future(); //绑定关系
	thread t(move(ptk), 10, 20); //移动语义，转移对象的所有权
	int result = future.get();
	cout << "Add result = " << result << endl;
	t.join();

	return 0;
}

int add_func(int x, int y)
{
	cout << "child thread id = " << this_thread::get_id() << endl; //调用this_thread命名空间中的标准库函数get_id()获取当前线程的id
	cout << "x = " << x << ", y = " << y << endl;
	return x + y;
}
