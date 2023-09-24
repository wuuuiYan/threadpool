// 条件变量condition_variable的成员函数wait的第二种用法：传入可调用对象
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>

using namespace std;

mutex mtx;
condition_variable flag;
int count = -1;

void signal_(void);
void wait(int id);

int main(void)
{
	thread t1(signal_);
	thread t2(wait, 2);
	thread t3(wait, 3);

	t1.join();
	t2.join();
	t3.join();

	return 0;
}

void signal_(void)
{
	sleep(5); //由于这个延时，两个wait线程一定会提前加锁，然后到wait阻塞
	unique_lock<mutex> lck(mtx);
	cout << "notify all threads" << endl;
	lck.unlock(); //必须在其他线程被唤醒之前手动解锁互斥量
	flag.notify_all();
	// 上述的操作只能让另外两个线程收到信号，但lambda表达式仍然为false

	// 下面的操作不仅让另外两个线程收到信号，并且让lambda表达式变为true
	sleep(5);
	lck.lock(); //重新上锁互斥量，因为下面要更改共享数据
	count = 1;
	cout << "notify all threads again" << endl;
	lck.unlock();
	flag.notify_all();
}

void wait(int id)
{
	unique_lock<mutex> lck(mtx);
	cout << "Thread " << id << " waits for count: " << endl;
	// 这里有一个问题：为什么在wait函数中的lambda表达式返回值为false时，子线程t2、t3可以同时打印上面这条语句
	// 按照之前的理解，只能有一个线程打印这条语句，另外一个线程由于互斥量mtx被加锁不会运行
	// 奥理解了，当运行下面的wait语句时，wait会暂时解锁互斥量mtx，这时另一个子线程可以对其加锁
	// 最终两个线程都进入等待状态
	flag.wait(lck, [](){return count == 1;});
	// 成员函数wait的第二个参数是一个lambda表达式，用于判断条件是否满足
	// 一旦进入阻塞状态就暂时解锁mtx
	// 当条件变量收到信号 && lambda表达式返回true时，线程变为阻塞状态；当mutex被解锁后，线程变为就绪状态
	// lambda表达式返回值为false时，即使收到了信号，wait也不会通过阻塞
	// 当lambda表达式返回true时，即使没有收到信号，wait也会通过阻塞，但此时不会暂时解锁互斥锁mtx，signal线程也就没有存在的意义了
	// 即此时子线程t2、t3某一时刻只能有一个线程在执行，另外一个线程由于互斥量mtx被加锁不会运行，卡在了第一步实例化对象lck处
	cout << "Thread " << id << ": " << count << endl;
	// sleep(1);
}
