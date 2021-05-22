#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

using namespace std;

#define SERVER_PORT 8000        // 服务端口
#define BUFFER_SIZE 1024        // 数据段长度
#define FILE_NAME_MAX_SIZE 512  // 文件名最大长度
#define wnd 1024                // 窗口长度

const char* server_ip = "172.19.35.185";  // 服务端IP

set<int32_t> ready_to_send; // 要发送的包id

int FLAG = -1; // 挥手包id
int ending = 0; // 结束标记

/* 参数结构体，用于向线程函数传递参数 */
struct parameter {
    int32_t client_socket_fd;
    struct sockaddr_in server_addr;
    socklen_t server_addr_length;
    const char* file_name;
};

// 用于计算时刻
#define GET_TIME(now)                           \
    {                                           \
        struct timeval t;                       \
        gettimeofday(&t, NULL);                 \
        now = t.tv_sec + t.tv_usec / 1000000.0; \
    }

/* 报头 */
typedef struct
{
    int32_t id;        // 报文id
    int32_t buf_size;  // 数据部分长度
    int16_t fin;       // 结束标志符
    int16_t syn;       // 建立连接请求标志符
} PacketHead;

/* 数据包 */
struct Packet {
    PacketHead head;
    char buf[BUFFER_SIZE];
} packet;

/**
 * @brief 创建Server和Socket
 * @param server_addr   服务端地址
 * @param server_addr_length    服务端地址长度
 * @param client_socket_fd  Socket的文件描述符
 * @return void
 */
