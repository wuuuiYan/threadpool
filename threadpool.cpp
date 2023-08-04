#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THREAHOLD = INT32_MAX; // 任务队列中可存储的最大任务数量
const int THREAD_MAX_THRESHOLD = 1024; // 线程容器中可存储的最大线程数量
const int THREAD_MAX_IDLE_TIME = 60; // 空闲线程最大等待时间，单位：秒

/////////////////  ThreadPool方法实现

// 线程池构造，默认构造函数
ThreadPool::ThreadPool()
	: initThreadSize_(0) // 因为启动线程池时会设定初始值，所以这里统一设置为0
	, curThreadSize_(0)
	, threadSizeThresHold_(THREAD_MAX_THRESHOLD)
	, idleThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThresHold_(TASK_MAX_THREAHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// 线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	// 等待线程池中的所有线程返回，此时线程有两种状态：等待获取任务 & 正在执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_); // 绑定互斥锁的智能锁
	notEmpty_.notify_all(); // waiting --> blocking
	exitCond_.wait(lock, [&]()->bool{return threads_.size() == 0; });
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// 设置任务队列中能存储的最大任务数量
void ThreadPool::setTaskQueMaxThresHold(int threshold)
{
	if (checkRunningState())
		return;
	taskQueMaxThresHold_ = threshold;
}

// 设置cached模式下线程容器中能存储的最大线程数量
void ThreadPool::setThreadSizeThresHold(int threshold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
		threadSizeThresHold_ = threshold;
}

// 向线程池提交任务(生产者)：用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁，任务队列是临界区代码段，存在竟态条件
	// 在unique_lock模板类的构造函数中，就会调用lock()成员函数对互斥锁taskQueMtx_进行加锁
	// 必须使用unique_lock模板类，而不能使用lock_guard模板类，因为后者不存在lock()与unlock()成员函数
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	
	// 线程通信，等待任务队列不满。如果队列已满则需要限定等待时间，不能使得用户线程一直阻塞
	// 用户提交任务，函数最长不能阻塞超过1s，否则判断任务提交失败，返回
	/*while (taskQue_.size() == taskQueMaxThresHold_)
	{
		notFull_.wait(lock);
		// 1) 将线程状态由running改为waiting，2）暂时解锁taskQueMtx_
	}*/
	// notFull_.wait(lock, [&]() -> bool {return taskQue_.size() < taskQueMaxThresHold_; })
	// 使用成员函数wait()的重载形式：传入可调用对象，等待直到条件变量接收到信号&互斥量解锁&可调用对象条件成立时才running
	// 成员函数wait_for()，限制等待可调用对象成立的时间，等待给定时间尺度；成员函数wait_until()，限制等待时间，直到某个绝对时刻，无可调用对象
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() -> bool { return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		// 表示notFull_已等待完给定时间，可调用对象条件依然不成立，提交任务失败
		std::cerr << "Task queue is full, submit task fail." << std::endl;
		return Result(sp, false); // 会匹配到Result类型的移动构造与移动赋值
		// return sp->getResult(); // 线程执行完task之后，Task类型的对象就被析构了
	}

	// 如果队列不满，把任务放入队列中
	taskQue_.emplace(sp);
	taskSize_++;

	// 因为新放入了任务，队列肯定不空，通过条件变量notEmpty_通知消费者(线程)可以消费任务
	notEmpty_.notify_all();

	// cached模式：任务处理比较紧急，小而快的任务
	// 需要根据任务数量 & 空闲线程数量 & 总线程数量判断是否需要创建新的线程对象(使用自定义线程库)
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThresHold_)
	{
		std::cout << ">>> create new thread..." << std::endl;

		// 创建新线程对象
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		// 启动线程
		threads_[threadId]->start();

		// 修改线程个数相关变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	// 返回任务的匿名Result对象
	return Result(sp);
	// return sp->getResult();
}

// 启动线程池
void ThreadPool::start(int initThreadSize)
{
	// 设置线程池的运行状态
	isPoolRunning_ = true;

	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; ++i)
	{
		// 创建Thread类型的对象时，把线程函数threadFunc()传递给Thread的构造函数，这样线程才能执行具体的线程函数
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// bind()函数是一个通用的函数适配器，它接受一个可调用对象，生成一个新的可调用对象来适应原对象的参数列表
		// bind()函数生成一个可调用线程函数threadFunc()的匿名可调用对象，在调用threadFunc()时将(_1, this)作为实参
		// 匿名可调用对象被传入Thread类的构造函数，实例化线程对象，然后make_unique()函数创建指向该线程对象的智能指针
		// make_unique()函数是在C++14标准中推出的，C++11标准中只推出了make_shared()函数
		// 智能指针unique_ptr的左值引用的拷贝构造与拷贝赋值被禁用，因此此处只能使用右值引用
		// threads_.emplace_back(std::move(ptr)); // 使用标准库函数move()进行资源所有权的移动
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// 启动所有线程
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threads_[i]->start(); // 启动线程，需要执行具体的线程函数
		// 即使是修改之后使用map容器，map本身也重载了operator[]
		idleThreadSize_++; // 记录空闲线程的数量
	}
}

