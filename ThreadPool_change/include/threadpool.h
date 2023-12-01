#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <thread>
#include  <queue>
#include <memory>
#include <atomic>
#include <mutex>//线程安全的操作
#include <condition_variable>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <future>
#include <functional>
#include <semaphore.h>

const int TASK_MAX_THRESHOLD = INT32_MAX;//最大任务数量2147483647
const int THREAD_MAX_THRESHOLD =  100;//最大线程数量
const int THREAD_MAX_IDLE_TIME =  60;//线程最大空闲时间(s)

//线程类型 
class Thread
{
public:
    //一定要在void()里面有参数才能加int, 否则不能加， std::thread t(func_, 1), 则func_可以为std::function<void()>，否则不能加int
    using ThreadFunc = std::function<void(int)>;//返回值为void , 函数形参为int
    //线程构造
    Thread(ThreadFunc func)
        : func_(func)
        , threadId_(generateId_++)//每次创建1个线程，都让id++
    {} 
    //线程析构
    ~Thread() = default;
    //启动线程
    void start()
    {
        //创建一个线程来执行一个线程函数
        //要加上ref(func)
        std::thread t(func_, threadId_);//C++11来说，线程对象t和线程函数func_
        t.detach();//设置分离线程， func_和t分离， t.detach和t.join功能差不多
    }
    
    int getId() const
    {
        return threadId_;
    }
private:
    ThreadFunc func_;
    static int generateId_;//generateId的目的是为了让id进行更新
    int threadId_; //cached线程池不可少的,每个线程的id
};

//线程池工作模式，类和枚举项都是大驼峰命名法
enum class PoolMode //加上class后，枚举类型的作用域被限制在类中，不加class，枚举类型的作用域是全局的
{
    MODE_FIXED, //固定大小线程池
    MODE_CACHED, //动态大小线程池
};
/*

提交任务
pool.submitTask(std::make_shared<MyTask>());若Task无构造函数，则()里面为空
*/
//线程池类型
int Thread::generateId_ = 0;

class ThreadPool
{
public:
    ThreadPool()
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
    ~ThreadPool()
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
    void setMode(PoolMode mode)
    {
        if (checkRunningState())
        {
            return;
        }
        poolMode_ =  mode;
    }

    //设置task任务队列上限的阈值
    void setTaskQueMaxThreshold(int threshold)
    {
        if (checkRunningState())
        {
            return;
        }
        threadSizeThreshold_ = threshold;
    }

    void setInitThreadSize(int size)
    {
        initThreadSize_ = size;
    }
    //设置线程池cached模式下线程阈值
    void setThreadSizeThreshold(int threshold)
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
    //给线程池提交任务
    //使用可变参模版编程，让submitTask可以接受任意任务函数和任意数量的参数
    //pool.submitTask(sum1, 1, 2),  
    //Func&&代表引用折叠和右值引用，...Args代表可变参模版编程, 
    /**std::future<返回值类型>
     * std::future<decltype(func(args...))>:传入args...给func, 然后decltype推导func的返回结果来得到submitTask的返回值
     */
    template<typename Func, typename... Args>
    auto submitTask(Func&& func,  Args&&... args) -> std::future<decltype(func(args...))>//根据表达式的形式推导表达式结果
    {
        //这一行通过 decltype 推导出函数 func 在给定参数 args 的情况下的返回类型 RType。
        using RType = decltype(func(args...));//变量不能传递给类型，所以用using 非 auto

        //##########task########
        //这一行创建了一个共享指针（std::shared_ptr），指向一个带有返回类型 RType 的 std::packaged_task。
        //std::packaged_task 是一个将函数包装为可异步执行的任务的类。通过 std::bind 绑定了函数 func 和参数 args 到这个任务上。
        //这里使用了 std::forward 进行完美转发，以保留参数的值类别（左值或右值）和常量性。
        auto task = std::make_shared<std::packaged_task<RType()>>
        (std::bind(std::forward<Func>(func),  std::forward<Args>(args)...));
        //args...初始化func,  bind(func, args...)作为绑定器初始化packaged_task
        //例子：std::packaged_task<int()> task(std::bind(f, 2, 11));
        // std::future<int> result = task.get_future();
        //##########task########
        //得到结果
        std::future<RType> result = task->get_future();
        
        std::unique_lock<std::mutex> lock(taskQueMtx_);

        //等待在条件变量上
        if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
        [&]()->bool{
            return taskQue_.size() < taskQueMaxThreshold_;
        }))
        {
            std::cerr << "task queue is full, submit task fail." << std::endl;
            auto task = std::make_shared<std::packaged_task<RType()>>(
                []()->RType{ return RType(); }); //返回RType类型

            (*task)(); //执行task对象
            return task->get_future();
        }

        //如果有空余 把任务放入任务队列中
        // taskQue_.emplace(sp); 
        //[task]表示捕获task
        taskQue_.emplace([task](){
            //*执行下面的任务, (*task)(), 是指将task任务解引用，然后执行
            (*task)();
        });

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
        return result;
    }
    //开始线程池
//开始线程池
    void start(int initThreadSize) //CPU默认核心数量
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
    void threadFunc(int threadid) //线程函数返回， 相应的线程也就结束了
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
            
            Task task;
            {
                //先获得锁
                std::unique_lock<std::mutex> lock(taskQueMtx_);
                // std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
                std::cout << "love dabao============" << std::this_thread::get_id() << "==========love dabao" << std::endl;

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
                task(); //执行function<void()>
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
//============
    ThreadPool(const ThreadPool&) = delete; //禁止拷贝构造
    ThreadPool& operator=(const ThreadPool&) = delete; //禁止赋值构造

private:
    //定义线程函数
    void threadHandler();

//=============================线程================================
    //判断线程池运行的状态判断
    bool checkRunningState() const
    {
        return isPoolRunning_;
    }

private:
    //线程相关
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;//每个id对应1个thread
    size_t initThreadSize_; //初始化线程数量, size_t是unsigned int类型无符号整数
    int threadSizeThreshold_;//线程数量上限阈值 , 不能够无限增加线程数量
    std::atomic_int  curThreadSize_;//记录当前线程池里面线程总数量，由于线程数量会改变，所以得用原子类型
    std::atomic_int  idleThreadSize_;//记录空闲线程的数量， 由于空闲线程数量会改变，所以得用原子类型

    //Task任务就是函数对象
    using Task  = std::function<void()>;//返回值为void,  不带参数的函数对象
    std::queue<Task> taskQue_; //任务队列,之所用指针是因为任务类型不确定，可能是任意类型
    //concreteTask的run方法中，可以通过dynamic_cast转换为具体类型，将传入对象的生命周期延长，所以要用强智能指针
    std::atomic_int taskSize_; //任务数量
    int taskQueMaxThreshold_; //任务队列上限阈值

    //线程间通信相关
    std::mutex taskQueMtx_; //互斥锁
    std::condition_variable notFull_;//表示任务队列不满的条件变量
    std::condition_variable notEmpty_;//表示任务队列不空的条件变量
    std::condition_variable exitCond_; //等待线程资源回收 
    //线程池状态
    PoolMode  poolMode_;
    std::atomic_bool isPoolRunning_;//当前线程池的启动状态
};



#endif //THREADPOOL_H