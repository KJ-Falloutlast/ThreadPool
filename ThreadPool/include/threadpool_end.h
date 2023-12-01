#include <iostream>
#include <functional>
#include <thread>
#include <future>
/*如何让线程池提交任务更加方便
1. pool.submit(sum1, 10, 20)
    pool.submit(sum2, 1, 2, 3)：接受无固定参数的参数
    submit:可变参模版参数
2. 自己造了Result以及相关类型，代码多
    C++线程库 thread  packaged_task(function函数对象) ---获取任务的返回值  async
    使用future代替Result节省代码
*/

int sum1(int a, int b)
{
    return a + b;
}

int sum2(int a, int b, int c)
{
    return a + b + c;
}

int main()
{
   /* std::thread t1(sum1, 10, 20);
    std::thread t2(sum2, 1, 2, 3);
    t1.join();
    t2.join();
    */
   
   //通过包装的任务获取返回值
   std::packaged_task<int(int, int)> task(sum1);
   //类似于Result, 它返回1个Result返回值的对象
   /*future <=>Result
    1.future    
            future<_Res> 
      get_future()
      { return future<_Res>(_M_state); }
    2. Result
    Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
    {return Result(sp);}
   */
   std::future<int> res = task.get_future();//get_future可以得到任务返回值
//    task(10, 20);
   std::thread t(task, 10, 20);
   std::cout << res.get() << std::endl;
}