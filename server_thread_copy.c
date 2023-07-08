#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/*
    1、创建套接字
    2、绑定ip和端口
    3、监听
    4、接收连接
    5、创建子线程
    6、子线程处理业务
*/

// 创建打包结构体
struct sockinfo {
    pthread_t tid;  // 子线程id号
    struct sockaddr_in addr;    // 通信套接字
    int cfd;    // 描述符
};

// 创建全局的sockinfo
struct sockinfo infos[128];

// 子线程处理业务
void* working(void* arg) {
    // 子线程和客户端通讯
    struct sockinfo* pinfo = (struct sockinfo*) arg;

    char cliIp[16];
    inet_ntop(AF_INET, &(pinfo->addr.sin_addr.s_addr), cliIp, sizeof(cliIp));
    unsigned short cliPort = ntohs(pinfo->addr.sin_port);
    printf("client ip : %s, port: %d\n", cliIp, cliPort);

    // 接收客户端发来的信息
    char recvBuf[1024];
    while(1) {
        int len = read(pinfo->cfd, recvBuf, sizeof(recvBuf));
        if (len == -1) {
            perror("read:");
            exit(-1);
        } else if (len > 0) {
            printf("recv client : %s\n", recvBuf);
        } else if (len == 0) {
            printf("client close...\n");
            break;
        }

        write(pinfo->cfd, recvBuf, strlen(recvBuf) + 1);
    }
    
    close(pinfo->cfd);
    return NULL;
}

int main() {

    // 创建套接字
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        perror("socket:");
        exit(-1);
    }

    // 绑定端口和ip
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    // inet_pton(AF_INET, "100.120.245.136", &(serverAddr.sin_addr.s_addr));
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9999);
    bind(sfd, (struct sockaddr*)&serverAddr, (socklen_t)sizeof(serverAddr));

    // 监听端口
    int lret = listen(sfd, 128);
    if (lret == -1) {
        perror("listen");
        exit(-1);
    }

    // 初始化传入的套接字结构 允许创建的子线程为128个
    int max = sizeof(infos) / sizeof(infos[0]);
    for (int i = 0; i < max; ++i) {
        memset((struct sockinfo*)&infos[i], 0, sizeof(infos[i]));
        infos[i].cfd = -1;    // 表示可用
        infos[i].tid = -1;     // 可用线程
    }

    // accept 接收连接
    while(1) {
        struct sockaddr_in cliAddr;
        int len = sizeof(cliAddr);
        int cfd = accept(sfd, (struct sockaddr*)&cliAddr, (socklen_t*)&len);
        if (cfd == -1) {
            perror("accept:");
            exit(-1);
        }

        // 找出一个可用的 sockinfo 结构
        struct sockinfo* pinfo;
        for (int i = 0; i < max; ++i) {
            if (infos[i].cfd == -1) {
                pinfo = &infos[i];
                break;
            } 
            if (i == max - 1) {
                sleep(1);
                i = -1;
            }
        }

        // 传入信息
        pinfo->cfd = cfd;
        memcpy(&(pinfo->addr), &cliAddr, len);

        // 创建子线程
        // 面向对象的思想开发：采用 struct 结构将需要传入的参数打包：比如客户端的ip和端口信息，子线程id，与客户端通信的端口描述符等
        pthread_create(&(pinfo->tid), NULL, working, pinfo);

        // 分离子线程
        pthread_detach(pinfo->tid);


    }

    close(sfd);
    return 0;
}