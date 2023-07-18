#ifndef LST_TIMER_H
#define LST_TIMER_H

// #include "http_conn.h"
#include <time.h>

#define TIMESLOT 5        // 时间槽

class HttpConn;

// 定时器类
class Util_Timer {
public:
    Util_Timer() : prev_(nullptr), next_(nullptr) {}
    Util_Timer(time_t cur, HttpConn* user_data, void cbfun(HttpConn*));

    void (*cb_func_)(HttpConn*);     // 任务回调函数指针
    time_t expire_;                  // 任务超时时间，采用绝对时间
    HttpConn* user_data_;            // 用户数据结构
    Util_Timer* prev_;               // 指向前一个定时器
    Util_Timer* next_;               // 指向后一个定时器
};


// 定时器有序队列, 采用链表（可采用最小堆设计）
// 超时时间定义：采用绝对时间。队列中超时时间从小到大排序，超时时间越小，表明越容易达到需要关闭的时间点（当前时间 > 超时间时间）
class Sort_Timer_List {
public:
    Sort_Timer_List();

    ~Sort_Timer_List();         // 销毁队列
    
    void Add_Timer(Util_Timer* timer);           // 添加定时器到队列中

    void Adjust_Timer(Util_Timer* timer);        // 修改定时器在队列中的位置

    void Del_Timer(Util_Timer* timer);           // 删除在队列中定时器

    void Tick();                // SIGALARM 信号被触发时就调用一次 tick 函数，处理队列中到期任务
private:
    // 重载，将目标定时器添加到节点 lst_head_ 之后的部分链表中
    void Add_Timer(Util_Timer* timer, Util_Timer* lst_head);

    Util_Timer* head_;   // 头节点
    Util_Timer* tail_;   // 尾节点
};

#endif