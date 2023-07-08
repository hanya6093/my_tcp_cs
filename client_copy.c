#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


/*
    1、创建套接字
    3、连接服务器：绑定服务端IP和端口
    4、业务处理
    5、关闭套接字
*/
int main() {

    // 创建套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket:");
        exit(-1);
    }

    // 连接服务器
    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    inet_pton(AF_INET, "100.120.245.136", &(serveraddr.sin_addr.s_addr));
    serveraddr.sin_port = htons(9999);

    int ret = connect(fd, (const struct sockaddr*)&serveraddr, (socklen_t)sizeof(serveraddr));
    if (ret == -1) {
        perror("connect:");
        exit(-1);
    }

    char recvBuf[1024];
    int i = 1;
    // 业务处理
    while (1) {
        memset(recvBuf, 0, 1024);
        sprintf(recvBuf, "data : %d\n", i++);

        write(fd, recvBuf, strlen(recvBuf) + 1);
        
        int len = read(fd, recvBuf, sizeof(recvBuf));
        if (len == -1) {
            perror("read");
            exit(-1);
        } else if (len > 0) {
            printf("recv server : %s\n", recvBuf);
        } else if (len == 0) {
            printf("server closed...");
            break;
        }

        sleep(1);

    }

    // 关闭连接
    close(fd);


    return 0;
}