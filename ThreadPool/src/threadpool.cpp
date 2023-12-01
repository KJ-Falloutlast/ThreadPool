#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
const int TASK_MAX_THRESHOLD = INT32_MAX;//最大任务数量2147483647
const int THREAD_MAX_THRESHOLD =  100;//最大线程数量
const int THREAD_MAX_IDLE_TIME =  60;//线程最大空闲时间(s)

//=============================线程池================================
//线程池构造
//锁不要初始化
ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , taskSize_(0) 
    , idleThreadSize_(0) //空闲线程
    , curThreadSize_(0) 
    , threadSizeThreshold_(THREAD_MAX_THRESHOLD) //线程最大上限
    , taskQueMaxThreshold_  (TASK_MAX_THRESHOLD)//不要在代码中出现除了0/1的数字，数字要用变量代替
    , poolMode_(PoolMode::MODE_FIXED) 
    , isPoolRunning_(false) 
    {}

//线程池的析构,在C++工程中，有资源的分配，就有资源的析构
ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;
    //等待线程池的线程返回, 有两种状态：阻塞  & 正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();//通知notEmpty_.wait从等待进入阻塞
    //?为什么有1个线程未被回收， 检查线程队列还有线程，所以一直等
    //size = 0, 资源回收完了，向下走
    exitCond_.wait(lock,  [&]()->bool{return threads_.size() == 0;}); //当 threads_.size() != 0 线程进入阻塞状态，释放锁；否则往下执行
}


//线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
    {
        return;
    }
    poolMode_ =  mode;
}
//设置task任务队列上限的阈值
void ThreadPool::setTaskQueMaxThreshold(int threshold)
{
    if (checkRunningState())
    {
        return;
    }
    threadSizeThreshold_ = threshold;
}

//设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeThreshold(int threshold)
{
    if (checkRunningState())
    {
        return;
    }
    if (poolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThreshold_ = threshold;
    }
}
void ThreadPool::setInitThreadSize(int size)
{
    
}

//给线程池添加任务 生产者
//如果用户调用submittask阻塞了1s时间，任务队列还没有空余下来，返回任务提交失败
/*
*void ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    //获得锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    //线程的通信 等待任务队列有空余
    //用户提交任务，最长不能阻塞超过1s, 否则判断提交任务失败，返回
    //方法1：
    // while (taskQue_.size() == taskQueMaxThreshold_)
    // {
    //     notFull_.wait(lock);
    // }
    //*方法2：wait

    1.&为引用方式捕获变量
    2.bool = false则阻塞线程，同时释放锁，否则无法执行下面的语句，bool = true, 继续执行下面的语句
    3. wait(等待条件满足才继续执行) , wait_for(增加10s, 10s条件还不满足，继续向下执行) 
        , wait_until(等待到12点, 12点之后就不等了)
        wait_for和wait_until有返回值

    // notFull_.wait(lock,
    //         [&]()->bool{
    //         return taskQue_.size() < taskQueMaxThreshold_;
    //         });
    //*方法3：wait_for, 等待1s
    //!先判断taskQue_.size() < taskQueMaxThreshold_,如果返回true,执行下面的语句，
    //如果返回false: 阻塞等待1s, 如果超过1s则return, 如果未超过1s阻塞停止，就继续执行下面的语句
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
            [&]()->bool{
            return taskQue_.size() < taskQueMaxThreshold_;
            }))
    {
        //表示notFull等待1s, 条件依然没有满足
        std::cerr << "task queue is full, submit task fail." << std::endl;
        return;
    }
    //如果有空余 把任务放入任务队列中
    taskQue_.emplace(sp);
    taskSize_++; //将task的数量++
    
    //因为新放了任务，任务队列肯定不空了， 在notEmpty上通知消费者 ，分配线程执行任务
    notEmpty_.notify_all();
}
*/
//##############Result返回值##############
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    //获得锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    //线程的通信 等待任务队列有空余
    //用户提交任务，最长不能阻塞超过1s, 否则判断提交任务失败，返回
    //方法1：
    // while (taskQue_.size() == taskQueMaxThreshold_)
    // {
    //     notFull_.wait(lock);
    // }
    //*方法2：wait
