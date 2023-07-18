#include "lst_timer.h"
#include "http_conn.h"

void cb_func(HttpConn* user_data) {
    user_data->CloseConn();
}

Util_Timer::Util_Timer(time_t cur, HttpConn* user_data, void cbfun(HttpConn*)) {
    expire_ = cur + 3 * TIMESLOT;
    user_data_ = user_data;
    cb_func_ = cbfun;
}

Sort_Timer_List::Sort_Timer_List() : head_(nullptr), tail_(nullptr) {

}

// 销毁队列
Sort_Timer_List::~Sort_Timer_List() {
    Util_Timer* temp = head_;
    while (temp) {
        head_ = head_->next_;
        delete temp;
        temp = head_;
    }
}      

// 添加定时器到队列中
void Sort_Timer_List::Add_Timer(Util_Timer* timer) {
    // 如果 timer == nullptr 直接返回
    if (!timer) {
        return;
    }
    
    //  如果队列为空
    if (!head_) {
        head_ = tail_ = timer;
        return;
    }
    
    // 如果队列不为空 如果是最小超时时间，那么放在队列头部，否则调用 Add_Timer 重载加入到队列合适位置
    if (timer->expire_ < head_->expire_) {
        timer->next_ = head_;
        head_->prev_ = timer;
        head_ = timer;
        return;
    } else {
        Add_Timer(timer, head_);
    }
}

// 修改定时器在队列中的位置 只考虑超时时间延长的情况
void Sort_Timer_List::Adjust_Timer(Util_Timer* timer) {
    if (!timer) {
        return;
    }

    // 如果目标就是在尾部或者仍然比下一个节点的超时时间小，那么不用调整
    if (!timer->next_ || (timer->expire_ < timer->next_->expire_)) return;

    // 如果目标时间是定时器的头节点，则移动该定时间
    if (timer == head_) {
        head_ = head_->next_;
        head_->prev_ = nullptr;
        timer->next_ = nullptr;
        Add_Timer(timer, head_);
    } else {
        // 不是头节点，那么直接处理，但是需要修改它前后节点的值
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
        Add_Timer(timer, head_);
    }

}   

// 删除在队列中定时器
void Sort_Timer_List::Del_Timer(Util_Timer* timer) {
    if (!timer) {
        return;
    }

    // 如果队列只有一个该定时器
    if ((timer == head_) && (timer == tail_)) {
        delete timer;
        head_ = nullptr;
        tail_ = nullptr;
        return;
    } else if (timer == head_){
        // 位于头部 且至少两个节点
        head_ = head_->next_;
        head_->prev_ = nullptr;
        delete timer;
        return;
    } else if (timer == tail_) {
        // 位于尾部，且至少两个节点
        tail_ = tail_->prev_;
        tail_->next_ = nullptr;
        delete timer;
        return;
    } else {
        // 位于中间
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
        delete timer;
        return;
    }

}         

// SIGALARM 信号被触发时就调用一次 tick 函数，处理队列中到期任务
void Sort_Timer_List::Tick() {
    if (!head_) {
        return;
    }

    std::cout << "timer tick" << std::endl;

    time_t cur = time(nullptr);         // 获取当前系统时间
    Util_Timer* temp = head_;            // 遍历节点，知道遇到一个尚未到期的定时器
    while (temp) {
        // 如果找到了大于当前时间的节点，那么退出循环
        if (cur < temp->expire_) {
            break;
        }
        // 定时器回到函数，执行定时任务
        temp->cb_func_(temp->user_data_);
        head_ = temp->next_;
        if (head_) {
            head_->prev_ = nullptr;
        }
        delete temp;
        temp = head_;
    }

}               

// 重载，将目标定时器添加到节点 lst_head_ 之后的部分链表中
void Sort_Timer_List::Add_Timer(Util_Timer* timer, Util_Timer* lst_head_) {
    Util_Timer* prev_ = lst_head_;
    Util_Timer* temp = prev_->next_;

    // 遍历队列，直到找到一个超时时间大于该节点的超时时间的节点，插入在这个节点前面
    while (temp) {
        if (timer->expire_ < temp->expire_) {
            // 插入到 prev_ 和 temp 之间
            prev_->next_ = timer;
            timer->next_ = temp;
            temp->prev_ = timer;
            timer->prev_ = prev_;
            break;
        }
        prev_ = temp;
        temp = temp->next_;
    }
    
    if (!temp) {
        prev_->next_ = timer;
        timer->prev_ = prev_;
        timer->next_ = nullptr;
        tail_ = timer;
    }
}
