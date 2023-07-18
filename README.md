# my_tcp_cs基础

第一次尝试自己写tcp的简单通讯，比较简单，但是还是收获很多

这对tcp的 `client` 和 `server` 是为了验证三挥手和四挥手

* 第 1 次的包中是三挥手，是因为服务端没有需要发送的消息，受到客户端的 `FIN` 后就直接回复 `FIN/ACK` 包，所以是三挥手

* 第 8 次的包中是四挥手，因为收到 `FIN` 包后，服务端还有消息要回，并且要被客户端接受到能优雅的四挥手，否则就会 `RST` 直接关闭

## client的代码编写

* 关闭连接需要使用 `shutdown` 函数 并使用 `SHUT_WR` 参数确保只关闭写，不关闭读。

## server的代码编写
* 同样使用 `shutdown` 函数更好
   
## 使用的抓包软件
`tcpdump`

    tcpdump -i lo tcp and port 8888 -s0 -w ./tcp_closex.pcap

分析软件 `Wireshark` 

* 将上面的 `.pacp` 使用该软件打开即可

## 增加了多线程的server编写

+ 对应的服务端和客户端为 `server_thread_copy.c` 和 `client_copy.c` 

+ 多线程编写编写流程如下

    1. 创建套接字
    2. 绑定端口和IP
    3. 监听端口
    4. 接收客户端发来的连接
    5. 创建子线程
    6. 子线程使用回调函数处理业务
    > 处理业务时子线程的回调函数需要获取客户端的信息，可以采用 struct 结构体将所有的信息打包，然后将结构体地址传给回调函数。

+ 打包结构体和传入结构体时注意的问题
    + 打包结构体后需要创建全局的变量，并确定该结构体个数（这决定了可同时运行的子线程的个数）
    + 需要在主线程中初始化结构体
    + 每接收到一个客户端的请求就在结构体数组中找到一个可以使用的结构体，并将客户端数据赋值给这个结构体。


# 基本服务器框架 - baseserver
这个是基本的服务器框架，已经可以完成接收和发送任务

## 采用模式
+ 模拟 proactor 模式

主线程模拟内核接收发送过来的数据，并将数据从内核空间中搬运到用户缓冲区中，并将待处理的任务加入到待处理队列中，等待子线程从队列获取任务并处理

线程池中的子线程负责从待处理队列中获取待处理的任务，解析任务，并更根据请求信息生成返回数据，并将这个数据写入到缓冲区中

可使用 reactor 模式


## 线程池
+ 线程池采用随机算法

通过主线程中生成很多线程，然后 epoll 检测到 socket 缓冲区中数据时(有连接信息发送过来)，将 socket 中的缓冲区的数据读出，然后调用将它加入到队列中。此时线程池中的**信号量**增加，使得阻塞在 `sem_wait` 的线程解除阻塞状态，内核会从一堆被阻塞的子线程中选取一个子线程，其他子线程仍然阻塞在`sem_wait` 中。这个解除阻塞的子线程加锁，确保同一时刻只有一个子线程在获取队列中的任务。获取任务后，解锁，并将**信号量**减一。

## 使用 epoll 模式的 ET 模式
+ epoll 模式

在内核中创建一个 epoll 实例(红黑树)，将需要检测的 socket 文件描述符加入红黑树中，让内核帮我们检测那个 socket 缓冲区中有数据。

有数据时，此时就会将阻塞在 `epoll_wiat` 的用户进程唤醒，用户进程会得到一个装满数据的 epoll_event 数组，遍历这个数组，依次判断数组中的 epoll_event 事件是什么类型，然后根据不同的类型做不同的处理。

我们在将文件描述符加入到红黑树时，已经设置了事件检测类型，内核会针对我们设定的类型做判断，然后将符合设定事件的文件描述符加入到 epoll_event 数组中。需要注意的是，返回的 epoll_event 中包含该 socket 所有的事件，都可以检测，读事件和写事件必然被返回。

# Http 服务器 - webserver
采用上文中的基本server架构，并完成了HTTP消息的消息的解析和生成响应。目前采用 HTTP/1.0 协议，仅支持 GET 请求，同时仅对部分请求头进行了回应，并生成响应。

部分技术栈：
+ 内存映射将响应文件，mmap
+ 采用可变参数函数
+ 使用 ioev 分块写方式
+ 采用正则表达式解析 http 请求头（C++的正则表达式好像在不同平台表现不同）

编译指令（防止忘记）
`g++ -o server ./src/*.cpp -I ./include -pthread`

采用 webbranch 压力测试
`./webbranch -c 1000 -t 10 http://IP:port/index.html`
+    -c 表示访问个数 -t 表示访问时间
+ 压力测试软件，文件目录下 make 即可

## 加入定时器