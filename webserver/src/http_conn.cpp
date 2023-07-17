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

// 修改文件描述符的事件状态，重置 socket 上的 EPOLLONESHOT 事件，确保下次可读时，epoll仍然提醒
bool Modfd(int epollfd, int fd, int ev) {
    // 获取当前 fd 的事件类型 其中 ev 就是之前的状态
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    int ret = 0;
    while ((ret = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event)) == -1);
    return true;
}


// ********************HttpConn API************************

/*
    分为两个部分 read 和 write
    read : 处理流程
    1、从 socket 内核缓冲区中将数据一次性读到用户缓冲器
    2、调用任务实例的 Process 处理读取的数据
    3、从缓冲器中开始开始分解数据，设置三种主机状态机，依次运行
    4、http数据分为请求行、请求头、请求数据
        4.1、分解请求行
        4.2、分解请求头
        4.3、分解请求数据
    5、每个主机状态机下还有从机状态机，分别是读取到完成的一个行、行出错、行数据不完整
    6、http请求完整，开始根据请求映射数据

    write : 处理流程
    1、根据读返回的状态运行不同逻辑
    2、设置响应行信息
    3、设置响应头信息
    4、设置响应数据
    5、映射文件
    6、修改文件描述的状态
    7、写入数据到内核的socket缓冲区
*/

// 初始化静态变量
int HttpConn::m_epollfd_ = -1;
int HttpConn::m_user_count_ = 0;

HttpConn::HttpConn() {
    m_sockfd_ = -1;
}

HttpConn::~HttpConn() {

}

// 二次初始化连接，初始化套接字和地址
bool HttpConn::Init(int fd, const sockaddr_in& addr) {
    // 初始化任务
    m_sockfd_ = fd;
    m_addr_ = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (!Addfd(HttpConn::m_epollfd_, m_sockfd_, true)) {
        std::cout << "客户端套接字加入epoll失败" << std::endl;
        return false;
    }
    HttpConn::m_user_count_++;
    Init();
    return true;
}

// 初始化连接，初始化http数据包信息
void HttpConn::Init() {
    // 初始化缓冲区参数
    // 写缓冲区参数
    m_bytes_have_send_ = 0;
    m_bytes_to_send_ = 0;
    m_write_idx_ = 0;
    memset(m_write_buf_, 0, WRITE_BUFFER_SIZE_);

    // 状态机信息、连接信息
    m_check_status_ =  CHECK_STATE_REQUESTLINE;
    m_linger_ = false;  // 默认短链接
    m_method_ = GET;
    m_url_ = nullptr;
    m_version_ =  nullptr;
    m_host_ =nullptr;
    m_content_length_ = 0;
    memset(m_real_file_, 0, FILENAME_LEN_);
    
    // 读缓冲区信息
    m_start_of_line_ = 0;
    m_checked_idx_ = 0;
    m_read_idx_ = 0;
    memset(m_read_buf_, 0, READ_BUFFER_SIZE_);

}

// 关闭连接
bool HttpConn::CloseConn() {
    if (m_sockfd_ != -1) {
        // 从 epoll 移除 fd
        if (!Removefd(m_epollfd_, m_sockfd_)) {
            char ip[16];
            inet_ntop(AF_INET, &m_addr_.sin_addr.s_addr, ip, sizeof(ip));
            printf("客户端套接字 %d, IP : %s 关闭失败\n", m_sockfd_, ip);
            return false;
        }
        m_sockfd_ = -1;
        HttpConn::m_user_count_--;
    }
    // std::cout << m_sockfd_ << std::endl;
    return true;
}

// 从套接字中读取数据到缓冲中
bool HttpConn::Read() {
    // 一次性读完数据
    // 判断内核缓冲区文件读取大小是否超过用户区缓冲区大小
    if (m_read_idx_ >= READ_BUFFER_SIZE_) {
        return false;
    }
    char bytes_read = 0;
    // 循环读取内核缓冲区中的信息
    while (true) {
        // 从 m_read_buf_ + m_read_idx 索引处开始保存信息，大小是 READ_BUFFER_NUMBER - m_read_idx
        // recv 默认阻塞，但如果 fd 非阻塞，那么也是非阻塞
        bytes_read = recv(m_sockfd_, m_read_buf_ + m_read_idx_, READ_BUFFER_SIZE_ - m_read_idx_, 0);
        // std::cout << "recv : 读取成功" << std::endl;
        // 判断是否读取结束
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 在非阻塞socket上调用recv/send时,如果当前无数据可读/写,则返回EWOULDBLOCK。表示资源暂时不可用,如果立即等待会阻塞
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            // 对方关闭连接
            return false;
        }
        m_read_idx_ += bytes_read;
    }
    return true;
}

