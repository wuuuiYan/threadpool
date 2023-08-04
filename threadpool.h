#ifndef THREADPOOL_H //防卫式声明，如果没有定义，执行下面的定义
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>

// Any类型：表示可以接收任意数据的类型
// 模板类型的代码必须放在头文件中，才能在编译阶段实例化
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete; // 拷贝构造函数
	Any& operator=(const Any&) = delete; // 拷贝赋值运算符
	// 因为成员变量智能指针类型unique_ptr禁止左值引用和拷贝构造，所以该类也禁止拷贝构造和拷贝初始化
	Any(Any&&) = default; // 移动构造函数
	Any& operator=(Any&&) = default; // 移动赋值运算符

	// 只接受一个参数的构造函数是转换构造函数
	// 转换构造函数使用函数模板，可以接收任意类型而构造Any类对象
	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}
	// 基类指针指向派生类的对象

	// 这个方法能够把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 从成员变量base_找到指向的Derive对象，从该对象中再取出它的成员变量data_
		// 基类指针 -> 派生类指针(RTTI)：当基类的指针确实指向派生类的对象时才可以转换成功，转换失败时返回nullptr
		Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get()); // 成员函数get()获取智能指针所指向对象的裸指针
		if (pd == nullptr)
		{
			// 类型不匹配会转换失败
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default; // 基类的析构函数是虚函数
	};

	// 派生类类型，同时使用了类模板
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) {}
		T data_; // 使用类模板实现任意类型
	};
	// 实现类的嵌套

private:
	// 委托设计思想：定义基类的指针，可以指向派生类的对象
	std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore
{
public:
	// 构造函数
	Semaphore(int limit = 0) 
		: resLimit_(limit)
		, isExit_(false)
		{} // 可以在初始化时指定资源计数
	// 析构函数
	~Semaphore()
	{
		isExit_ = true;
	}

	// 获取一个信号量资源
	void wait()
	{
		if (isExit_) // 资源已经销毁，不要继续等待
			return;
		std::unique_lock<std::mutex> lock(mtx_);
		// 等待信号量有资源，没有资源的话会阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// 增加一个信号量资源
	void post()
	{
		if (isExit_) // 资源已经销毁，不要继续等待
			return;
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// 在Linux系统上，condition_variable的析构函数什么也没做
		// 导致这里状态已经失效，无故阻塞
		cond_.notify_all();
	}
	// 可以在不同线程中获取资源或增加资源，这是与互斥锁的最本质区别
private:
	std::atomic_bool isExit_;
	int resLimit_; // 资源计数，因为信号量可看作是资源计数无限制的互斥锁
	std::mutex mtx_;
	std::condition_variable cond_;
	// 互斥锁与条件变量都是类类型，注意不是模板类
};

// Task类型的前置声明
class Task;

// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
// 用户线程提交任务并且接收返回值，具体消费任务时由其他线程执行，所以需要线程间的通信
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// 问题一：成员函数setVal()：获取任务执行完的返回值
	void setVal(Any any);
	
	// 问题二：成员函数get()：用户调用该方法获取task的返回值(Any类型的对象)
	Any get(); // 当用户调用到这个成员函数，但是线程任务还没有执行完毕时需要阻塞用户线程，所以需要用到信号量

private:
	Any any_; // 存储任务的返回值
	Semaphore sem_; // 线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; // 表示任务提交是否成功，只有当任务提交成功时才可以接收返回值
};

// 任务抽象基类，含有纯虚函数的类是抽象基类
// 不能定义抽象基类的对象，但是可以定义抽象基类类型的指针(任务队列)，进而指向派生类的对象
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);

	// 用户可以自定义任务类型，从Task继承而来，重写run()方法，实现自定义任务处理
	virtual Any run() = 0;

private:
	Result* result_; // Result对象的声明周期是要长于Task对象的
	// 委托设计思想：这里必须使用裸指针，不能使用智能指针，否则会发生交叉/循环引用问题
};

// 线程池支持的两种模式
enum class PoolMode
{
	MODE_FIXED,  // 线程数量固定
	MODE_CACHED, // 线程数量可动态增长，但也不会超过设定阈值
};

