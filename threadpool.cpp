#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THREAHOLD = INT32_MAX; // ��������пɴ洢�������������
const int THREAD_MAX_THRESHOLD = 1024; // �߳������пɴ洢������߳�����
const int THREAD_MAX_IDLE_TIME = 60; // �����߳����ȴ�ʱ�䣬��λ����

/////////////////  ThreadPool����ʵ��

// �̳߳ع��죬Ĭ�Ϲ��캯��
ThreadPool::ThreadPool()
	: initThreadSize_(0) // ��Ϊ�����̳߳�ʱ���趨��ʼֵ����������ͳһ����Ϊ0
	, curThreadSize_(0)
	, threadSizeThresHold_(THREAD_MAX_THRESHOLD)
	, idleThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThresHold_(TASK_MAX_THREAHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// �̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	// �ȴ��̳߳��е������̷߳��أ���ʱ�߳�������״̬���ȴ���ȡ���� & ����ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_); // �󶨻�������������
	notEmpty_.notify_all(); // waiting --> blocking
	exitCond_.wait(lock, [&]()->bool{return threads_.size() == 0; });
}

// �����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// ��������������ܴ洢�������������
void ThreadPool::setTaskQueMaxThresHold(int threshold)
{
	if (checkRunningState())
		return;
	taskQueMaxThresHold_ = threshold;
}

// ����cachedģʽ���߳��������ܴ洢������߳�����
void ThreadPool::setThreadSizeThresHold(int threshold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
		threadSizeThresHold_ = threshold;
}

// ���̳߳��ύ����(������)���û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��������������ٽ�������Σ����ھ�̬����
	// ��unique_lockģ����Ĺ��캯���У��ͻ����lock()��Ա�����Ի�����taskQueMtx_���м���
	// ����ʹ��unique_lockģ���࣬������ʹ��lock_guardģ���࣬��Ϊ���߲�����lock()��unlock()��Ա����
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	
	// �߳�ͨ�ţ��ȴ�������в��������������������Ҫ�޶��ȴ�ʱ�䣬����ʹ���û��߳�һֱ����
	// �û��ύ���񣬺����������������1s�������ж������ύʧ�ܣ�����
	/*while (taskQue_.size() == taskQueMaxThresHold_)
	{
		notFull_.wait(lock);
		// 1) ���߳�״̬��running��Ϊwaiting��2����ʱ����taskQueMtx_
	}*/
	// notFull_.wait(lock, [&]() -> bool {return taskQue_.size() < taskQueMaxThresHold_; })
	// ʹ�ó�Ա����wait()��������ʽ������ɵ��ö��󣬵ȴ�ֱ�������������յ��ź�&����������&�ɵ��ö�����������ʱ��running
	// ��Ա����wait_for()�����Ƶȴ��ɵ��ö��������ʱ�䣬�ȴ�����ʱ��߶ȣ���Ա����wait_until()�����Ƶȴ�ʱ�䣬ֱ��ĳ������ʱ�̣��޿ɵ��ö���
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() -> bool { return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		// ��ʾnotFull_�ѵȴ������ʱ�䣬�ɵ��ö���������Ȼ���������ύ����ʧ��
		std::cerr << "Task queue is full, submit task fail." << std::endl;
		return Result(sp, false); // ��ƥ�䵽Result���͵��ƶ��������ƶ���ֵ
		// return sp->getResult(); // �߳�ִ����task֮��Task���͵Ķ���ͱ�������
	}

	// ������в�������������������
	taskQue_.emplace(sp);
	taskSize_++;

	// ��Ϊ�·��������񣬶��п϶����գ�ͨ����������notEmpty_֪ͨ������(�߳�)������������
	notEmpty_.notify_all();

	// cachedģʽ��������ȽϽ�����С���������
	// ��Ҫ������������ & �����߳����� & ���߳������ж��Ƿ���Ҫ�����µ��̶߳���(ʹ���Զ����߳̿�)
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThresHold_)
	{
		std::cout << ">>> create new thread..." << std::endl;

		// �������̶߳���
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		// �����߳�
		threads_[threadId]->start();

		// �޸��̸߳�����ر���
		curThreadSize_++;
		idleThreadSize_++;
	}

	// �������������Result����
	return Result(sp);
	// return sp->getResult();
}

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// �����̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; ++i)
	{
		// ����Thread���͵Ķ���ʱ�����̺߳���threadFunc()���ݸ�Thread�Ĺ��캯���������̲߳���ִ�о�����̺߳���
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// bind()������һ��ͨ�õĺ�����������������һ���ɵ��ö�������һ���µĿɵ��ö�������Ӧԭ����Ĳ����б�
		// bind()��������һ���ɵ����̺߳���threadFunc()�������ɵ��ö����ڵ���threadFunc()ʱ��(_1, this)��Ϊʵ��
		// �����ɵ��ö��󱻴���Thread��Ĺ��캯����ʵ�����̶߳���Ȼ��make_unique()��������ָ����̶߳��������ָ��
		// make_unique()��������C++14��׼���Ƴ��ģ�C++11��׼��ֻ�Ƴ���make_shared()����
		// ����ָ��unique_ptr����ֵ���õĿ��������뿽����ֵ�����ã���˴˴�ֻ��ʹ����ֵ����
		// threads_.emplace_back(std::move(ptr)); // ʹ�ñ�׼�⺯��move()������Դ����Ȩ���ƶ�
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// ���������߳�
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threads_[i]->start(); // �����̣߳���Ҫִ�о�����̺߳���
		// ��ʹ���޸�֮��ʹ��map������map����Ҳ������operator[]
		idleThreadSize_++; // ��¼�����̵߳�����
	}
}