// 线程函数：线程池中的线程从任务队列中消费任务(消费者)
void ThreadPool::threadFunc(int threadId) // 线程函数返回，相应的线程也就结束了！！！
{
	 // std::cout << "Begin with threadFunc tid: " << std::this_thread::get_id() << std::endl;
	 // std::cout << "End with threadFunc tid: " << std::this_thread::get_id() << std::endl;

	auto lastTime = std::chrono::high_resolution_clock().now();

	// 所有任务必须执行完成，线程池才可以回收所有线程资源，而不是线程池析构时就立刻回收所有线程资源，所以还是用for循环
	// 当任务队列为空时才判断线程池是否析构
	for (; ; ) // 不断消费任务，所以是一个死循环
	{
		std::shared_ptr<Task> task;

		// 为了保证线程取得任务后就解锁，所以需要再开一个代码段，否则其他线程需要等待该线程执行完任务后才能获取锁
		// 当代码段结束时，锁就会被unique_lock模板类的析构函数释放
		{
			// 获取锁，任务队列是临界区代码段，存在竟态条件
			// 在unique_lock模板类的构造函数中，就会调用lock()成员函数对互斥锁taskQueMtx_进行加锁
			// 必须使用unique_lock模板类，而不能使用lock_guard模板类，因为后者不存在lock()与unlock()成员函数
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
			
			// Q：每一秒钟返回一次。如何区分超时返回与有任务待执行返回？A：锁 + 双重判断
			// 只要有任务没有执行完成，就不会进入循环，必须等任务全部执行完才能回收线程资源
			while (taskQue_.size() == 0)
			{
				// 线程池要结束，回收原来等待获取任务的线程资源
				if (!isPoolRunning_)
				{
					// 线程池要结束，回收线程资源
					threads_.erase(threadId); // 注意不要传入std::this_thread::get_id()
					std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return; // 线程函数结束就是线程结束
				}

				// cached模式下，有可能已经创建了很多线程但空闲时间超过60s，在保证初始线程数量的基础上应该回收多余线程
				// “很多”：超过initThreadSize_; “空闲时间”：当前时间 - 上次线程执行时间
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 条件变量，超时返回。成员函数wait_for()返回值为timeout/no_timeout，前者表示超时，后者表示未超时
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto nowTime = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime); // 作差只会得到周期数，还要转化为时间格式
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) // 在保证初始线程数量的基础上回收线程
						{
							// 回收当前线程：把线程对象从线程容器中删除，修改与线程数量相关的变量
							// 如何把线程和线程ID对应起来，这里就要将存储线程的容器从vector改为unordered_map
							threads_.erase(threadId); // 注意不要传入std::this_thread::get_id()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
							return; // 线程函数返回，线程结束
						}
					}
				}
				else // Fixed模式，当任务队列为空时等待用户提交任务
				{
					// 多线程通信，同时等待任务队列不空。一旦发现不空，但只能有一个线程抢到锁
					notEmpty_.wait(lock);
				}
				
				// 线程池要结束，回收原来等待获取任务的线程资源
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadId); // 注意不要传入std::this_thread::get_id()

				//	std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return; // 线程函数结束就是线程结束
				//}
			}

			idleThreadSize_--; // 线程开始执行任务，空闲线程的数量减少

			std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功..." << std::endl;

			// 如果队列不空，从队列中取出任务
			task = taskQue_.front(); // std::shared_ptr<Task> task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果队列中依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 因为新消费了任务，队列肯定不满。在notFull_上进行通知可以用户提交/生产任务
			notFull_.notify_all(); // 这里就能明显地感受到使用两个条件变量的好处
		} // 从队列中取出任务之后就应该解锁，否则如果当前任务比较耗时，则线程会发生阻塞
		
		// 当前线程负责执行任务，任务的执行不能包含在加锁的范围内，否则其他线程也不能消费任务
		if (task != nullptr)
		{
			// 执行任务，把任务的返回值通过setVal方法给到Result
			// task->run(); // 继承与多态，根据基类指针所指向的具体派生类对象，调用具体对象的重写成员函数
			task->exec(); // 这里的设计相当于又多套一层
		}

		idleThreadSize_++; // 线程执行任务完成，空闲线程的数量又增加
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}

	// 线程池要结束，回收原来正在实行任务中的线程资源
	// threads_.erase(threadId);
	// std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
	// exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

/////////////////  Thread方法实现

int Thread::generateId_ = 0; // 静态成员变量必须在类外进行初始化
// 初始化时不再使用关键字static，且可以被任意非static成员函数访问

// 线程构造：接受线程函数对象，用于定义实例化的线程所执行的具体操作
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

// 线程析构
Thread::~Thread() {}

// 线程启动
void Thread::start()
{
	// 创建线程，执行由线程池指定的线程函数
	std::thread t(func_, threadId_); // C++11标准，t是线程对象，func_是线程函数
	t.detach(); // 子线程与主线程分离，主线程不等待子线程运行完成就结束
	// Linux中：pthread_detach()函数，将pthread_t设置成分离线程
}

// 为每个线程手动分配ID
int Thread::getId() const
{
	return threadId_;
}

/////////////////  Task方法实现

Task::Task() : result_(nullptr) {}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); // 这里发生多态调用，run()函数的返回值正好是Any类型
		// 将派生类重写的成员函数run()的返回值作为实参，调用Result类型的成员函数setVal
	}
}

void Task::setResult(Result *res)
{
	result_ = res;
}

/////////////////  Result方法实现

Result::Result(std::shared_ptr<Task> task, bool isValid) // 函数定义时不再需要指明默认参数
	: task_(std::move(task)), isValid_(isValid) 
{
	task_->setResult(this); // 通过Task类与Result类的耦合才能这样做，太妙了!!!
}

void Result::setVal(Any any) // 是谁在调用这个成员函数？Task类与Result类的耦合
{
	// 存储task的返回值
	this->any_ = std::move(any); // 注意Any类在定义时已禁止左值引用和拷贝构造
	sem_.post(); // 已经获取了任务的返回值，增加信号量资源
}

Any Result::get() // 用户调用
{
	if (!isValid_)
	{
		return ""; // 这里就会调用Any类型的构造函数
	}
	sem_.wait(); // task任务如果没有执行完，这里会阻塞用户线程
	return std::move(any_); // 注意Any类在定义时已禁止左值引用和拷贝构造
}