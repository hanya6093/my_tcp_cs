#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
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

#define MAX_FD 65536    // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // epoll可返回的套接字个数

/*
    1、判断是否正常启动，参数是否正常
    2、处理信号，SIGPIPE
    3、线性池初始化
    4、创建任务数组
        4.1、构建任务类
    5、创建套接字、设置端口复用、绑定、监听
    6、创建epoll实例
        6.1、创建epoll_event，可返回的事件的个数
        6.2、创建epoll实例
        6.3、连接HttpConn与epoll
    7、将监听套接字加入epoll实例中
    8、开始监听epoll事件 epoll_wait
    9、循环返回的epoll_event
        9.1、如果时监听套接字，初始化任务实例，并将任务实例和客户端套接字绑定，并将套接字加入到监听epoll中
        9.2、如果时客户端套接字事件，检测epoll事件是什么类型，不同类型采用不同处理
            —— EPOLLRDHUP,EPOLLHUP,EPOLLERR 直接关闭连接并释放任务实例
            —— EPOLLIN 读事件，读取信息，将任务加队列中等待子线程处理
            —— EOPOLLOUT 写事件，写数据
    10、关闭各种文件描述符
*/

void AddSig(int sig, void(handler)(int)) {
    // 在形参声明函数指针类型时,不需要在名称前加*。
    // 如果是在函数内部定义函数指针变量,才需要在变量名前加*
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

extern bool Addfd(int epollfd, int fd, bool oneshot);
extern bool Removefd(int epollfd, int fd);

int main(int argc, char* argv[]) {

    // 1、判断是否正常启动，参数是否正常
    if (argc <= 1) {
        printf("usage: %s port_number\n", argv[0]);
        exit(1);
    }
    // 得到端口
    int port = atoi(argv[1]);

    // 2、处理信号，SIGPIPE - 如果读端关闭，继续写入就会发出该信号
    // 需要将一个特殊信号忽略 SIGPIPE，防止客户端异常断开导致服务器中断
    // 同时考虑后面还有信号处理，直接分装一个信号处理函数
    AddSig(SIGPIPE, SIG_IGN);

    // 3、线性池初始化
    ThreadPool<HttpConn>* pool = nullptr;
    // 接收 ThreadPoll 抛出得异常
    try {
        pool = new ThreadPool<HttpConn>;
    } catch(...) {  // ... 表示什么异常都接收
        exit(1);
    }

    // 4、创建任务数组
    //     4.1、构建任务类
    HttpConn* users = new HttpConn[MAX_FD];

    // 5、创建套接字、设置端口复用、绑定、监听
    int listenfd = 0;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket:");
        exit(-1);
    }
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));  // 端口复用
    int ret;
    if ((ret = bind(listenfd, (sockaddr*)&address, sizeof(address))) == -1) {
        perror("bind:");
        exit(-1);
    }
    if ((ret = listen(listenfd, 128)) == -1) {
        perror("listen:");
        exit(-1);
    }

    // 6、创建epoll实例
    //      6.1、创建epoll_event，可返回的事件的个数
    //      6.2、创建epoll实例
    //      6.3、连接HttpConn与epoll
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(1);
    HttpConn::m_epollfd_ = epollfd;

    // 7、将监听套接字加入epoll实例中
    // 分装成一个函数，方便后续调用
    if (!Addfd(epollfd, listenfd, false)) {
        std::cout << "add listenfd fail" << std::endl;
        exit(-1);
    }

    while (true) {
        // 8、开始监听epoll事件 epoll_wait
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        // number < 0 && errno != EINTR 表示失败，且不是被某些信号中断的失败
        if ((number < 0) && (errno != EINTR)) {
            std::cout << "epoll failture" << std::endl;
            break;
        }

        // 9、循环返回的epoll_event
        for (int i = 0; i < number; ++i) {
            // 9.1、如果时监听套接字，初始化任务实例，并将任务实例和客户端套接字绑定，并将套接字加入到监听epoll中
            int curfd = events[i].data.fd;
            if(curfd == listenfd) {
                sockaddr_in cliaddr;
                int len = sizeof(cliaddr);
                int clifd = accept(curfd, (sockaddr*)&cliaddr, (socklen_t*)&len);
                // 获取失败
                if (clifd < 0) {
                    perror("accept:");
                    continue;
                }
                // 判断客户端数目有没有超过上限 超过就放弃这个连接
                if (HttpConn::m_user_count_ > MAX_FD) {
                    close(clifd);
                    continue;
                }
                // 指定一个任务初始化
                if (!users[clifd].Init(clifd, cliaddr)) {
                    char ip[16];
                    inet_ntop(AF_INET, &cliaddr.sin_addr.s_addr, ip, sizeof(ip));
                    printf("客户端套接字 %d, IP : %s 初始化失败\n", clifd, ip);
                    continue;
                }
            //9.2、如果时客户端套接字事件，检测epoll事件是什么类型，不同类型采用不同处理
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // —— EPOLLRDHUP,EPOLLHUP,EPOLLERR 直接关闭连接并释放任务实例
                // 如果是事件显示错误，关闭这个连接任务
                while (!users[events[i].data.fd].CloseConn());
            } else if (events[i].events & EPOLLIN) {
                // —— EPOLLIN 读事件，读取信息，将任务加队列中等待子线程处理
                if (users[curfd].Read()) {
                    pool->Append(users + curfd);
                } else {
                    users[curfd].CloseConn();
                }
            } else if (events[i].events & EPOLLOUT) {
                // —— EOPOLLOUT 写事件，写数据
                // epoll在返回事件时,会返回所有就绪的事件,不仅限于注册的事件。
                // 即使未注册EPOLLOUT,如果写入就绪,返回的events也会包含EPOLLOUT。
                // 所以可以通过判断events是否包含EPOLLOUT,来知道是否可写,实现未注册写事件的判断。
                if (!users[curfd].Write()) {
                    users[curfd].CloseConn();
                }
            }
        }
    }
    // 10、关闭各种文件描述符
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}