// �̺߳������̳߳��е��̴߳������������������(������)
void ThreadPool::threadFunc(int threadId) // �̺߳������أ���Ӧ���߳�Ҳ�ͽ����ˣ�����
{
	 // std::cout << "Begin with threadFunc tid: " << std::this_thread::get_id() << std::endl;
	 // std::cout << "End with threadFunc tid: " << std::this_thread::get_id() << std::endl;

	auto lastTime = std::chrono::high_resolution_clock().now();

	// �����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ���������̳߳�����ʱ�����̻��������߳���Դ�����Ի�����forѭ��
	// ���������Ϊ��ʱ���ж��̳߳��Ƿ�����
	for (; ; ) // ������������������һ����ѭ��
	{
		std::shared_ptr<Task> task;

		// Ϊ�˱�֤�߳�ȡ�������ͽ�����������Ҫ�ٿ�һ������Σ����������߳���Ҫ�ȴ����߳�ִ�����������ܻ�ȡ��
		// ������ν���ʱ�����ͻᱻunique_lockģ��������������ͷ�
		{
			// ��ȡ��������������ٽ�������Σ����ھ�̬����
			// ��unique_lockģ����Ĺ��캯���У��ͻ����lock()��Ա�����Ի�����taskQueMtx_���м���
			// ����ʹ��unique_lockģ���࣬������ʹ��lock_guardģ���࣬��Ϊ���߲�����lock()��unlock()��Ա����
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;
			
			// Q��ÿһ���ӷ���һ�Ρ�������ֳ�ʱ�������������ִ�з��أ�A���� + ˫���ж�
			// ֻҪ������û��ִ����ɣ��Ͳ������ѭ�������������ȫ��ִ������ܻ����߳���Դ
			while (taskQue_.size() == 0)
			{
				// �̳߳�Ҫ����������ԭ���ȴ���ȡ������߳���Դ
				if (!isPoolRunning_)
				{
					// �̳߳�Ҫ�����������߳���Դ
					threads_.erase(threadId); // ע�ⲻҪ����std::this_thread::get_id()
					std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return; // �̺߳������������߳̽���
				}

				// cachedģʽ�£��п����Ѿ������˺ܶ��̵߳�����ʱ�䳬��60s���ڱ�֤��ʼ�߳������Ļ�����Ӧ�û��ն����߳�
				// ���ܶࡱ������initThreadSize_; ������ʱ�䡱����ǰʱ�� - �ϴ��߳�ִ��ʱ��
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ������������ʱ���ء���Ա����wait_for()����ֵΪtimeout/no_timeout��ǰ�߱�ʾ��ʱ�����߱�ʾδ��ʱ
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto nowTime = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime); // ����ֻ��õ�����������Ҫת��Ϊʱ���ʽ
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) // �ڱ�֤��ʼ�߳������Ļ����ϻ����߳�
						{
							// ���յ�ǰ�̣߳����̶߳�����߳�������ɾ�����޸����߳�������صı���
							// ��ΰ��̺߳��߳�ID��Ӧ�����������Ҫ���洢�̵߳�������vector��Ϊunordered_map
							threads_.erase(threadId); // ע�ⲻҪ����std::this_thread::get_id()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
							return; // �̺߳������أ��߳̽���
						}
					}
				}
				else // Fixedģʽ�����������Ϊ��ʱ�ȴ��û��ύ����
				{
					// ���߳�ͨ�ţ�ͬʱ�ȴ�������в��ա�һ�����ֲ��գ���ֻ����һ���߳�������
					notEmpty_.wait(lock);
				}
				
				// �̳߳�Ҫ����������ԭ���ȴ���ȡ������߳���Դ
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadId); // ע�ⲻҪ����std::this_thread::get_id()

				//	std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return; // �̺߳������������߳̽���
				//}
			}

			idleThreadSize_--; // �߳̿�ʼִ�����񣬿����̵߳���������

			std::cout << "tid: " << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;

			// ������в��գ��Ӷ�����ȡ������
			task = taskQue_.front(); // std::shared_ptr<Task> task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �����������Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ��Ϊ�����������񣬶��п϶���������notFull_�Ͻ���֪ͨ�����û��ύ/��������
			notFull_.notify_all(); // ����������Եظ��ܵ�ʹ���������������ĺô�
		} // �Ӷ�����ȡ������֮���Ӧ�ý��������������ǰ����ȽϺ�ʱ�����̻߳ᷢ������
		
		// ��ǰ�̸߳���ִ�����������ִ�в��ܰ����ڼ����ķ�Χ�ڣ����������߳�Ҳ������������
		if (task != nullptr)
		{
			// ִ�����񣬰�����ķ���ֵͨ��setVal��������Result
			// task->run(); // �̳����̬�����ݻ���ָ����ָ��ľ�����������󣬵��þ���������д��Ա����
			task->exec(); // ���������൱���ֶ���һ��
		}

		idleThreadSize_++; // �߳�ִ��������ɣ������̵߳�����������
		lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
	}

	// �̳߳�Ҫ����������ԭ������ʵ�������е��߳���Դ
	// threads_.erase(threadId);
	// std::cout << "Thread: " << std::this_thread::get_id() << " exit!" << std::endl;
	// exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

