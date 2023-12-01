#include <iostream>
#include <thread>
#include <numeric>
#include <memory>
// #include <unique_lock>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <list>
#include "test_any.h"

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


//=============================Result================================
Result::Result(std::shared_ptr<Task> task, bool isValid )
    : isValid_(isValid)
    , task_(task)
{
    task_->setResult(this);//Result* = this
}

Any Result::get() //用户执行的
{
    if (!isValid_)//得到Any的值
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



//==========================MyTask======================
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
    Any run() //run方法最终在线程池中分配的线程中执行
    {
        std::cout << "tid: " << std::this_thread::get_id() << "I love XWC----begin" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));//睡2s
        int sum = 0;
        for (int i = begin_; i <= end_; i++)
        {
            sum += i;
        }
        std::cout << "tid: " << std::this_thread::get_id() << "I love XWC ----end" << std::endl;
        return sum;
    }
private:
    int begin_;
    int end_;
};

int main() 
{
    // Create a task
    auto myTask = std::make_shared<MyTask>(1, 100);

    // Create a Result object for the task
    Result result(myTask);

    // Create a thread to simulate task execution and set the result
    std::thread taskThread([&]() {
        Any resultValue = myTask->run();
        int intValue = resultValue.cast_<int>();//intValue = 5050
        result.setVal(intValue);
    });

    // Create another thread to retrieve the result
    std::thread getResultThread([&]() {
        Any retrievedResult = result.get();
        int intValue = retrievedResult.cast_<int>();
        std::cout << "Result from task: " << intValue << std::endl;
    });

    // Join threads
    taskThread.join();
    getResultThread.join();
    return 0;
}