void Setup_ServerAndSocket_Client(struct sockaddr_in& server_addr, socklen_t& server_addr_length, int32_t& client_socket_fd) {
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                    // 设置地址族
    server_addr.sin_addr.s_addr = inet_addr(server_ip);  // 装入服务端ip地址
    server_addr.sin_port = htons(SERVER_PORT);           // 装入端口号

    server_addr_length = sizeof(server_addr);  // 服务端地址长度

    /* 创建socket */
    client_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);  // 创建套接字，第二个参数代表类型为UDP
    if (client_socket_fd < 0) {
        cerr << "Create Socket Failed:";  // 异常处理：创建套接字失败
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief 将文件分片发送到服务端的线程，每次发wnd个包，如果检测到有包没有得到ack则选择重传，直到所有包都得到ack则滑动窗口，将窗口移动到后wnd个包
 * @param client_socket_fd  socket描述符
 * @param server_addr   接收方服务地址
 * @param server_addr_length   接收方服务地址长度
 * @param file_name    文件名
 * @return void
 */
void* Post(void* arg) {
    struct parameter* p = (parameter*)arg;
    int32_t client_socket_fd = p->client_socket_fd;
    struct sockaddr_in server_addr = p->server_addr;
    socklen_t server_addr_length = p->server_addr_length;
    const char* file_name = p->file_name;

    int32_t len = 0;  //  要发的文件段大小
    FILE* fp;  // 文件指针
    int32_t base = 1;  // 每段的段首包id
    int16_t finish = 0;  // 检查该段是否传输完成

    /* 每次向server发wnd个包，如果检测到有包没有得到ack则选择重传，直到所有包都得到ack则滑动窗口，将窗口移动到后wnd个包 */
    while (1) {
        for (int i : ready_to_send) {
            /* 防溢出 */
            if (FLAG > 0 && i >= FLAG)
                break;

            /* 如果是握手包，则发送文件夹名 */
            if (i == 1) {
                /* 以只读方式打开文件 */
                if (fp == NULL) {
                    fp = fopen(file_name, "r");
                    /* 异常处理：没有此文件 */
                    if (NULL == fp) {
                        cout << "File:" << file_name << " Not Found. Please Enter an Existed File" << endl;
                        exit(EXIT_FAILURE);
                    }
                }
                bzero(packet.buf, BUFFER_SIZE);
                /*  将文件名存入buffer内    */
                strncpy(packet.buf, file_name, strlen(file_name) > BUFFER_SIZE ? BUFFER_SIZE : strlen(file_name));
                /* 将要发送的信息打包到报文头部 */
                packet.head.id = i;
                packet.head.buf_size = BUFFER_SIZE;
                packet.head.fin = 0;
                packet.head.syn = 1;
                // cout << "data_size : " << sizeof(packet) << endl;
                /*   发送报文到接收端   */
                if (sendto(client_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                    cerr << "Send File Failed:" << endl;  // 异常处理：sendto函数调用失败
                    exit(EXIT_FAILURE);
                }
                // cout << "RTT : " << stop - start << endl;
                cout << "N0: " << i << endl;
            }
            /* 若不是握手包，则从文件中读取一个段数据存入保重并发给server */ 
            else {

                /* 文件指针偏移 */
                fseek(fp, (BUFFER_SIZE * (i - 2)), 1);
                /* 从文件中读取BUFFER_SIZE个Byte，作为一个包的数据 */
                len = fread(packet.buf, 1, BUFFER_SIZE, fp);
                /* 文件指针复原 */
                fseek(fp, -(min(len, BUFFER_SIZE)), 1);
                fseek(fp, -(BUFFER_SIZE * (i - 2)), 1);
                // cout << "len : " << len << " i: " << i << endl;
                if (len > 0) {
                    packet.head.id = i;          // 发送id放进包头,用于标记顺序
                    packet.head.buf_size = len;  // 记录数据长度
                    packet.head.fin = 0;         // 不是挥手包
                    packet.head.syn = 0;         // 不是握手包
                    // cout << "data_size : " << sizeof(packet) << endl;
                    /*   发送报文到接收端   */
                    if (sendto(client_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                        cerr << "Send File Failed:" << endl;  // 异常处理：sendto函数调用失败
                        fclose(fp);
                        exit(EXIT_FAILURE);
                    }
                    // cout << "RTT : " << stop - start << endl;
                    cout << "N0: " << i << endl;
                } else {
                    packet.head.fin = 1;  // 挥手包，表示文件传输结束
                    packet.head.id = i;
                    if (sendto(client_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                        cerr << "Send File Failed!" << endl;
                        fclose(fp);
                        exit(EXIT_FAILURE);
                    }
                    finish = 1; // 结束标记
                    FLAG = i; // 标记挥手包的id
                    sleep(0.5); // 发送结束后sleep1秒，让另一个线程收完ack
                    break;
                }
            }
        }
        /* 如果结束，直接退出 */
        if (ending)
            return nullptr;
        /* 所有包都得到ack则滑动窗口，将窗口移动到后wnd个包 */
        if (ready_to_send.empty()) {
            base += wnd;
            for (int i = base; i < base + wnd; ++i) {
                ready_to_send.insert(i);
            }
            /* 如果结束，直接退出 */
            if (finish) {
                break;
            }
        }
    }
    /* 关闭文件 */
    fclose(fp);
    return nullptr;
}

/**
 * @brief 接受ACK的线程，每次收到server传来的ACK则将id加入has_acked中
 * @param client_socket_fd  socket描述符
 * @param server_addr   接收方服务地址
 * @param server_addr_length   接收方服务地址长度
 * @return void
 */
void* Get_ACK(void* arg) {
    /* 将各个参数从arg结构体中提取出来 */
    struct parameter* p = (parameter*)arg;
    int32_t client_socket_fd = p->client_socket_fd;
    struct sockaddr_in server_addr = p->server_addr;
    socklen_t server_addr_length = p->server_addr_length;

    /* 不断监听接收方传来的ack */
    while (1) {
        PacketHead ack;
        int ret;  // 存recvfrom返回值
        /*  尝试接收接收方发来的ACK */
        if ((ret = recvfrom(client_socket_fd, (char*)&ack, sizeof(ack), 0, (struct sockaddr*)&server_addr, &server_addr_length)) < 0) {
            cout << "----FINISH----" << endl;  // server已经不发送ack包了，代表传输结束，退出线程
            ending = 1;  // 代表传输结束
            return nullptr;
        }
        ready_to_send.erase(ack.id); // 将id加入has_acked中
    }
}

int main() {
    /*  计时变量    */
    double start, stop;

    /* 变量声明 */
    struct sockaddr_in server_addr;  // 服务端地址
    socklen_t server_addr_length;    // 服务端地址长度
    int32_t client_socket_fd;        // 套接字

    ready_to_send.clear();
    for (int i = 1; i < 1 + wnd; ++i) {
        ready_to_send.insert(i);
    }

    Setup_ServerAndSocket_Client(server_addr, server_addr_length, client_socket_fd);  // 创建服务器和套接字

    /*  sockopt使能设置结束时间 */
    struct timeval timeout;
    timeout.tv_sec = 1;   //秒
    timeout.tv_usec = 0;  //微秒
    if (setsockopt(client_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        cerr << "setsockopt failed:" << endl;  // 异常处理，设置socket超时字段失败
        exit(EXIT_FAILURE);
    }

    /* 输入文件名到缓冲区 */
    char file_name[FILE_NAME_MAX_SIZE + 1];
    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
    printf("Please Input File Name On Client: ");
    scanf("%s", file_name);

    /* 开始计时 */
    GET_TIME(start);

    long thread;                // 线程号
    pthread_t* thread_handles;  // 线程指针

    // 总共启用两个线程
    int thread_count = 2;

    // 为线程指针申请内存
    thread_handles = (pthread_t*)malloc(thread_count * sizeof(pthread_t));

    // 打包参数结构体
    struct parameter* par = new parameter;
    par->client_socket_fd = client_socket_fd;
    par->file_name = file_name;
    par->server_addr = server_addr;
    par->server_addr_length = server_addr_length;

    // 开启thread_count个线程，每个线程启用线程函数
    pthread_create(&thread_handles[0], NULL, Get_ACK,
                   (void*)par);
    pthread_create(&thread_handles[1], NULL, Post,
                   (void*)par);

    // 同步线程
    for (thread = 0; thread < thread_count; thread++)
        pthread_join(thread_handles[thread], NULL);

    /* 结束计时 */
    GET_TIME(stop);

    cout << "File:" << file_name << " Transfer Successful!" << endl;
    cout << "Spend " << stop - start << " Seconds for Transferring" << endl;

    free(thread_handles);

    /*  关闭套接字  */
    close(client_socket_fd);
    return 0;
}