/////////////////  Thread����ʵ��

int Thread::generateId_ = 0; // ��̬��Ա����������������г�ʼ��
// ��ʼ��ʱ����ʹ�ùؼ���static���ҿ��Ա������static��Ա��������

// �̹߳��죺�����̺߳����������ڶ���ʵ�������߳���ִ�еľ������
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

// �߳�����
Thread::~Thread() {}

// �߳�����
void Thread::start()
{
	// �����̣߳�ִ�����̳߳�ָ�����̺߳���
	std::thread t(func_, threadId_); // C++11��׼��t���̶߳���func_���̺߳���
	t.detach(); // ���߳������̷߳��룬���̲߳��ȴ����߳�������ɾͽ���
	// Linux�У�pthread_detach()��������pthread_t���óɷ����߳�
}

// Ϊÿ���߳��ֶ�����ID
int Thread::getId() const
{
	return threadId_;
}

/////////////////  Task����ʵ��

Task::Task() : result_(nullptr) {}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); // ���﷢����̬���ã�run()�����ķ���ֵ������Any����
		// ����������д�ĳ�Ա����run()�ķ���ֵ��Ϊʵ�Σ�����Result���͵ĳ�Ա����setVal
	}
}

void Task::setResult(Result *res)
{
	result_ = res;
}

/////////////////  Result����ʵ��

Result::Result(std::shared_ptr<Task> task, bool isValid) // ��������ʱ������Ҫָ��Ĭ�ϲ���
	: task_(std::move(task)), isValid_(isValid) 
{
	task_->setResult(this); // ͨ��Task����Result�����ϲ�����������̫����!!!
}

void Result::setVal(Any any) // ��˭�ڵ��������Ա������Task����Result������
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any); // ע��Any���ڶ���ʱ�ѽ�ֹ��ֵ���úͿ�������
	sem_.post(); // �Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}

Any Result::get() // �û�����
{
	if (!isValid_)
	{
		return ""; // ����ͻ����Any���͵Ĺ��캯��
	}
	sem_.wait(); // task�������û��ִ���꣬����������û��߳�
	return std::move(any_); // ע��Any���ڶ���ʱ�ѽ�ֹ��ֵ���úͿ�������
}