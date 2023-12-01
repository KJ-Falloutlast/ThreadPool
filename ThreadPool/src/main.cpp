#include <threadpool.h>
#include  <iostream>
#include <chrono>
#include <thread>
using uLong = unsigned long long;
/*
有些场景，希望能够获取执行任务得返回值
举例子：
1 + ...+30000
thread1 1+...+10000
thread1 10001+...+20000
...
main thread: 给每个线程分配计算的区间，并等待他们 算完返回结果，合并最终的结果即可
*/
class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end)
    {}
    //?问题1：如何设计run返回值接受任意类型,因为Task的基类vitual不能和模版一起
    //Java Python的object是所有其他类类型的基类
    //c++17 Any
    Any run()  // run方法最终就在线程池分配的线程中去做执行了!
    {
        std::cout << "tid:" << std::this_thread::get_id()
            << "begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++)
            sum += i;
        std::cout << "tid:" << std::this_thread::get_id()
            << "end!" << std::endl;

        return sum;
    }

private:
    int begin_;
    int end_;
};



int main()
{
    {
        //设计：当线程池ThreadPool出作用域析构时，此时任务队列里面如果还有任务，是等任务全部执行完成，再结束；还是不执行剩下的任务
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start (2);//2个线程
        //?为什么会有std::cout <<  ">>> create new thread" << std::endl, 是因为2个线程，但是有5个任务，所以才会创建新的3个线程
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100));//提交1次任务，run()才会执行1次
        Result res2 = pool.submitTask(std::make_shared<MyTask>(101, 200));//提交1次任务，run()才会执行1次
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        uLong sum1 = res1.get().cast_<int>();
        std::cout << "sum1 =  " << sum1 << " "<< std::endl;
        /*4个线程， 1个任务， 则1个线程获取1个任务
        tid:139901598549760尝试获取任务...
        tid:139901590157056尝试获取任务...
        tid:139901581764352尝试获取任务...
        tid:139901573371648尝试获取任务...
        tid:139901590157056获取任务成功... 
        sum1 =  5050 
        main over!
        */
    }//pool析构的时候, 不能出了作用域就不执行了！！
    std::cout << "main over!" << std::endl;

/*
#if 0
    //ThreadPool对象析构后，怎么样把线程池相关线程资源回收
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start(4);//4个线程
    
        // linux上，这些Result对象也是局部对象，要析构的！！！
        //总共6个任务
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(300000001, 400000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        int sum1 = res1.get().cast_<int>();  // get返回了一个Any类型，怎么转成具体的类型呢？
        int sum2 = res2.get().cast_<int>();
        int sum3 = res3.get().cast_<int>();
        //int sum1 = res1.get().cast_<int>();
        std::cout << "sum = "  << sum1 << std::endl; 
    } // 这里Result对象和ThreadPool也要析构!!! 在vs下，条件变量析构会释放相应资源的
    
    std::cout << "main over!" << std::endl;
    getchar();
#endif
    // // 问题：ThreadPool对象析构以后，怎么样把线程池相关的线程资源全部回收？
    // {
    //     ThreadPool pool;
    //     // 用户自己设置线程池的工作模式
    //     pool.setMode(PoolMode::MODE_CACHED);
    //     // 开始启动线程池
    //     pool.start(4);

    //     // 如何设计这里的Result机制呢
    //     Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
    //     Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
    //     Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    //     pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

    //     pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    //     pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

    //     // 随着task被执行完，task对象没了，依赖于task对象的Result对象也没了
    //     int sum1 = res1.get().cast_<int>();  // get返回了一个Any类型，怎么转成具体的类型呢？
    //     int sum2 = res2.get().cast_<int>();
    //     int sum3 = res3.get().cast_<int>();

    //     // Master - Slave线程模型
    //     // Master线程用来分解任务，然后给各个Slave线程分配任务
    //     // 等待各个Slave线程执行完任务，返回结果
    //     // Master线程合并各个任务结果，输出
    //     std::cout << (sum1 + sum2 + sum3) << std::endl;
    // }
    


    // int sum = 0;
    // for (int i = 1; i <= 300000000; i++)
    //     sum += i;
    // std::cout << sum << std::endl;
 */
    /*pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());*/

    getchar();

}