// 从缓冲写入数据到套接字中
bool HttpConn::Write() {
    // 一次性写完数据
    if (m_bytes_to_send_ == 0) {
        // 如果待发送的数据 = 0，重新开始接收数据
        int i = 5;
        while (i > 5 && !Modfd(m_epollfd_, m_sockfd_, EPOLLIN)) {
            --i;
        }
        if (i == 0)  {
            // 运行失败
        }
        // 初始化各个参数
        Init();
        return true;
    }
    // std::cout << "开始写入" << std::endl;
    // 采用分散写方式
    int temp = 0;   // 表示成功写入的数据字节数
    while (true) {
        // std::cout << "writev : 写入..." << std::endl;
        temp = writev(m_sockfd_, m_iv_, m_iv_count_);
        // std::cout << "writev : 写入完成" << std::endl;
        if (temp <= -1) {
            // 如果 socket 缓冲区没有空间，等待下依次的 EPOLLOUT 事件，
            // 但是此服务器无法接收同一个客户的下依次数据，确保连接的完整性
            if (errno == EAGAIN) {
                // 修改 sockfd 的事件类型,检测事件是否可行
                Modfd(m_epollfd_, m_sockfd_, EPOLLOUT);
                // std::cout << "写入成功" << std::endl;
                return true;
            }
            // std::cout << "写入失败" << std::endl;
            Unmap();
            return false;
        }
        // 记录已经发送的字节数，和剩下待发送的字节数
        m_bytes_to_send_ -= temp;
        m_bytes_have_send_ += temp;
        // 根据上述情况分成两个情况 如果第一个 m_iv_ 是否发送完，处理第一个的字节len
        if (m_bytes_have_send_ >= m_iv_[0].iov_len) {
            m_iv_[0].iov_len = 0;
            m_iv_[1].iov_base = m_file_address_ + (m_bytes_have_send_ - m_read_idx_);
            m_iv_[1].iov_len = m_bytes_to_send_;
        } else {
        // 如果第一个 m_iv_ 未发送完，修改发送的位置和长度
            m_iv_[0].iov_base = m_write_buf_ + m_bytes_have_send_;
            m_iv_[0].iov_len = m_iv_[0].iov_len - temp;
        }
        // std::cout << "写入第二部分" << std::endl;
        // 如果 第二个发送完，修改该第二个的发送位置和长度
        if (m_bytes_to_send_ <= 0) {
            // 没有数据要发送的 解除映射、修改状态
            Unmap();
            Modfd(m_epollfd_, m_sockfd_, EPOLLIN);
            // std::cout << m_linger_ << std::endl;
            if (m_linger_) {
                Init();
                return true;
            } else {
                return false;
            }
        }

    }
    // 非阻塞写，如果发送给的字节数变为0，写结束
    
    return true;
}

void HttpConn::Process() {
    // std::cout << "Process : 开始处理" << std::endl;
    // 解析http请求
    HTTP_CODE ret = Process_Read();
    // 判断是否是消息不完整
    if (ret == NO_REQUEST) {
        // 判断 fd 是否修改成功
        int i = 5;
        while (i > 0 && !Modfd(m_epollfd_, m_sockfd_, EPOLLIN)) {
            --i;
        }
        if (i == 0) {
            // 修改失败则将ret修改为 INTERNAL_ERROR 错误传入写处理中
            ret = INTERNAL_ERROR;
        } else {
            // 修改成功
            return;
        }
    }
    // std::cout << "parse request, create response" << std::endl;
    // 生成响应 如果发生错误：关闭连接
    if (!Process_Write(ret)) {
        int i = 5;
        while (i > 0 && CloseConn()) {
            --i;
        }
        if (i == 0) {
            // 关闭连接失败 如果出现各种关闭问题最终怎么处理？
            // 关闭连接失败，那么说明这个socket的文件描述符无法从epoll中移除
            // 那么由于EPOLLONESHOT的作用，会使得这个文件描述符不会被再次感知到
            // 处理方式：任由其游离在epoll中，占据一个系统资源
            return;
        }
    }
    // 写关闭成功
    Modfd(m_epollfd_, m_sockfd_, EPOLLOUT);
    // std::cout << "Modfd : 修改结束" << std::endl;
}