/*1.&为引用方式捕获变量
    2.bool = false则阻塞线程，同时释放锁，否则无法执行下面的语句，bool = true, 继续执行下面的语句
    3. wait(等待条件满足才继续执行) , wait_for(增加10s, 10s条件还不满足，继续向下执行) 
        , wait_until(等待到12点, 12点之后就不等了)
        wait_for和wait_until有返回值*/
    // notFull_.wait(lock,
    //         [&]()->bool{
    //         return taskQue_.size() < taskQueMaxThreshold_;
    //         });
    //*方法3：wait_for, 等待1s
    //!先判断taskQue_.size() < taskQueMaxThreshold_,如果返回true,执行下面的语句，
    //如果返回false: 阻塞等待1s, 如果超过1s则return, 如果未超过1s阻塞停止，就继续执行下面的语句
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
            [&]()->bool{
                return taskQue_.size() < taskQueMaxThreshold_;
            }))
    {
        //表示notFull等待1s, 条件依然没有满足
        std::cerr << "task queue is full, submit task fail." << std::endl;
        //?选择哪一个？
        // return task->getResult();//不可以用这个，为什么？
        return Result(sp, false);//false代表无效任务返回值
    }
    //如果有空余 把任务放入任务队列中
    taskQue_.emplace(sp);
    taskSize_++; //将task的数量++
    //因为新放了任务，任务队列肯定不空了， 在notEmpty上通知消费者 ，分配线程执行任务
    notEmpty_.notify_all();

    //?需要根据任务数量和空闲线程数量，判断是否需要创建新的线程
    //cached模式 任务处理比较紧急 场景：小而快的任务，需要根据任务数量和空闲线程数量，判断是否为空
    //返回任务的Result对象
    if (poolMode_ == PoolMode::MODE_CACHED 
        && taskSize_ > idleThreadSize_
        && curThreadSize_ < threadSizeThreshold_)
    {
        std::cout <<  ">>> create new thread" << std::endl;
        //创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,  std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        //启动线程
        threads_[threadId]->start();
        //修改线程个数相关变量++
        curThreadSize_++;
        idleThreadSize_++;
    }
    //返回任务的Result对象
    return Result(sp);
}
//##############Result返回值##############

//开始线程池
void ThreadPool::start(int initThreadSize) //CPU默认核心数量
{   
    //设置线程池的运行状态
    isPoolRunning_ = true;
    //记录初始线程对象
    initThreadSize_  = initThreadSize;
    curThreadSize_  = initThreadSize;
    for (int i = 0; i  < initThreadSize_; i++)
    {
        //创建thread线程对象的时候，把线程对象给thread线程对象
        //?这个地方是重点，用绑定器把threadFunc绑定在ptr上
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        // threads_.emplace_back(std::move(ptr));//对应的new一定会有delete
        //因为unique_ptr是不允许普通的拷贝构造和赋值，所以要用右值
        //bind的意义是，将ThreadPool函数对象绑定在threadFunc函数上
    }

    //启动所有线程
    //    std::vector<Thread*>  threads_;//线程列表
    for (int i = 0; i < initThreadSize_; i++)
    { 
        threads_[i]->start();//启动所有线程(而非线程),需要执行一个线程函数
        idleThreadSize_++; //记录初始空闲线程的数量
    }
}

