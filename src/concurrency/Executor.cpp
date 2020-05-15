#include <afina/concurrency/Executor.h>

#include <chrono>
#include <iostream>
#include <utility>

static void empty_task() {}

namespace Afina {
namespace Concurrency {

Executor::Executor(size_t low_wm, size_t hight_wm, size_t max_queue, size_t idle)
    : threads(0), idle(0), low_watermark(low_wm), hight_watermark(hight_wm), max_queue_size(max_queue),
      idle_time(idle){};

// All done in Stop()
Executor::~Executor(){};

void Executor::Start() {
    state = State::kRun;
    std::unique_lock<std::mutex> lock(mutex);
    for (size_t i = 0; i < low_watermark; ++i) {
        std::thread pool_thread(&perform, this);
        pool_thread.detach(); // no tasks yet
    }
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> lock(mutex);
    state = State::kStopping;

    if (await) {
        while (threads > 0) {
            cv_stop.wait(lock);
        }
    } else if (threads == 0) {
        state = State::kStopped;
    }
}

void perform(Executor *executor) {
    bool expired = false;
    while (!expired) // thread life cycle
    {
        std::function<void()> task = empty_task;
        {
            std::unique_lock<std::mutex> lock(executor->mutex); // to get task

            if (executor->state == Executor::State::kStopped) // if server changed state but haven't captured mutex yet
            {
                break;
            }

            auto timeout = std::chrono::system_clock::now() + std::chrono::milliseconds(executor->idle_time);

            while ((executor->tasks.empty()) &&
                   (executor->state == Executor::State::kRun)) // state: empty task queue - waiting...
            {
                if (executor->empty_condition.wait_until(lock, timeout) == std::cv_status::timeout &&
                    (executor->threads + executor->idle) > executor->low_watermark) {
                    //"killing" thread
                    expired = true;
                    break;
                }
            }

            if (!expired) {
                // state: expired but no tasks
                if (executor->tasks.empty()) {
                    continue;
                }
                task = executor->tasks.front();
                executor->tasks.pop_front();
                executor->threads++;
                executor->idle--;
            }
        }

        try {
            task(); // executing task
        } catch (std::exception &exc) {
            std::cerr << exc.what() << std::endl; // change to logger
        }

        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            if (!expired) {
                executor->idle++;
            }
            executor->threads--;

            if (executor->state == Executor::State::kStopping && executor->tasks.empty()) // stopping...
            {
                executor->cv_stop.notify_all();
                break;
            }
        }
    }
}

} // namespace Concurrency
} // namespace Afina
