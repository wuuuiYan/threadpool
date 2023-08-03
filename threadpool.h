#ifndef THREADPOOL_H //����ʽ���������û�ж��壬ִ������Ķ���
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

// Any���ͣ���ʾ���Խ����������ݵ�����
// ģ�����͵Ĵ���������ͷ�ļ��У������ڱ���׶�ʵ����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	// ��Ϊ��Ա��������ָ������unique_ptr��ֹ��ֵ���úͿ������죬���Ը���Ҳ��ֹ��ֵ���úͿ�������
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// ������캯��������Any���ͽ��������������Ͷ��������
	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}
	// ����ָ��ָ��������Ķ���

	// ��������ܹ���Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// �ӳ�Ա����base_�ҵ�ָ���Derive���󣬴Ӹö�������ȡ�����ĳ�Ա����data_
		// ����ָ�� -> ������ָ�� RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get()); //��Ա����get()��ȡ����ָ����ָ��������ָ��
		if (pd == nullptr)
		{
			// ���Ͳ�ƥ���ת��ʧ��
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	// ����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) {}
		T data_; // �������������������
	};
	// ʵ�����Ƕ��

private:
	// ��������ָ�룬����ָ��������Ķ���
	std::unique_ptr<Base> base_;
};

// ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0) : resLimit_(limit) {}
	~Semaphore() = default;

	// ��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		// �ȴ��ź�������Դ��û����Դ�Ļ���������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// ����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

// Task���͵�ǰ������
class Task;

// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
// ���߳��ύ�����ҽ��շ���ֵ��������������ʱ�������߳�ִ�У�������Ҫ�̼߳��ͨ��
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// ����һ����Ա����setVal()����ȡ����ִ����ķ���ֵ
	void setVal(Any any);
	
	// ���������Ա����get()���û��������������ȡtask�ķ���ֵ(Any���͵Ķ���)
	Any get();

private:
	Any any_; // �洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; // ��ʾ�����ύ�Ƿ�ɹ���ֻ�е������ύ�ɹ�ʱ�ſ��Խ��շ���ֵ
};

// ����������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);

	// �û������Զ��������������ͣ���Task�̳ж�������дrun()������ʵ���Զ���������
	virtual Any run() = 0;

private:
	Result* result_; // Result���������������Ҫ����Task��
	// �������ʹ����ָ�룬����ʹ������ָ��
};

// �̳߳�֧�ֵ�����ģʽ
enum class PoolMode
{
	MODE_FIXED,  // �߳������̶�
	MODE_CACHED, // �߳������ɶ�̬��������Ҳ���ᳬ���趨��ֵ
};

// �߳�����
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>; // ʹ�����ͱ��������̺߳����������ͣ���������Ϊvoid���β��б�Ϊ��

	// �̹߳���
	Thread(ThreadFunc func);

	// �߳�����
	~Thread();
	
	// �߳�����
	void start();

	// Ϊÿ���߳��ֶ�����ID
	int getId() const;

private:
	ThreadFunc func_; // �̺߳�������
	static int generateId_; // ʹ�þ�̬���������������κ�һ������
	int threadId_; // ���浱ǰ�߳�id
};

/*
* example:
*	ThreadPool pool
*	pool.start();
*	
*	class MyTask : public Task
*	{
*		public:
*			void run() { // �̴߳���...}
*	}��
*	
*	pool.submitTask(std::make_shared<MyTask>());
* 
*/


// �̳߳�����
class ThreadPool
{
public:
	// �̳߳ع���
	ThreadPool();
	// �̳߳�����
	~ThreadPool();

	// �����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	// ��������������ܴ洢�������������
	void setTaskQueMaxThresHold(int threshold);

	// ����cachedģʽ���߳��������ܴ洢������߳�����
	void setThreadSizeThresHold(int threshold);
	// ֻ�����̳߳���δ����ʱ������������������״̬�������������������ж���Ҫ�Ƚ����ж�

	// ���̳߳��ύ����(������)
	Result submitTask(std::shared_ptr<Task> sp);

	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());

	// ��ֹ�̳߳ؿ��������ֵ����
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �̺߳���
	void threadFunc(int threadId);

	// ����̳߳ص�����״̬
	bool checkRunningState() const;

private:
	// �ٷ����е���ĳ�Ա�������Ǽ�ǰ�»��ߣ���Ա�������������»������ӵ�ȫСдӢ��
	// ����Լ�DIY�Ŀ��Ա����ʹ�ú��»��ߣ���Ա������ʹ��С�շ�������

	// std::vector<std::unique_ptr<Thread>> threads_; // �洢�̵߳����������迼���̰߳�ȫ����
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �����̶߳������߳�ID֮��Ĺ���
	size_t initThreadSize_; // ��ʼ�߳�����
	std::atomic_int curThreadSize_; // ��ǰ�߳������е��߳�����
	// threads_.size()Ҳ�ܱ�ʾ��ǰ�߳������е��߳�����������Ϊvector�����̰߳�ȫ�ģ����Կ�����������һ��ԭ�����͵ı���
	int threadSizeThresHold_; // �߳��������ܴ洢������߳���������Ϊ�����޸ģ����Բ��޶�ԭ�Ӳ���
	std::atomic_int idleThreadSize_; // ��ǰ�����̵߳�����

	std::queue<std::shared_ptr<Task>> taskQue_; // �洢����Ķ��У���Ҫ�����̰߳�ȫ����
	// Q��Ϊʲô���ﲻ�����ָ�룬�����������ָ�룿
	// A����Ϊ��ĳЩ��������¿��ܴ������һ��������Task�����ڴ���������������������󽫱����٣���ʱ���ʹ����ָ�룬��������ڴ�й©
	std::atomic_uint taskSize_; // ������������Ϊ�û����������߳��������񶼻��޸Ĵ˱��������Ա��뱣֤ԭ�Ӳ���
	// taskQue_.size()Ҳ�ܱ�ʾ��ǰ��������е���������������Ϊqueue�����̰߳�ȫ�ģ����Կ�����������һ��ԭ�����͵ı���
	int taskQueMaxThresHold_; // ��������е����������������Ϊ�����޸ģ����Բ��޶�ԭ�Ӳ���

	std::mutex taskQueMtx_; // �����̶߳���Ψһʹ��Ȩ�Ļ���������֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в����������������û������������ύ����ȴ�ִ��(������)
	std::condition_variable notEmpty_; // ��ʾ������ﲻ�յ������������߳��������Դ���ȡ����ִ��(������)
	std::condition_variable exitCond_; // �ȴ��߳���Դȫ������

	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ��ʹ��ö�����Ͷ���
	std::atomic_bool isPoolRunning_; // ��ʾ��ǰ�̳߳صĹ���״̬����Ϊ�ڶ���߳��ж�Ҫ�õ���������Ҫ�����̰߳�ȫ����
};

#endif