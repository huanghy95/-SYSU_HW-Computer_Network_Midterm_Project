#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
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
#include <string>

using namespace std;

#define SERVER_PORT 8000        // 服务端口
#define BUFFER_SIZE 1024        // 数据段长度
#define FILE_NAME_MAX_SIZE 512  // 文件名最大长度
#define max_buff_size 100000

char text_buf[max_buff_size][BUFFER_SIZE];
bool book[max_buff_size];
int max_length = -1;
int tot = 0;
int buflen[max_buff_size];
int begin_position = 2;
int line = 10000;

/**
 * @brief 自定义的strncpy函数，完全复制字符串，防止出现/0问题
 */
void my_strncpy(char* s1, char* s2, int len) {
    for (int i = 0; i < len; ++i) {
        s1[i] = s2[i];
    }
}

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

/* 接收报文 */
struct Packet {
    PacketHead head;
    char buf[BUFFER_SIZE];
} packet;

/**
 * @brief 创建Server和Socket
 * @param server_addr   UDP套接口
 * @param server_socket_fd  socket描述符
 * @return void
 */
void Setup_ServerAndSocket_Server(struct sockaddr_in& server_addr, int32_t& server_socket_fd) {
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                 // 设置地址族
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 装入服务端ip地址
    server_addr.sin_port = htons(SERVER_PORT);        // 装入端口号

    /* 创建套接字 */
    server_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);  // 创建套接字，第二个参数代表类型为UDP

    /* 异常处理：套接字创建失败 */
    if (server_socket_fd == -1) {
        cerr << "Create Socket Failed:" << endl;
        exit(EXIT_FAILURE);
    }

    /* 绑定套接口 */
    if (-1 == (bind(server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)))) {
        cerr << "Server Bind Failed:" << endl;
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief 创建并打开文件
 * @param file_name 要创建打开的文件名
 * @return File* 返回对应文件指针
 */
FILE* Create_And_Open_File(char* file_name) {
    FILE* fp = fopen(file_name, "w");
    if (fp == NULL) {
        cout << "Couldn't Open "
             << "File:\t" << file_name << endl;
    }
    return fp;
}

/**
 * @brief 监听客户端发来的分组并将其接收，写入文件内
 * @param client_addr   发送方服务地址，UDP套接口
 * @param server_socket_fd  Socket的文件描述符
 * @param client_addr_length    发送方地址长度
 * @return void
 */
void Listening(const struct sockaddr_in& client_addr, const int32_t server_socket_fd, socklen_t& client_addr_length) {
    double start, stop;
    int32_t id = 1;
    int32_t len = 0;
    char file_name[FILE_NAME_MAX_SIZE + 1];
    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
    FILE* fp = NULL;

    /* 设置超时自动关闭文件的时间 */
    struct timeval timeout;
    timeout.tv_sec = 0.5;  //秒
    timeout.tv_usec = 0;   //微秒
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        cerr << "setsockopt failed:" << endl;
        exit(EXIT_FAILURE);
    }

    while (1) {
        PacketHead ack;

        if ((len = recvfrom(server_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, &client_addr_length)) > 0) {
            // cout << "==========>"
            //      << ": Pack_Id :" << packet.head.id << " ID : " << id << endl;
            /* 如果是挥手包，则关闭文件 */
            if (packet.head.fin == 1) {
                max_length = packet.head.id;
                if (max_length > 0 && tot == max_length - 2) {
                    for (int i = 2; i < max_length; ++i) {
                        // cout << "i : " << i << " text: " << text_buf[i] << " size : " << buflen[i] << endl;
                        fwrite(text_buf[i], sizeof(char), buflen[i], fp);
                    }
                    fclose(fp);
                    GET_TIME(stop);
                    cout << "tot:" << tot << endl;
                    cout << "Receive File:\t" << file_name << " From Client IP Successful!" << endl;
                    cout << "Time Spending for Receiving:\t" << stop - start << endl;
                    /* 初始化各变量，等待下一个文件写入 */
                    id = 1;
                    len = 0;
                    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
                    fp = NULL;
                    max_length = -1;
                    memset(text_buf, 0, sizeof(text_buf));
                    memset(book, 0, sizeof(book));
                    tot = 0;
                }
                continue;
            }
            /* 打包ACK信息 */
            if (id == 1 && packet.head.syn != 1)
                continue;
            ack.id = packet.head.id;
            ack.buf_size = packet.head.buf_size;
            ack.syn = packet.head.syn;
            ack.fin = packet.head.fin;
            ++id;  // 待接收包的id++
            /* 发送数据包确认信息ACK */
            if (sendto(server_socket_fd, (char*)&ack, sizeof(ack), 0, (struct sockaddr*)&client_addr, client_addr_length) < 0) {
                cerr << "Send confirm information failed!" << endl;
            }
            // cout << "<<<<<<<<<<<<<<<<<<<" << ack.id << endl;
            /*  如果是握手包  */
            if (packet.head.syn == 1) {
                /*  从第一个包中读出文件名  */
                if (fp == NULL) {
                    strncpy(file_name, packet.buf, strlen(packet.buf) > FILE_NAME_MAX_SIZE ? FILE_NAME_MAX_SIZE : strlen(packet.buf));
                    /*  打开文件    */
                    fp = Create_And_Open_File(file_name);
                    cout << "Ready to Receive File:\t" << file_name << endl;
                    GET_TIME(start);
                }
            }
            /* 写入文件 */
            else {
                if (book[packet.head.id] == 0) {
                    bzero(text_buf[packet.head.id], BUFFER_SIZE);
                    my_strncpy(text_buf[packet.head.id], packet.buf, packet.head.buf_size);
                    buflen[packet.head.id] = packet.head.buf_size;
                    book[packet.head.id] = 1;
                    tot++;
                    if (max_length > 0 && tot == max_length - 2) {
                        for (int i = 2; i < max_length; ++i) {
                            fwrite(text_buf[i], sizeof(char), buflen[i], fp);
                        }
                        fclose(fp);
                        GET_TIME(stop);
                        cout << "Receive File:\t" << file_name << " From Client IP Successful!" << endl;
                        cout << "Time Spending for Receiving:\t" << stop - start << endl;
                        cout << "2. tot:" << tot << endl;
                        /* 初始化各变量，等待下一个文件写入 */
                        id = 1;
                        len = 0;
                        bzero(file_name, FILE_NAME_MAX_SIZE + 1);
                        fp = NULL;
                        max_length = -1;
                        memset(text_buf, 0, sizeof(text_buf));
                        memset(book, 0, sizeof(book));
                        tot = 0;
                    }
                }
            }

        } else {
            /* 如果此时没有文件被打开，则继续监听即可 */
            if (fp == NULL) {
                // cout << "click" << endl;
                continue;
            }
            /* 若超过时间限制后检测到有文件被打开，但没有收到挥手包使其被关闭，则将其关闭 */
            for (int i = 2; i <= tot + 1; ++i) {
                fwrite(text_buf[i], sizeof(char), buflen[i], fp);
            }
            fclose(fp);
            GET_TIME(stop);
            cout << "Receive File:\t" << file_name << " From Client IP Successful!" << endl;
            cout << "Time Spending for Receiving:\t" << stop - start << endl;
            /* 初始化各变量，等待下一个文件写入 */
            id = 1;
            len = 0;
            bzero(file_name, FILE_NAME_MAX_SIZE + 1);
            fp = NULL;
            max_length = -1;
            memset(text_buf, 0, sizeof(text_buf));
            memset(book, 0, sizeof(book));
            tot = 0;
        }
    }
    return;
}

int main() {
    /* 变量声明 */
    struct sockaddr_in server_addr;  // 服务接口地址
    int32_t server_socket_fd;        // socket声明

    Setup_ServerAndSocket_Server(server_addr, server_socket_fd);

    /* 数据传输 */

    /* 定义一个地址，用于捕获客户端地址 */
    struct sockaddr_in client_addr;
    socklen_t client_addr_length = sizeof(client_addr);

    Listening(client_addr, server_socket_fd, client_addr_length);

    close(server_socket_fd);
    return 0;
}