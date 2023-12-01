#ifndef TEST_ANY_H
#define TEST_ANY_H
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
    //!核心所在， 利用模版+多态+智能指针，实现任意类型接受，派生类对象对基类对初始化
    //*这个构造函数可以让Any接受任意其他类型数据
    // Any(T data) : base_(new Derive <T>(data))
    template<typename T> 
    Any(T data) : base_(std::make_unique<Derive <T>>(data))
    { }
    //如果Base base_, 则base_ = Derive<T>(data), 
    //如果unique_ptr<Base> base_, 则base_  = unique_ptr<Derive<T>>(double)

    //能把Any对象存储的data数据提取出来
    template<typename T>// T:int  Derive<int>: int, cast_: long
    T cast_()
    {
        //我们怎么从base_找到它所指向的Derive对象，从它里面取出data成员变量
        //*基类指针 ----> 派生类类指针 RTTI, 因为Base类型指向Derive类型，所以能够用Dynamic_cast强转
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());//get()是得到base_内部指针
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

#endif //THREADPOOL_H