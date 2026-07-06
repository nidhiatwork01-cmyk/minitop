#ifndef MINITOP_TASK_SCHEDULER_H
#define MINITOP_TASK_SCHEDULER_H

#include <functional>
#include <queue>
#include <vector>
#include <chrono>
#include <iostream>
#include "minitop/thread_compat.h"

namespace minitop {

// Represents a scheduled job with execution time and priority
struct ScheduledTask {
    std::function<void()> job;
    unsigned long long run_at_ms; // Target timestamp in milliseconds
    int priority;                 // Higher value runs first if timestamps are identical
    std::string task_name;        // Optional name for logging

    bool operator<(const ScheduledTask& other) const {
        if (run_at_ms == other.run_at_ms) {
            return priority < other.priority; // Larger priority floats to top
        }
        return run_at_ms > other.run_at_ms; // Smaller timestamp floats to top (soonest first)
    }
};

// Thread-safe task scheduler component
class TaskScheduler {
public:
    TaskScheduler() : running_(false), worker_thread_(nullptr) {}
    ~TaskScheduler() {
        Stop();
    }

    // Starts the background task runner thread
    void Start() {
        if (running_) return;
        running_ = true;
        worker_thread_ = SpawnThread(SchedulerThreadEntry, this);
    }

    // Stops the worker thread and discards remaining tasks
    void Stop() {
        if (!running_) return;
        {
            SrwLockGuard lock(mutex_);
            running_ = false;
            cv_.notify_all();
        }
        JoinThread(worker_thread_);
        worker_thread_ = nullptr;
    }

    // Schedules a task to run after a delay (in milliseconds)
    void Schedule(const std::string& name, std::function<void()> job, int delay_ms, int priority = 0) {
        unsigned long long run_at = GetCurrentTimeMs() + delay_ms;
        {
            SrwLockGuard lock(mutex_);
            tasks_.push({job, run_at, priority, name});
            cv_.notify_one();
        }
    }

    // Returns current system time in milliseconds
    static unsigned long long GetCurrentTimeMs() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    // Core worker loop
    void Run() {
        SrwLockGuard lock(mutex_);
        while (running_) {
            if (tasks_.empty()) {
                cv_.wait(mutex_);
            } else {
                ScheduledTask current = tasks_.top();
                unsigned long long now = GetCurrentTimeMs();

                if (now >= current.run_at_ms) {
                    tasks_.pop();
                    
                    // Release mutex while executing the job to prevent deadlock
                    mutex_.unlock();
                    try {
                        current.job();
                    } catch (...) {
                        // Suppress job exception to keep the scheduler thread alive
                    }
                    mutex_.lock();
                } else {
                    int wait_ms = static_cast<int>(current.run_at_ms - now);
                    cv_.wait_for(mutex_, wait_ms);
                }
            }
        }
    }

private:
#ifdef _WIN32
    static unsigned long __stdcall SchedulerThreadEntry(void* param) {
        static_cast<TaskScheduler*>(param)->Run();
        return 0;
    }
#else
    static void* SchedulerThreadEntry(void* param) {
        static_cast<TaskScheduler*>(param)->Run();
        return nullptr;
    }
#endif

    bool running_;
    void* worker_thread_;
    std::priority_queue<ScheduledTask> tasks_;
    SrwMutex mutex_;
    ConditionVariable cv_;
};

} // namespace minitop

#endif // MINITOP_TASK_SCHEDULER_H
