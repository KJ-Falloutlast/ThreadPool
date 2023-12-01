# ThreadPool

## 项目名换：基于可变参数模版实现的线程池
1. github: https://github.com/KJ-Falloutlast/ThreadPool.git
2. 工具：vscode开发，gdb调试分析死锁问题

## 项目描述
1. 基于可变参模版编程和引用折叠原理，实现线程池submitTask接口，支持任意函数和任意参数的传递
2. 使用future类型定制submitTask提交任务的返回值
3. 使用map和queue管理任务和对象
4. 基于condition_variable和mutex实现任务提交和任务执行线程的通信

## 项目整体流程
![image-20231201084534118](https://obsidians-pics.oss-cn-beijing.aliyuncs.com/image-20231201084534118.png)
