#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <iostream>

// 线程池类，定义成类模板，为了代码复用
template<class T>
class ThreadPool {
public:
    ThreadPool(int thread_number = 8, int max_requests = 10000);

    ~ThreadPool();

    bool Append(T* request);
private:
    static void* Worker(void* arg);
    void Run();
    int m_thread_number_;    // 线程数量
    pthread_t* m_threads_;   // 描述线性池的数组，大小为 m_thread_number
    bool m_stop_;            // 是否结束线程

    int m_max_requests_;     // 请求队列中最多允许的等待处理的的请求的数量
    std::list<T*> m_workqueue_;    // 请求队列
    Locker m_queuelocker_;   // 保护请求队列的互斥锁
    Sem m_queuestat_;        // 是否有待处理的任务
};

// 定义实现
// 初始化线程池，创建给定数目的线程并分离线程
template<class T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests)
    : m_thread_number_(thread_number),
      m_max_requests_(max_requests),
      m_stop_(false),
      m_threads_(NULL) {
    
    // 输入的参数异常
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    // 创建 m_thread_number_ 个线程ID
    m_threads_ = new pthread_t[m_thread_number_];
    if (!m_threads_) {
        throw std::exception();
    }

    // 创建 thread_number 个线程，并将他们设置为脱离线程
    for (int i = 0; i < m_thread_number_; ++i) {
        std::cout << "create the " << i << "th thread" << std::endl;
        // 创建线程
        if (pthread_create(m_threads_ + i, NULL, Worker, this) != 0) {
            delete [] m_threads_;
            throw std::exception();
        }
        // 线程分离
        if (pthread_detach(m_threads_[i]) != 0) {
            delete [] m_threads_;
            throw std::exception();
        }
    }
}

template<class T>
ThreadPool<T>::~ThreadPool() {
    delete [] m_threads_;
    m_stop_ = true;
}

// 添加新的请求到队列中 主线程使用
template<class T>
bool ThreadPool<T>::Append(T* request) {
    // 操作队列是需要加锁，因为它是被所有线程共享的
    m_queuelocker_.Lock();
    if (m_workqueue_.size() > m_max_requests_) {
        // 队列不为空，
        m_queuelocker_.unLock();
        return false;
    }
    m_workqueue_.push_back(request);
    m_queuelocker_.unLock();
    m_queuestat_.Post();    // 队列资源 + 1
    return true;
}

// 因为是回调函数，所以是静态的，因此要处理某个线性池时需要传入对应的 this 指针
// 作为一个汇总，去调用每个线程特定的业务逻辑（有人说时为了少些 pool-> ）
template<class T>
void* ThreadPool<T>::Worker(void* arg) {
    ThreadPool* pool = (ThreadPool*) arg;
    pool->Run();
    return pool;
}

// 处理函数
template<class T>
void ThreadPool<T>::Run() {
    while (1) {
        // 等待信号量资源 和 从队列中取出待处理的请求
        m_queuestat_.Wait();
        m_queuelocker_.Lock();
        if (m_workqueue_.empty()) {
            m_queuelocker_.unLock();
            continue;
        }
        T* request = m_workqueue_.front();
        m_workqueue_.pop_front();
        m_queuelocker_.unLock();
        if (!request) {
            continue;
        }
        // 获取资源后处理资源
        request->Process();

    }
}

#endif