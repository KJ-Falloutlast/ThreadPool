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
//不要用using namespace std

//Any类型：可以接收和返回任意类型
/*为什么Any的定义和实现不写在.h和.cpp中，C++标准明确表示，
当一个模板不被用到的时侯，它就不该被实例化出来。这就表示Test.cpp.o中没有test函数的定义。
所以模版类的定义和实现写在.h中，不要分开写
*/
class Any
{
public:
    //要给出默认构造和析构
    Any() = default;
    ~Any() = default;
    //接收一个成员变量
    //成员变量base已禁止左值/右值拷贝和构造，那么构造函数也得禁止
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default; 
    Any& operator=(Any&&) = default;
    //!核心所在， 利用模版+多态+智能指针，实现任意类型接受，完成基类指针完成对派生类对象的接受
    //*这个构造函数可以让Any接受任意其他类型数据
    // Any(T data) : base_(new Derive <T>(data))
    template<typename T> 
    Any(T data) : base_(std::make_unique<Derive <T>>(data))
    { }

    //能把Any对象存储的data数据提取出来
    template<typename T>// T:int  Derive<int>: int, cast_: long
    T cast_()
    {
        //我们怎么从base_找到它所指向的Derive对象，从它里面取出data成员变量
        //*基类指针 ----> 派生类类指针 RTTI, 因为Base类型指向Derive类型，所以能够用Dynamic_cast强转
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr)//如果类型不匹配，就无法转化成功
        {
            throw "type is unmatch!" ;
        }
        return pd->data_;
    }
private:
    //基类类型
    class Base
    {
    public:
        virtual ~Base() = default;///相当于 virtual ~Base(){}
    };

    //派生类类型
    template<typename T>
    class Derive: public Base
    {
        public:
            Derive(T data) : data_(data){}
            T data_;//保存了任意的其他类型
    };
//成员变量
private:
    //定义1个基类指针
    std::unique_ptr<Base> base_;//禁止左值的拷贝和构造
};

//信号量
class Semaphore
{
public:
    Semaphore(int limit = 0) : resLimit_(limit)
    {}
    ~Semaphore() = default;
    //获取一个信号量资源, resLimit_--
    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx_);//获取 一把锁
        //等待信号量有资源，没有资源，会阻塞当前线程，有资源,继续执行下面的代码
        cond_.wait(lock, [&]()->bool{return resLimit_ > 0; });
        resLimit_--;
        //消耗就消耗了，不用通知
    }

    //增加一个信号量资源, resLimit_++
    void post() //post为执行
    {
        std::unique_lock<std::mutex> lock(mtx_);//获取 一把锁
        resLimit_++;//资源计数+1
        cond_.notify_all();//资源计数++后，通知
    }
private:
    int resLimit_;//资源计数
    std::mutex mtx_;
    std::condition_variable cond_;

};

//Task类型的前置声明
class Task;
//实现接受提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
    // error: use of deleted function ‘Result::Result(const Result&)，因为Result的拷贝构造函数被禁止了·
    //解决这个问题，就是把Result的拷贝构造函数和赋值构造函数都禁止掉
    //如何解决这个问题？
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;
    void setVal(Any any);
    //问题1：setVal方法，获取任务执行完的返回值(任务执行完，它的返回值在哪)
    
    //问题2：get方法，用户调用这个方法获取task的返回值(怎么拿到任务执行的run的返回值,存在Result对象的Any)
    Any get();
private:
    Any any_; //存储任务的返回值
    Semaphore sem_;//线程通信信号量
    std::shared_ptr<Task> task_;//指向对应获取返回值的任务对象, task的引用计数不为0, 则task不会析构
    std::atomic_bool isValid_; //返回值是否有效，如果任务已经提交失败了，返回值肯定是无效的
};

//任务抽象基类
//用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务
class Task
{
public:
    Task();
    ~Task() = default;
    // virtual void run() = 0;                                                                                                                                                                                                                                                                                             
    void exec();//不会多态
    void setResult(Result* res);
    virtual Any run() = 0;//多态调用。virtual 和 虚函数不能放在一块， 任务的返回值在这                                                                                                                                                                                                                                                                                               
private:
    //这里不能用智能指针，否则会和Result发生交叉引用
    Result* result_;//Result对象的生命周期 > Task对象生命周期
};

//线程类型 
class Thread
{
public:
    //一定要在void()里面有参数才能加int, 否则不能加， std::thread t(func_, 1), 则func_可以为std::function<void()>，否则不能加int
    using ThreadFunc = std::function<void(int)>;//返回值为void , 函数形参为int
    //线程构造
    Thread(ThreadFunc func);
    //线程析构
    ~Thread();
    //启动线程
    void start();
    
    //获取线程ID
    int getId() const;
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
example:
ThreadPool pool
pool.start()
class MyTask : public Task
{
    public:
        void run(){//线程代码}
        
}
提交任务
pool.submitTask(std::make_shared<MyTask>());若Task无构造函数，则()里面为空
*/
//线程池类型
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();
    //线程池的工作模式
    void setMode(PoolMode mode);
    //设置task任务队列上限的阈值
    void setTaskQueMaxThreshold(int threshold);
    void setInitThreadSize(int size);
    void setThreadSizeThreshold(int threshold);//设置线程上限阈值
    //给线程池添加任务
    // void submitTask(std::shared_ptr<Task> sp);
    Result submitTask(std::shared_ptr<Task> sp);
    //开始线程池
    void start(int initThreadSize = 4);
    void threadFunc(int threadid);
    //线程池之所以要禁止拷贝构造和赋值构造，是因为线程池的生命周期是由用户控制的，
    //如果允许拷贝构造和赋值构造，那么就会出现多个线程池同时运行的情况
    ThreadPool(const ThreadPool&) = delete; //禁止拷贝构造
    ThreadPool& operator=(const ThreadPool&) = delete; //禁止赋值构造

private:
    //定义线程函数
    void threadHandler();

    //检查pool的运行的状态, 为成员函数服务的函数，要为private模式
    bool checkRunningState() const;
private:
    //线程相关
    // std::vector<std::unique_ptr<Thread>>  threads_;// 如果用裸指针不会自动释放，改为智能指针,会自动释放new出来的内存
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;//每个id对应1个thread
    size_t initThreadSize_; //初始化线程数量, size_t是unsigned int类型无符号整数
    int threadSizeThreshold_;//线程数量上限阈值 , 不能够无限增加线程数量
    std::atomic_int  curThreadSize_;//记录当前线程池里面线程总数量，由于线程数量会改变，所以得用原子类型
    std::atomic_int  idleThreadSize_;//记录空闲线程的数量， 由于空闲线程数量会改变，所以得用原子类型

    //任务相关
    std::queue<std::shared_ptr<Task>> taskQue_; //任务队列,之所用指针是因为任务类型不确定，可能是任意类型
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

//可以看到unique_lock的锁：
/*
      explicit lock_guard(mutex_type& __m) : _M_device(__m)
      { _M_device.lock(); }

      lock_guard(mutex_type& __m, adopt_lock_t) noexcept : _M_device(__m)
      { } // calling thread owns mutex

      ~lock_guard()
      { _M_device.unlock(); }
**/


#endif //THREADPOOL_H