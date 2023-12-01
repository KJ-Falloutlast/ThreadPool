#include <iostream>
#include <vector>
#include <functional>
#include <future>


class Task {
public:
    virtual ~Task() = default;
    virtual void execute() = 0;
    virtual void setResult(Result* result) {
        result_ = result;
    }

protected:
    Result* result_;
};

class ThreadPool {
public:
    ThreadPool(size_t numThreads) : stop_(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            threads_.emplace_back([this] {
                while (true) {
                    std::shared_ptr<Task> task = getNextTask();
                    if (!task) {
                        break; // No more tasks, exit the thread
                    }
                    task->execute();
                }
            });
        }
    }

    ~ThreadPool() {
        stop_ = true;
        for (std::thread& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    template <typename F, typename... Args>
    auto addTask(F&& f, Args&&... args) -> std::shared_ptr<Task> {
        auto task = std::make_shared<MyTask>(std::forward<Args>(args)...);
        task->setResult(new Result(task, true));

        std::function<void()> taskFunction = [task, f]() {
            Any result = f(); // Execute the task
            task->result_->setVal(std::move(result));
        };

        {
            std::unique_lock<std::mutex> lock(taskMutex_);
            tasks_.emplace_back(taskFunction);
        }

        taskSem_.post(); // Notify a waiting thread that a new task is available
        return task;
    }

private:
    std::shared_ptr<Task> getNextTask() {
        taskSem_.wait(); // Wait for a task to be available
        std::unique_lock<std::mutex> lock(taskMutex_);
        if (tasks_.empty()) {
            return nullptr;
        }
        std::shared_ptr<Task> task = tasks_.front();
        tasks_.pop_front();
        return task;
    }

private:
    std::vector<std::thread> threads_;
    std::list<std::function<void()>> tasks_;
    std::mutex taskMutex_;
    Semaphore taskSem_;
    std::atomic_bool stop_;
};

int main() {
    // Create a thread pool with 4 threads
    ThreadPool threadPool(4);

    // Add tasks to the thread pool
    auto task1 = threadPool.addTask([] {
        std::cout << "Task 1 running" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return 42;
    });

    auto task2 = threadPool.addTask([] {
        std::cout << "Task 2 running" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 100;
    });

    // Wait for tasks to complete and get results
    int resultValue1 = task1->result_->get().cast_<int>();
    int resultValue2 = task2->result_->get().cast_<int>();

    std::cout << "Result 1: " << resultValue1 << std::endl;
    std::cout << "Result 2: " << resultValue2 << std::endl;

    return 0;
}