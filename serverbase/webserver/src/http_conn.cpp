#include "http_conn.h"

// ********************全局API************************
// 设置 fd 非阻塞
bool Setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    int ret = fcntl(fd, F_SETFL, new_option);   // 返回 -1 表示失败
    if (ret == -1) {
        perror("set nonblock:");
        return false;
    }
    return true;
}

// 将 fd 添加到 epoll 实例中
bool Addfd(int epollfd, int fd, bool oneshot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (oneshot) {
        event.events |= EPOLLONESHOT;
    }
    // 使用 ET 模式，必须使用非阻塞 fd，封装设置文件非阻塞API
    if (!Setnonblocking(fd)) {
        return false;
    }
    int ret = 0;
    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    if (ret == -1) {
        perror("Addfd:");
        return false;
    }
    return true;
}

// 将 fd 从 epoll 实例中移除
bool Removefd(int epollfd, int fd) {
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0) == -1) {
        std::cout << "移除失败" << std::endl;
        return false;
    }
    return true;
}

// ********************HttpConn API************************
// 初始化静态变量
int HttpConn::m_epollfd_ = -1;
int HttpConn::m_user_count_ = 0;

// 初始化连接，初始化套接字和地址
bool HttpConn::Init(int fd, const sockaddr_in& addr) {
    // 初始化任务
    sockfd_ = fd;
    m_addr_ = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (!Addfd(HttpConn::m_epollfd_, sockfd_, true)) {
        std::cout << "客户端套接字加入epoll失败" << std::endl;
        return false;
    }
    HttpConn::m_user_count_++;
    Init();
    return true;
}

// 初始化连接，初始化http数据包信息
void HttpConn::Init() {
    // 初始化各种http数据包信息
}

// 关闭连接
bool HttpConn::CloseConn() {
    if (sockfd_ != -1) {
        // 从 epoll 移除 fd
        if (!Removefd(HttpConn::m_epollfd_, sockfd_)) {
            char ip[16];
            inet_ntop(AF_INET, &m_addr_.sin_addr.s_addr, ip, sizeof(ip));
            printf("客户端套接字 %d, IP : %s 初始化失败\n", sockfd_, ip);
            return false;
        }
        sockfd_ = -1;
        HttpConn::m_user_count_--;
    }
    return true;
}

// 从套接字中读取数据到缓冲中
bool HttpConn::Read() {
    std::cout << "一次性读完数据" << std::endl;
    return true;
}

// 从缓冲写入数据到套接字中
bool HttpConn::Write() {
    std::cout << "一次性写完数据" << std::endl;
    return true;
}

void HttpConn::Process() {
    // 解析http请求

    std::cout << "parse request, create response" << std::endl;

    // 生成响应
}