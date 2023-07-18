#ifndef HEEP_CONN_H
#define HTTP_CONN_H

#include "locker.h"
#include "threadpool.h"
#include "lst_timer.h"
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
#include <regex>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <stdarg.h>
#include <assert.h>



class HttpConn {
public:
    HttpConn();
    ~HttpConn();
    bool Init(int fd, const sockaddr_in& addr, Util_Timer* timer, Sort_Timer_List* timer_list); // 初始化任务的fd和地址等
    void Process();     // 任务处理
    bool CloseConn();   // 关闭连接
    bool Read();        // 读取数据
    bool Write();       // 写入数据
    int Get_m_sockfd();  // 获取socket文件描述符
public:
    static int m_epollfd_;              // 所有注册到epoll中得任务
    static int m_user_count_;           // 用户数量
    static Sort_Timer_List* m_list_head_;    // timer 管理队列

    static const int READ_BUFFER_SIZE_ = 2048;      // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE_ = 2048;     // 写缓冲区大小
    static const int FILENAME_LEN_ = 200;

    const char* doc_root_ = "/home/lj/lession/webserver/resources";     // 资源根目录

    // 定义HTTP响应的一些状态信息
    const char* ok_200_title = "OK";
    const char* error_400_title = "Bad Request";
    const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
    const char* error_403_title = "Forbidden";
    const char* error_403_form = "You do not have permission to get file from this server.\n";
    const char* error_404_title = "Not Found";
    const char* error_404_form = "The requested file was not found on this server.\n";
    const char* error_500_title = "Internal Error";
    const char* error_500_form = "There was an unusual problem serving the requested file.\n";

    // HTTP 请求方法，目前只支持GET
    /*
        - GET       - 获取资源；该方法请求获取指定资源,只请求获取数据,不做修改。GET请求可以缓存。
        - POST      - 发送数据进行处理；向指定资源提交数据,请求服务器进行处理,如提交表单或者上传文件。POST请求不会被缓存。
        - PUT       - 更新资源；向指定资源位置上传内容,覆盖原数据,用于完整替换资源。
        - PATCH     - 更新资源的部分内容；向资源提交部分更新,只修改指定的数据,不影响其他部分。
        - DELETE    - 删除资源；请求删除指定的资源。
        - HEAD      - 获取报文首部；请求获取资源的响应报文的首部,用于获取报文信息而不获取资源本身。
        - OPTIONS   - 查询支持的方法；用于查询指定资源支持的通信选项,比如查询指定资源支持的HTTP方法。
        - CONNECT   - 建立网络连接；通常用于建立HTTPS加密连接。
        - TRACE     - 追踪路径；回显服务器收到的请求,用于测试或者诊断。
    */
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE     :    当前正在分析请求行
        CHECK_STATE_HEADER          :    当前正在分析头部字段
        CHECK_STATE_CONTENT         :    当前正在解析请求体
    */
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

    /*
        从状态机的三种可能状态，对行的读取状态，分别表示
        LINE_OK     :       读取到一个完整的行
        LINE_BAD    :       行出错
        LINE_OPEN   :       行数据尚不完整
    */  
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, 
                    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

private:
    void Init();                        // 初始化http数据包信息
    HTTP_CODE Process_Read();           // 解析http请求
    bool Process_Write(HttpConn::HTTP_CODE ret);               // 处理HTTP请求

    LINE_STATUS Parse_Line();                       // 解析一行判断依据
    char* Get_Line();                               // 获取一行数据
    HTTP_CODE Parse_Request_Line(char* text);       // 解析请求行
    HTTP_CODE Parse_Head(char* text);               // 解析请求头
    HTTP_CODE Parese_Content(char* text);           // 解析请求体
    HTTP_CODE Do_Response();                        // 做回应
    void Unmap();                                   // 解除文件映射

    bool Add_Response(const char* format, ...);                    // 向写缓冲区中写入数据
    bool Add_Status_Line(int status, const char* title);           // 添加响应行
    bool Add_Headers(int content_len);                             // 添加响应头
    bool Add_Head_ContentLength(int content_len);                  // 添加 content_length
    bool Add_Content_Type();                                       // 添加响应体类型
    bool Add_Linger();                                             // 添加Keep—alive
    bool Add_Blank_Line();                                         // 添加空行 \r\n
    bool Add_Content(const char* content);                         // 添加响应体
private:
    
    int m_sockfd_;                          // 任务对应的套接字
    sockaddr_in m_addr_;                    // 客户端地址
    Util_Timer* m_timer_;                      // 定时器

    char m_read_buf_[READ_BUFFER_SIZE_];    // 读缓冲区大小
    int m_read_idx_;                        // 标识内核缓冲区数据被读缓冲区已经读入的数据的最后一个字节的下一个位置
    int m_checked_idx_;                     // 当前正在解析的字符在读缓冲区中的位置
    int m_start_of_line_;                   // 当前正在解析的行的起始位置

    char m_write_buf_[WRITE_BUFFER_SIZE_];  // 写缓冲区
    int m_write_idx_;                       // 标识写缓冲区中已写的个数 或者 表示下次写入的位置             
    iovec m_iv_[2];                         // 采用 writev 来执行写操作 查看手册 writev
    int m_iv_count_;                        // m_iv_count_ 表示被写内存块的数量
    int m_bytes_to_send_;                   // 需要发送的响应的字节数
    int m_bytes_have_send_;                 // 已经发送出去的字节数

    CHECK_STATE m_check_status_;            // 主机状态当前状态
    METHOD m_method_;                       // 请求的方法

    char m_real_file_[FILENAME_LEN_];        // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root 是网页根目录
    struct stat m_real_file_stat_;          // 客户请求的文件的状态信息
    char* m_file_address_;                   // 文件映射地址
    char* m_url_;                           // 客户端请求的目标文件的文件名
    char* m_version_;                       // HTTP协议版本号，仅支持http1.1
    char* m_host_;                          // 主机名
    int m_content_length_;                  // http请求的消息总长度
    bool m_linger_;                         // http请求是否要求保持连接
};

#endif