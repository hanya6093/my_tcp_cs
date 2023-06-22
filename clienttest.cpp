#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <iostream>

using namespace std;

int main(int argc, char* argv[]) {
    // 1、创建一个监听socket
    int connectfd = socket(AF_INET, SOCK_STREAM,0);
    if (connectfd < 0) {
        fprintf(stderr, "socket error : %s\n\a", strerror(errno));
        return -1;
    }

    // 2、初始化服务器地址和端口
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(8888);

    // 3、连接服务器
    if (connect(connectfd, (struct sockaddr *)(&server_addr),sizeof(server_addr)) < 0) {
        fprintf(stderr, "cnnect error : %s\n", strerror(errno));
        return -1;
    }

    cout << "connect success" <<endl;

    char sendline[64] = "hello, i am xiaolin";

    // 4、发送数据
    int ret = send(connectfd, sendline, strlen(sendline), 0);
    if (ret != strlen(sendline)) {
        fprintf(stderr, "send data error : %s\n", strerror(errno));
        return -1;
    }

    cout << "already send " << ret << " bytes" << endl;

    // 5、关闭连接
    // close(connectfd);
    shutdown(connectfd, SHUT_WR);

    // 6、读取客户端发送的数据
    char messages[64] = {0};
    int n = read(connectfd, messages, strlen(sendline));
    if (n < 0) {
        fprintf(stderr, "read error : %s\n\a", strerror(errno));
    } else if (n == 0) { // 返回 0 表示读到 Fin 报文
        fprintf(stderr, "client closed \n");
        // close(clientfd); // 没有数据要发送，立马关闭连接
    }
    messages[n] = 0;
    // 处理获取的数据
    cout << "received " << n << " bytes : " << messages << endl;


    sleep(1);


    return 0;
}