// 线程类型
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>; // 使用类型别名定义线程函数对象类型：返回类型为void，形参列表为int

	// 线程构造：接受线程函数对象，用于定义实例化的线程所执行的具体操作
	Thread(ThreadFunc func);
	// 线程析构
	~Thread();
	
	// 启动线程
	void start();

	// 为每个线程手动分配ID
	int getId() const;

private:
	ThreadFunc func_; // 线程函数对象
	static int generateId_; // 使用静态变量，不依附于任何一个对象
	int threadId_; // 保存当前线程id
};

/*
* example:
*	ThreadPool pool
*	pool.start();
*	
*	class MyTask : public Task
*	{
*		public:
*			void run() { // 线程代码...}
*	}；
*	
*	pool.submitTask(std::make_shared<MyTask>());
* 
*/

// 线程池类型
class ThreadPool
{
public:
	// 线程池构造
	ThreadPool();
	// 线程池析构
	~ThreadPool();

	// 设置线程池的工作模式
	void setMode(PoolMode mode);

	// 设置任务队列中能存储的最大任务数量
	void setTaskQueMaxThresHold(int threshold);

	// 设置cached模式下线程容器中能存储的最大线程数量
	void setThreadSizeThresHold(int threshold);
	// 只有在线程池尚未启动时，才能设置上述三个状态参数，所以三个函数中都需要先进行判断

	// 向线程池提交任务(生产者调用)
	Result submitTask(std::shared_ptr<Task> sp);

	// 启动线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());

	// 禁止线程池拷贝构造或拷贝赋值操作
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 线程函数，为了能够访问互斥锁与条件变量，该函数定义在线程池类中
	void threadFunc(int threadId);

	// 检查线程池的运行状态
	bool checkRunningState() const;

private:
	// 官方库中类的成员变量都是加前下划线，成员函数名都是用下划线连接的全小写英文
	// 为了进行区别，自己DIY的库成员变量使用后下划线，成员函数名使用小驼峰命名法

	// std::vector<std::unique_ptr<Thread>> threads_; // 存放线程的容器，无需考虑线程安全问题
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 存放线程对象的容器，同时建立了线程对象与线程ID之间的关联
	size_t initThreadSize_; // 初始线程数量，Fixed模式使用
	std::atomic_int curThreadSize_; // 当前线程容器中的线程数量
	// threads_.size()也能表示当前线程容器中的线程数量，但因为vector不是线程安全的，所以考虑另外设置一个原子类型的变量
	int threadSizeThresHold_; // 线程容器中能存储的最大线程数量，因为不会修改，所以不限定原子操作。原因是线程不能无限创建
	std::atomic_int idleThreadSize_; // 当前空闲线程的数量

	std::queue<std::shared_ptr<Task>> taskQue_; // 存放任务的队列，需要考虑线程安全问题
	// Q：为什么这里不存放裸指针，而存放了智能指针？
	// A：因为在某些调用情况下可能传入的是一个匿名的Task对象，在传入操作语句结束后匿名对象将被销毁，此时如果使用裸指针，将会出现内存泄漏
	std::atomic_int taskSize_; // 任务数量，因为用户生产任务、线程消费任务都会修改此变量，所以必须保证原子操作
	// taskQue_.size()也能表示当前任务队列中的任务数量，但因为queue不是线程安全的，所以考虑另外设置一个原子类型的变量
	int taskQueMaxThresHold_; // 任务队列中的最大任务数量，因为不会修改，所以不限定原子操作

	std::mutex taskQueMtx_; // 代表任务队列唯一使用权的互斥锁，保证任务队列的线程安全
	std::condition_variable notFull_; // 代表任务队列不满的条件变量，用户可以向其中提交任务等待执行(生产者)
	std::condition_variable notEmpty_; // 代表任务队列不空的条件变量，线程可以从其中提取任务执行(消费者)
	std::condition_variable exitCond_; // 等待线程资源全部回收

	PoolMode poolMode_; // 当前线程池的工作模式，使用枚举类型定义
	std::atomic_bool isPoolRunning_; // 表示当前线程池的工作状态，因为在多个线程中都要用到，所以需要考虑线程安全问题
	// 一旦线程池启动之后，不能再更改其内部参数：Fixed/Cached、threadSizeThresHold_、threadSizeThresHold_
};

#endif