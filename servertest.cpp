/*

*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

using namespace std;
#define  MAXLINE 1024

int main(int argc, char* argv[]) {
    // 1、创建一个监听 socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        fprintf(stderr, "socket error : %s\n\a", strerror(errno));
        return -1;
    }
    // 2、初始化服务器地址和端口
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8888);

    // 3、绑定地址+端口
    if (bind(listenfd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr)) < 0) {
        fprintf(stderr, "bind error : %s\n\a", strerror(errno));
        return -1;
    }
    cout << "begin listen..." << endl;

    // 4、开始监听
    if (listen(listenfd, 128)) {
        fprintf(stderr, "listen error : %s\n\a", strerror(errno));
        exit(0);
    }

    // 5、获取已经连接的socket
    struct sockaddr_in client_addr;
    socklen_t client_addrlen = sizeof(client_addr);
    int clientfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addrlen);
    if (clientfd < 0) {
        fprintf(stderr, "accept error : %s\n\a", strerror(errno));
        exit(0);
    }

    cout << "accept success" <<endl;
    char message[MAXLINE] = {0};

    while(1) {
        // 6、读取客户端发送的数据
        int n = read(clientfd, message, MAXLINE);
        if (n < 0) {
            fprintf(stderr, "read error : %s\n\a", strerror(errno));
            break;
        } else if (n == 0) { // 返回 0 表示读到 Fin 报文
            fprintf(stderr, "client closed \n");
            // close(clientfd); // 没有数据要发送，立马关闭连接
            break;
        }

        message[n] = 0;
        // 处理获取的数据
        cout << "received " << n << " bytes : " << message << endl;
    }

    // 7、发送数据
    char overstring[64] = "it is over !";
    int ret = send(clientfd, overstring, strlen(overstring), 0);
    cout << ret << endl;
    if (ret != strlen(overstring)) {
        fprintf(stderr, "send data error : %s\n", strerror(errno));
        return -1;
    }

    cout << "already send " << ret << " bytes" << endl;
    shutdown(listenfd, SHUT_RD);
    return 0;
}

