// 使用#ifndef可以防止头文件重复包含
#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <errno.h>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class Locker {
public:
    Locker() {
        // 初始化互斥量
        if (pthread_mutex_init(&my_mutex_, NULL) != 0) {
            throw std::exception(); // 如果初始化失败，抛出异常
        }
    }

    ~Locker() {
        // 释放互斥量
        pthread_mutex_destroy(&my_mutex_);
    }

    // 加锁
    bool Lock() {
        return pthread_mutex_lock(&my_mutex_) == 0;
    }
    // 解锁
    bool unLock() {
        return pthread_mutex_unlock(&my_mutex_);
    }

private:
    pthread_mutex_t my_mutex_;   // 创建互斥量
};


// 条件变量
class Cond {
public:
    Cond() {
        if (pthread_cond_init(&my_cond_, NULL) != 0) {
            throw std::exception();
        }
    }

    ~Cond() {
        pthread_cond_destroy(&my_cond_);
    }

    /*
    1. 返回0,表示线程被成功唤醒。
    2. 返回EINTR,表示等待被一个信号中断唤醒。
    3. 返回EPERM,表示没有权限等待该条件变量。
    4. 返回EINVAL,表示条件变量或互斥锁不可用
    使用 int 类型更加合适
    */
    bool Wait(pthread_mutex_t* mutex) {
        int ret;
        // 如果返回为EINTR并不是错误，表示被信号打断，需要继续等待
        while ((ret = pthread_cond_wait(&my_cond_, mutex)) == EINTR) {
            continue;
        }
        return ret == 0;
    }

    bool TimedWait(pthread_mutex_t* mutex, struct timespec time) {
        int ret;
        while ((ret = pthread_cond_timedwait(&my_cond_, mutex, &time)) == EINTR) {
            continue;
        }
        return ret == 0;
    }

    bool Signal() {
        return pthread_cond_signal(&my_cond_) == 0;
    }

    bool BroadCast() {
        return pthread_cond_broadcast(&my_cond_) == 0;
    }

private:
    pthread_cond_t my_cond_;
};


class Sem {
public:
    Sem() {
        if (sem_init(&my_sem_, 0, 0) != 0) {
            throw std::exception();
        }
    }
    Sem(int num) {
        if (sem_init(&my_sem_, 0, num) != 0) {
            throw std::exception();
        }
    }

    ~Sem() {
        sem_destroy(&my_sem_);
    }

    // 返回 0 表示正常返回，返回 -1，且 errno != EINTR 表示是错误
    bool Wait() {
        int ret;
        while ((ret = sem_wait(&my_sem_)) == -1 && errno == EINTR) {
            continue;
        }
        return ret == 0;
    }

    bool Post() {
        return sem_post(&my_sem_) == 0;
    }
    
private:
    sem_t my_sem_;
};
#endif

