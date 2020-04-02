#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Afina {
namespace Concurrency {

/**
 * # Thread pool
 */
class Executor;
void perform(Executor* executer);

class Executor {

    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

public:
    Executor(std::string name, size_t low_wm, size_t high_wm, size_t max_queue, size_t idle);
    ~Executor();

    /**
      * Threadpool on start
      */
    void Start();

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false);

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
        // Prepare "task"
        auto task = std::bind(std::forward<F>(func), std::forward<Types>(args)...);
        {
            std::unique_lock<std::mutex> lock(this->mutex);
            if (tasks.size() >= max_queue_size || state != State::kRun)
            {
                return false;
            }

            // Enqueue new task
            tasks.push_back(task);

            //No idle threads from pool
            if (idle == 0  && threads < hight_watermark)
            {
                std::thread pool_thread(&perform, this);
                pool_thread.detach();//detaching thread here - tasks are void
                idle++;
            }
        }

        //New task in queue
        empty_condition.notify_one();
        return true;
    }

    void setName(std::string name);

    std::string getName();

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    friend void perform(Executor *executor);

    /**
     * Mutex to protect state below from concurrent modification
     */
    std::mutex mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;

    /**
     * Vector of actual threads that perform execution
     */
//    std::vector<std::thread> threads;

    //Using counter instead of thread vector - our tasks are "void", so don't have to store them - can just detach at once

    /**
     * Threads
     */
    size_t threads;

    /**
     * Currently idle threads
     */
    size_t idle;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks;

    /**
     * Flag to stop bg threads
     */
    State state;

    //Params

    std::string name;

    size_t low_watermark;

    size_t hight_watermark;

    size_t max_queue_size;

    size_t idle_time;

    //Stop cv(synthronizing server and client threads stopping)
    std::condition_variable cv_stop;
};
} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