HttpConn::HTTP_CODE HttpConn::Process_Read() {
    // 设置从机状态机和主机状态机
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    // 开始循环状态机
    // 在两种情况下继续：主机状态到了请求数据状态同时行请求正常；主机解析非请求数据同时行状态正常
    char * text = 0;    // 读取的一行数据的地址
    while (((m_check_status_ == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
                || (line_status = Parse_Line()) == LINE_OK) {
        // 获取一行数据
        text = Get_Line();
        m_start_of_line_ = m_checked_idx_;
        // printf("got 1 http line : %s\n", text);
        // std::cout << m_check_status_ << std::endl;

        // 状态不同处理方式
        switch (m_check_status_) {
            case CHECK_STATE_REQUESTLINE: {
                // 解析请求行
                ret = Parse_Request_Line(text);
                // std::cout << ret << std::endl;
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                // 解析请求头
                ret = Parse_Head(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    // 获得一个完整的请求，开始回应请求
                    return Do_Response();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                // 解析请求体
                ret = Parese_Content(text);
                if (ret == GET_REQUEST) {
                    return Do_Response();
                }
                // 请求体数据不完整，下一步如何处理？
                /*
                    如果数据不完整，则回返回 NO_REQUEST 情况，此时回修改 socket 文件描述符的状态
                    然后直接返回，继续读取文件，此时上次 m_read_buf_ 用户缓冲区的内容不清除，从后续
                    继续读取 socket 中的信息。
                */
                line_status = LINE_OPEN;
                break;
            }
            default: {
                // 其他非法情况返回内部错误
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 解析一行，判断依据是 \r\n
HttpConn::LINE_STATUS HttpConn::Parse_Line() {
    char* buf = m_read_buf_ + m_checked_idx_;
    // 被解析的长度
    size_t len = m_read_idx_ - m_checked_idx_;
    // 初始化正则表达式对象
    std::cmatch result;
    // 只要找到的字符串可以确保里面不会再含有\r\n中的任何一个
    if (!std::regex_search(buf, result, std::regex("^[^\\r\\n]*\\r\\n"))) {
        if(std::regex_search(buf, result, std::regex("^.*?\\r"))) {
            // 如果找到 \r
            if (result.length() == len) {
                return LINE_OPEN;
            } else {
                return LINE_BAD;
            }
        } else if (std::regex_search(buf, result, std::regex("^.*?\\n"))) {
            // 如果找到 \n
            if (( m_checked_idx_ > 1) && (m_read_buf_[m_checked_idx_ - 1] == '\r')) {
                m_read_buf_[m_checked_idx_ - 1] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        }
        return LINE_OPEN;
    }
    // 获取结果
    int slen = result.length();
    m_read_buf_[m_checked_idx_ + slen - 2] = '\0';
    m_read_buf_[m_checked_idx_ + slen - 1] = '\0';
    m_checked_idx_ += slen;
    return LINE_OK;
}

// 获取一行数据
char* HttpConn::Get_Line() {
    return m_read_buf_ + m_start_of_line_;
}

// 解析请求行 获得请求方法，目标URL,以及HTTP版本号
HttpConn::HTTP_CODE HttpConn::Parse_Request_Line(char* text) {
    // 不断解析这一行的数据
    // GET /index.html HTTP/1.1 
    // 获取第1个信息
    m_url_ = strpbrk(text, " \t");  // 判断第二个参数中字符哪个在text中最先出现
    if (!m_url_) {
        // std::cout << "第一个请求错误" << std::endl;
        return BAD_REQUEST;
    }
    *m_url_++ = '\0';
    char* method = text;
    // 这个地方可以增加其他的方法
    if (strcasecmp(method, "GET") == 0) {
        m_method_ = GET;
    } else {
        // std::cout << "第二个请求错误" << std::endl;
        return BAD_REQUEST;
    }

    // 获取第3个信息
    m_version_ = strpbrk(m_url_, " \t");
    if (!m_version_) {
        // std::cout << "第三个请求错误" << std::endl;
        return BAD_REQUEST;
    }
    *m_version_++ = '\0';
    if (strcasecmp(m_version_, "HTTP/1.1") != 0) {
        // std::cout << "第四个请求错误" << std::endl;
        return BAD_REQUEST;
    }

    // 整合第2个信息
    if (strncasecmp(m_url_, "http://", 7) == 0) {   // 比较前7个字符
        m_url_ += 7;
        m_url_ = strchr(m_url_, '/'); 
    }
    if (!m_url_ || m_url_[0] != '/') {
        return BAD_REQUEST;
    }
    // 请求行处理结束
    // std::cout << "Parse_Request_Line : 结束" << std::endl;
    m_check_status_ = CHECK_STATE_HEADER;

}

// 处理请求头的不同类型 这里主要针对 Host、connection、Context-lentgh
HttpConn::HTTP_CODE HttpConn::Parse_Head(char* text) {
    // 遇到空行，表示头部解析结束
    std::cmatch result;
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        if (m_content_length_ != 0) {
            // 状态机转为 CHECK_STATE_CONTENT
            m_check_status_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则得到一个完整的HTTP请求 - 不包含请求体的请求
        // std::cout << "完整的请求!" << std::endl;
        return GET_REQUEST;
    } else if (std::regex_search(text, result, std::regex("(Connection|Content-Length|Host):"))) {
        // 处理几种消息头信息
        // 使用正则表达式直接筛选出请求头的各类消息类型，使用 switch 对不同的类型 头处理
        std::string res = result[0];
        // 处理 connection 头部字段 connection:keep-alive
        if (res == "Connection:") {
            // 处理Connect 头部字段， 判断是不是KEEP-ALIVE
            if (std::regex_search(text, result, std::regex("keep-alive"))) {
                m_linger_ = true;
            }
        } else if (res == "Content-Length:") {
            // 处理 Content-Length 头部字段
            if (std::regex_search(text, result, std::regex("[0-9]*"))) {
                m_content_length_ = stoi(std::string(result[0]));
            }
        } else if (res == "Host:") {
            // 处理 Host 头部字段
            text += 5;
            text += strspn( text, " \t" );
            m_host_ = text;
        }
    } else {
        // std::cout << "oop! unknow header " << text << std::endl;
    }
    return NO_REQUEST;
}

// 没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入，后续可以自己根据实际情况进行处理
HttpConn::HTTP_CODE HttpConn::Parese_Content(char* text) {
    if (m_read_idx_ >= (m_content_length_ + m_checked_idx_)) {
        text[m_content_length_] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 对正常的请求做响应，分析目标文件的属性
// 如果文件存在，检查所有用户使用权限，确保不是目录文件
// 以上条件都符合：将目标文件 mmp 将其映射到内存 m_file_address 处，告诉调用者获取文件成功
HttpConn::HTTP_CODE HttpConn::Do_Response() {
    // std::cout << "Do_Response : " << std::endl;
    // 目标文件的路径 "/home/lj/lession/webserver/resources"
    strcpy(m_real_file_, doc_root_);
    int len = strlen(doc_root_);
    // 将路径前缀加到目标文件名前
    strncpy(m_real_file_ + len, m_url_, FILENAME_LEN_ - len -1);
    // 获取目标文件的相关状态信息：是否存在、是否为目录、是否可读、获取文件大小，-1 失败， 0 成功
    if (stat(m_real_file_, &m_real_file_stat_) < 0) {
        // 判断文件是否存在
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_real_file_stat_.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_real_file_stat_.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开方式
    int fd = open(m_real_file_, O_RDONLY);
    if (fd < 0) {
        // 文件打开失败 返回资源不存在
        return NO_RESOURCE;
    }
    // 创建内存映射
    m_file_address_ = (char*)mmap(0, m_real_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m_file_address_ == MAP_FAILED) {
        return INTERNAL_ERROR;
    }
    // 关闭文件
    close(fd);
    // 返回处理状态
    // std::cout << "Do_Response : 处理成功" << std::endl;
    return FILE_REQUEST;
}

// 解除文件映射
void HttpConn::Unmap() {
    if (m_file_address_) {
        munmap(m_file_address_, m_real_file_stat_.st_size);
        m_file_address_ = nullptr;
    }
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool HttpConn::Process_Write(HttpConn::HTTP_CODE ret) {
    // 根据 解析过程处理结果，对不同的事件进行处理
    // 需要处理的部分 主要添加不同状态码，填充响应头，填充响应体，以及最后处理成功响应
    switch (ret) {
        case INTERNAL_ERROR: {
            // 内部错误处理
            if (!Add_Status_Line(500 , error_500_title)) {
                return false;
            }
            if (!Add_Headers(strlen(error_500_form))) {
                return false;
            }
            if (!Add_Content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            // 请求错误处理
            // std::cout << "错误响应生成中..." << std::endl;
            if (!Add_Status_Line(400, error_400_title)) {
                return false;
            }
            if (!Add_Headers(strlen(error_400_form))) {
                return false;
            }
            if (!Add_Content(error_400_form)) {
                return false;
            }
            // std::cout << "错误响应生成成功" << std::endl;
            break;
        }
        case NO_RESOURCE: {
            // 没有对应资源处理
            if (!Add_Status_Line(404, error_404_title)) {
                return false;
            }
            if (!Add_Headers(strlen(error_404_form))) {
                return false;
            }
            if (!Add_Content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            // 权限不足处理
            if (!Add_Status_Line(403, error_404_title)) {
                return false;
            }
            if (!Add_Headers(strlen(error_403_form))) {
                return false;
            }
            if (!Add_Content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            // 成功请求错误处理
            // std::cout << "Process_Write : FILE_REQEST 1" << std::endl;
            if (!Add_Status_Line(200, ok_200_title)) {
                return false;
            }
            if (!Add_Headers(m_real_file_stat_.st_size)) {
                return false;
            }
            // std::cout << "Process_Write : FILE_REQEST 2" << std::endl;
            
            // 将两部分信息结合起来 struct iovec
            m_iv_[0].iov_base = m_write_buf_;
            m_iv_[0].iov_len = m_write_idx_;
            m_iv_[1].iov_base = m_file_address_;
            m_iv_[1].iov_len = m_real_file_stat_.st_size;
            m_iv_count_ = 2;
            m_bytes_to_send_ = m_write_idx_ + m_real_file_stat_.st_size;
            // std::cout << "Process_Write : FILE_REQEST 3" << std::endl;
            return true;
        }
        default:
            return false;
    }
    // 将基本信息写入  struct iovec
    m_iv_[0].iov_base = m_write_buf_;
    m_iv_[0].iov_len = m_write_idx_;
    m_iv_count_ = 1;
    m_bytes_to_send_ = m_write_idx_;
    // std::cout << "写入缓冲区中" << std::endl;
    return true;
}

// 向写缓冲区中写入数据
bool HttpConn::Add_Response(const char* format, ...) {
    // 检查当前写入位置是否超过缓冲区大小
    if (m_write_idx_ >= WRITE_BUFFER_SIZE_) {
        return false;
    }
    // 使用 va_list 写入数据
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf_ + m_write_idx_, WRITE_BUFFER_SIZE_, format, arg_list);
    // 判断写入的数据是否超过缓冲区大小
    if (len >= (WRITE_BUFFER_SIZE_ - m_write_idx_)) {
        return false;
    }
    // 当前写入位置增加
    m_write_idx_ += len;
    va_end(arg_list);
    return true;
}

// 添加响应行信息
bool HttpConn::Add_Status_Line(int status, const char* title) {
    // 将响应行的数据写入缓冲区中
    return Add_Response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应头信息 - 内部调用根据不同行调用不同信息
bool HttpConn::Add_Headers(int content_len) {
    // 添加响应体长度
    Add_Head_ContentLength(content_len);
    // 添加响应体类型
    Add_Content_Type();
    // 添加Keep—alive
    Add_Linger();
    // 添加空行 \r\n
    Add_Blank_Line();
}

// 添加响应头中 content_length 信息
bool HttpConn::Add_Head_ContentLength(int content_len) {
    return Add_Response("Content-Length: %d\r\n", content_len);
}

// 添加响应体类型
bool HttpConn::Add_Content_Type() {
    // 后续这个地方根据自己实际需求修改文件类型
    return Add_Response("Content-Type: %s\r\n", "text/html");
}

// 添加Keep—alive                                   
bool HttpConn::Add_Linger() {
    return Add_Response("Connection: %s\r\n", (m_linger_ == true ? "keep-alive" : "close"));
}

// 添加空行 \r\n                                             
bool HttpConn::Add_Blank_Line() {
    return Add_Response("%s", "\r\n");
}

// 添加响应体信息
bool HttpConn::Add_Content(const char* content) {
    return Add_Response("%s", content);
}