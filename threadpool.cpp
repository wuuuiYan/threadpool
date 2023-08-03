#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THREAHOLD = INT32_MAX; //任务队列中可存储的最大任务数量
const int THREAD_MAX_THREsHOLD = 1024; //线程容器中可存储的最大线程数量
const int THREAD_MAX_IDLE_TIME = 60; //空闲线程最大等待时间，单位：秒

/////////////////  ThreadPool方法实现

// 线程池构造，默认构造函数
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, curThreadSize_(0)
	, threadSizeThresHold_(THREAD_MAX_THREsHOLD)
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

	// 等待线程池中的所有线程返回，此时线程有两种状态：阻塞 & 运行
	std::unique_lock<std::mutex> lock(taskQueMtx_); //绑定互斥锁的智能锁
	notEmpty_.notify_all();
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
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	
	// 线程通信，等待任务队列不满
	// 用户提交任务，函数最长不能阻塞超过1s，否则判断任务提交失败，返回
	/*while (taskQue_.size() == taskQueMaxThresHold_)
	{
		notFull_.wait(lock);
		 1) 将线程状态由running改为waiting，2）解锁
	}*/
	// notFull_.wait(lock, [&]() -> bool {return taskQue_.size() < taskQueMaxThresHold_; })
	// 使用成员函数wait()的重载形式，等待直到条件变量接收到信号&互斥量解锁&可调用对象条件成立时才running
	// 成员函数wait_for()，限制等待可调用对象成立的时间；成员函数wait_until()，限制等待时间，无可调用对象
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() -> bool { return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		// 表示notFull_等待时间满，可调用对象条件依然不成立，提交任务失败
		std::cerr << "Task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
		// return sp->getResult();
	}

	// 如果队列不满，把任务放入队列中
	taskQue_.emplace(sp);
	taskSize_++;

	// 因为新放入了任务，队列肯定不空。在notEmpty_上进行通知可以消费任务
	notEmpty_.notify_all();

	// cached模式：需要根据任务数量&空闲线程数量&总线程数量判断是否需要创建新的线程对象(使用自定义线程库)
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

		// 修改相关变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	// 返回任务的Result对象
	return Result(sp);
	// return sp->getResult();
}

// 开启线程池
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
		// 创建Thread类型的对象时，把线程函数threadFunc传递给构造函数
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// bind()函数将线程函数与this指针绑定创建线程对象，然后make_unique()函数创建指向该线程对象的智能指针
		// make_unique()函数是在C++14标准中推出的，C++11标准中推出了make_shared()函数
		// 智能指针unique_ptr的左值引用的拷贝构造与赋值被禁用，因此此处只能使用右值引用
		// threads_.emplace_back(std::move(ptr)); 
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// 启动所有线程
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threads_[i]->start(); //需要执行线程函数
		// 即使是修改之后使用map容器，map本身也重载了operator[]
		idleThreadSize_++; //记录空闲线程的数量
	}
}

// 线程函数：线程池的所有线程从任务队列中消费任务
void ThreadPool::threadFunc(int threadId) //线程函数返回，相应的线程也就结束了！！！
{
	 // std::cout << "Begin with threadFunc tid: " << std::this_thread::get_id() << std::endl;

	 // std::cout << "End with threadFunc tid: " << std::this_thread::get_id() << std::endl;

	auto lastTime = std::chrono::high_resolution_clock().now();

	// 所有任务必须执行完成，线程池才可以回收所有线程资源
	for (; ; )//不断消费任务，所以是一个死循环
	{
		std::shared_ptr<Task> task;
		{
			// 获取锁，任务队列是临界区代码段，存在竟态条件
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

			
			// 每一秒钟返回一次。如何区分超时返回与有任务待执行返回？？？
			// 锁 + 双重判断
			while (taskQue_.size() == 0)
			{
				// 线程池要结束，回收线程资源
				if (!isPoolRunning_)
				{
					// 线程池要结束，回收线程资源
					threads_.erase(threadId); // 注意不要传入std::this_thread::get_id()
					std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}

				// cached模式下，有可能已经创建了很多线程但空闲时间超过60s，在保证初始线程数量的基础上应该回收多余线程
				// “很多”：超过initThreadSize_; “空闲时间”：当前时间 - 上次线程执行时间
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 条件变量，超时返回
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto nowTime = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// 回收当前线程：把线程对象从当前线程容器中删除，修改与线程数量相关的变量
							// 如何把线程和线程ID对应起来，这里就要将存储线程的容器从vector改为unordered_map
							threads_.erase(threadId); // 注意不要传入std::this_thread::get_id()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
							return; //线程函数返回，线程结束
						}
					}
				}
				else
				{
					// 线程通信，等待任务队列不空
					notEmpty_.wait(lock);
				}
				
				// 线程池要结束，回收线程资源
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadId); // 注意不要传入std::this_thread::get_id()

				//	std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return; //结束线程函数就是结束线程
				//}
			}

			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功..." << std::endl;

			// 如果队列不空，从队列中取出任务
			task = taskQue_.front();
			// std::shared_ptr<Task> task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 因为新消费了任务，队列肯定不满。在notFull_上进行通知可以生产任务
			notFull_.notify_all();
		} //从队列中取出任务在具体执行任务之前就应该解锁，否则如果当前任务比较耗时，则线程会发生阻塞
		
		// 当前线程负责执行任务
		if (task != nullptr)
		{
			// 执行任务，把任务的返回值通过setVal方法给到Result
			// task->run(); //继承与多态
			task->exec();
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

/////////////////  Thread方法实现

int Thread::generateId_ = 0; //静态成员变量在类外进行初始化

// 线程构造
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
	std::thread t(func_, threadId_); //C++11标准，t是线程对象，func_是线程函数
	t.detach(); //线程分离，主线程不等待子线程运行结束就结束
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
		result_->setVal(run()); //这里发生多态调用
		// 将派生类重写的成员函数run()的返回值作为实参，调用Result类型的成员函数setVal
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}

/////////////////  Result方法实现

Result::Result(std::shared_ptr<Task> task, bool isValid)
	: task_(std::move(task)), isValid_(isValid) 
{
	task_->setResult(this);
}

void Result::setVal(Any any) //是谁在调用这个成员函数？
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post(); //已经获取了任务的返回值，增加信号量资源
}

Any Result::get() //用户调用
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); //任务如果没有执行完，这里会阻塞用户线程
	return std::move(any_); //注意Any类在定义时已禁止左值引用和拷贝构造
}