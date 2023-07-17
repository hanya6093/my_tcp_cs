#ifndef HEEP_CONN_H
#define HTTP_CONN_H

#include "locker.h"
#include "threadpool.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/epoll.h>
/*
    任务处理函数

*/

class HttpConn {
public:
    HttpConn(){}
    ~HttpConn(){}
    bool Init(int fd, const sockaddr_in& addr); // 初始化任务的fd和地址等
    void Process();     // 任务处理
    bool CloseConn();   // 关闭连接
    bool Read();        // 读取数据
    bool Write();       // 写入数据
    static int m_epollfd_;       // 所有注册到epoll中得任务
    static int m_user_count_;    // 用户数量
private:
    void Init();    // 初始化http数据包信息
    int sockfd_;             // 任务对应的套接字
    sockaddr_in m_addr_;     // 客户端地址
};

#endif