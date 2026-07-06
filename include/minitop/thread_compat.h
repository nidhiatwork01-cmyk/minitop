#ifndef MINITOP_THREAD_COMPAT_H
#define MINITOP_THREAD_COMPAT_H

#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#else
#include <thread>
#include <mutex>
#include <condition_variable>
#endif

namespace minitop {

#ifdef _WIN32
// Slim Reader/Writer Mutex wrapper for Windows
class SrwMutex {
public:
    SrwMutex() {
        InitializeSRWLock(&lock_);
    }
    void lock() {
        AcquireSRWLockExclusive(&lock_);
    }
    void unlock() {
        ReleaseSRWLockExclusive(&lock_);
    }
    SRWLOCK* native_handle() {
        return &lock_;
    }
    SRWLOCK* get_lock() {
        return &lock_;
    }
private:
    SRWLOCK lock_;
};

// Unique lock / Lock Guard wrapper using SRWLock
class SrwLockGuard {
public:
    explicit SrwLockGuard(SrwMutex& m) : m_(m) {
        m_.lock();
    }
    ~SrwLockGuard() {
        m_.unlock();
    }
private:
    SrwMutex& m_;
};

// Condition Variable wrapper using Windows APIs
class ConditionVariable {
public:
    ConditionVariable() {
        InitializeConditionVariable(&cv_);
    }
    void wait(SrwMutex& m) {
        SleepConditionVariableSRW(&cv_, m.get_lock(), INFINITE, 0);
    }
    void wait_for(SrwMutex& m, int ms) {
        SleepConditionVariableSRW(&cv_, m.get_lock(), ms, 0);
    }
    void notify_one() {
        WakeConditionVariable(&cv_);
    }
    void notify_all() {
        WakeAllConditionVariable(&cv_);
    }
private:
    CONDITION_VARIABLE cv_;
};

#else
// POSIX / C++11 aliases
using SrwMutex = std::mutex;
using SrwLockGuard = std::unique_lock<std::mutex>;

class ConditionVariable {
public:
    void wait(SrwMutex& m) {
        std::unique_lock<std::mutex> lk(m, std::adopt_lock);
        cv_.wait(lk);
        lk.release(); // Keep lock held on return to match SRW behavior
    }
    void wait_for(SrwMutex& m, int ms) {
        std::unique_lock<std::mutex> lk(m, std::adopt_lock);
        cv_.wait_for(lk, std::chrono::milliseconds(ms));
        lk.release();
    }
    void notify_one() {
        cv_.notify_one();
    }
    void notify_all() {
        cv_.notify_all();
    }
private:
    std::condition_variable cv_;
};
#endif

// Cross-platform thread creation wrapper
#ifdef _WIN32
inline void* SpawnThread(unsigned long (__stdcall *func)(void*), void* arg) {
    return CreateThread(NULL, 0, func, arg, 0, NULL);
}
inline void JoinThread(void* handle) {
    if (handle) {
        WaitForSingleObject(handle, INFINITE);
        CloseHandle(handle);
    }
}
#else
inline void* SpawnThread(void* (*func)(void*), void* arg) {
    return new std::thread(func, arg);
}
inline void JoinThread(void* handle) {
    if (handle) {
        std::thread* t = static_cast<std::thread*>(handle);
        if (t->joinable()) t->join();
        delete t;
    }
}
#endif

} // namespace minitop

#endif // MINITOP_THREAD_COMPAT_H