//定义线程函数(线程池的所有任务从线程池消费任务)
void ThreadPool::threadFunc(int threadid) //线程函数返回， 相应的线程也就结束了
{
    // std::cout << "begin threadFunc" << std::this_thread::get_id() << std::endl;
    // std::cout << std::endl;
    // std::cout << "end threadFunc" << std::this_thread::get_id() << std::endl;
    //如果在相同的线程，打印的id是一样的，不一样的线程是不一样的
    auto lastTime = std::chrono::high_resolution_clock::now();
    //线程不断循环 
    //!如果不加unlock或者局部作用区域，则在一个线程未执行完之前，一直占用这把锁，没有其他线程对task队列进行操作，降低线程池效率
    //所有任务必须执行完成，线程池才可以回收所有线程资源，所以不能用    while(isPoolRunning_) 
    for (;;)
    {
		//方法1：unlock
		/* 
		//先获得锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//等待notEmpty条件
		//* true通过，false阻塞
		notEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0;});

		//从任务队列中取一个任务
		auto task = taskQue_.front();
		taskQue_.pop();
		taskSize_--;
		//!任务的执行不能包含在锁的范围内的
		lock.unlock();
		//当前线程负责执行这个任务
		*/

		//方法2：加上局部作用区域 
		std::shared_ptr<Task> task;
		{
			//先获得锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
			//cached模式下，有可能已经创建了很多线程，但是空闲时间超过60s,应该把多余的线程回收掉？
			//结束回收掉(超过initThreadSize数量的线程要回收)
			//当前时间 -  上一次线程执行时间  > 60s
			while (taskQue_.size() ==  0)
			{
                //线程池结束,回收线程 资源
                if (!isPoolRunning_ )
                {
                    //线程函数结束，删除线程
                    threads_.erase(threadid);//删掉线程后，空闲线程和线程池数量--
                    std::cout << ">>> threadid...." << std::this_thread::get_id() << "exit" << std::endl;
                    exitCond_.notify_all();//通知等待在exitCond_.wait(lock,  [&]()->bool{return threads_.size() == 0;});进入阻塞状态
                    return;//线程函数结束，线程结束
                }
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// !任务队列里面有任务不等待，无任务才等待，所以为taskQue_.size() == 0
                    if (std::cv_status::timeout == 
                            notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        //dur.count()表示的是Second
                        if (dur.count() >= THREAD_MAX_IDLE_TIME
                            && curThreadSize_ > initThreadSize_) //不能一直回收线程，一定要保证线程数量 > initsize
                        {
                            //开始回收当前线程
                            threads_.erase(threadid);//删掉线程后，空闲线程和线程池数量--
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout << ">>> threadid...." << std::this_thread::get_id() << "exit" << std::endl;
                            return;
                        }
                    }
				}
				else //若不是cached状态
				{
					//等待notEmpty条件。如果没有超时，则执行notEmpty_.wait(lock)
					//* true通过，false阻塞
					notEmpty_.wait(lock);
				}
				// //线程池结束,回收线程 资源
				// if (!isPoolRunning_ )
				// {
				// 	threads_.erase(threadid);//删掉线程后，空闲线程和线程池数量--
				// 	std::cout << ">>> threadid...." << std::this_thread::get_id() << "exit" << std::endl;
				// 	exitCond_.notify_all();//通知在exitCond_.wait(lock,  [&]()->bool{return threads_.size() == 0;});进入阻塞状态，判断threads.size()是否为0
				// 	return;//结束线程和函数，就是结束
				// }
			}
            //如果任务队列非空，取出一个任务并减小任务队列大小，然后通知其他等待在 notEmpty_ 上的线程（消费者线程）有任务可以执行。
            //同时通知等待在 notFull_ 上的线程（生产者线程）可以继续提交任务。
			idleThreadSize_--; //线程起来了，要去任务队列取任务，所以线程数量--
			std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功..." << std::endl;
			//从任务队列中取一个任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			//如果依然有剩余任务，继续通知其他线程(消费者)执行任务。有wait就有notify!
			//notEmpty_->消费者, notFull_->生产者
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();//唤醒其他线程池的线程，快来消费任务(可能有其他线程等待)
			}
			//*取出任务进行通知(生产者), 通知可以继续提交生产任务，在满的时候才有用
			notFull_.notify_all();
		}//释放unique_lock中的mtx

		if (task != nullptr)
		{
			//把任务的返回值setVal方法给到Result
			//*如果要增加更多任务在run上，价格函数套run, 发生多态
			task->exec();
            //task->run();//基类指针指向哪个派生对象，就会调用哪个派生对象对应的同名重载方法
		}
		idleThreadSize_++; //线程任务执行完后，线程数量++
		lastTime = std::chrono::high_resolution_clock::now(); //更新线程执行完的时间
		/*
		这个地方有一个问题：
		1. thread从task队列中拿到一把锁，等待任务执行完后再释放mutex
			2.thread从task队列中拿到mutex, 然后释放mutex, 让别的thread拿到可以获取mutex继续对task队列操作
			肯定是选择2 
		*/
    }//如果不加unlock, unique_lock在此处释放mutex
}
//=============================线程池================================



//=============================线程================================
int Thread::generateId_ = 0;
//判断线程池运行的状态判断
bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}
//##################线程方法实现#################
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)//每次创建1个线程，都让id++
{} 

//线程析构 
Thread::~Thread()
{}


//启动线程(注意，启动线程和线程池不一样)
void Thread::start()
{
    //创建一个线程来执行一个线程函数
    //要加上ref(func)
    std::thread t(func_, threadId_);//C++11来说，线程对象t和线程函数func_
    t.detach();//设置分离线程， func_和t分离， t.detach和t.join功能差不多
}

int Thread::getId() const
{
    return threadId_;
}
//=============================线程================================



//=============================Task================================
Task::Task()
    : result_(nullptr)
{}

void Task::exec()
{
    if (result_  != nullptr)
    {
        result_->setVal(run());//发生多态调用，方便用户重写run方法
    }
}

void Task::setResult(Result* res)
{
    result_ = res;
}


//############Result方法实现##########
Result::Result(std::shared_ptr<Task> task, bool isValid )
    : isValid_(isValid)
    , task_(task)
{
    task_->setResult(this);
}

Any Result::get() //用户执行的
{
    if (!isValid_)
    {
        return "";
    }
    sem_.wait(); //task任务如果没有执行完，会阻塞用户线程,任务执行完了，post一下，sem_有资源，继续执行
    //std::any_为左值 threadid.
    return  std::move(any_);//由于Any成员变量为unique_ptr，他是没有左值的，所以要返回右值
}
void Result::setVal(Any any)
{
    //存储task的返回值
    this->any_ = std::move(any);
    sem_.post();//已经获取任务的返回值，增加信号量资源
}



