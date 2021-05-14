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

#define SERVER_PORT 8000
#define BUFFER_SIZE 1024
#define FILE_NAME_MAX_SIZE 512
#define FILE_NAME_MAX_SIZE 512
const char* server_ip = "172.19.5.118";

// 用于计算时刻
#define GET_TIME(now)                           \
    {                                           \
        struct timeval t;                       \
        gettimeofday(&t, NULL);                 \
        now = t.tv_sec + t.tv_usec / 1000000.0; \
    }

/* 包头 */
typedef struct
{
    int32_t id;
    int32_t buf_size;
    int16_t fin;
    int16_t syn;
} PackInfo;

/* 接收包 */
struct RecvPack {
    PackInfo head;
    char buf[BUFFER_SIZE];
} data;

/**
 * @brief 创建Server和Socket
 * @param server_addr   服务端地址
 * @param server_addr_length    服务端地址长度
 * @param client_socket_fd  Socket的文件描述符
 * @return void
 */
void Setup_ServerAndSocket_Cilent(struct sockaddr_in& server_addr, socklen_t& server_addr_length, int32_t& client_socket_fd) {
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
 * @brief 将文件分片发送到服务端
 * @param client_socket_fd  socket描述符
 * @param server_addr   接收方服务地址
 * @param server_addr_length   接收方服务地址长度
 * @param file_name    文件名
 * @return void
 */
void Post(int32_t client_socket_fd, struct sockaddr_in& server_addr, socklen_t& server_addr_length, const char* file_name) {
    int32_t len = 0;                      //  要发的文件段大小
    int32_t receive_id = 0, send_id = 0;  // 初始化接受包号和发送包号
    FILE* fp;                             // 文件指针
    double start, stop;                   //  记录时刻的变量， 用于算RTT

    /*  sockopt使能设置超时重传 */
    struct timeval timeout;
    timeout.tv_sec = 0;        //秒
    timeout.tv_usec = 100000;  //微秒
    if (setsockopt(client_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        cerr << "setsockopt failed:" << endl;  // 异常处理，设置socket超时字段失败
        exit(EXIT_FAILURE);
    }

    /* 每读取一段数据，便将其发给客户端 */
    while (1) {
        PackInfo pack_info;  // 报头，用于接收服务端传来的ACK

        if (receive_id == send_id) {  // 如果接收到了上一个包的ACK，才继续发送下一个包
            ++send_id;                // 已发送包的数量加一
            /*  如果发送的包是第一个包，将它的syn为置为1，buffer记录为文件名    */
            if (send_id == 1) {
                fp = fopen(file_name, "r");
                /* 异常处理：没有此文件 */
                if (NULL == fp) {
                    cout << "File:" << file_name << " Not Found. Please Enter a Existed File" << endl;
                    exit(EXIT_FAILURE);
                }
                bzero(data.buf, BUFFER_SIZE);
                /*  将文件名存入buffer内    */
                strncpy(data.buf, file_name, strlen(file_name) > BUFFER_SIZE ? BUFFER_SIZE : strlen(file_name));
                /* 将要发送的信息打包到报文头部 */
                data.head.id = send_id;
                data.head.buf_size = BUFFER_SIZE;
                data.head.fin = 0;
                data.head.syn = 1;
                cout << "data_size : " << sizeof(data) << endl;
                GET_TIME(start);
                /*   发送报文到接收端   */
                if (sendto(client_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                    cerr << "Send File Failed:" << endl;  // 异常处理：sendto函数调用失败
                    exit(EXIT_FAILURE);
                }
                int ret;  // 存recvfrom返回值
                /*  尝试接收接收方发来的ACK */
                if ((ret = recvfrom(client_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&server_addr, &server_addr_length)) < 0) {
                    cerr << "----TIME OUT----" << endl;  // 超时，准备重传
                    continue;
                }
                GET_TIME(stop);
                cout << "RTT : " << stop - start << endl;
                cout << "N0: " << send_id << endl;
                /* 更新receive_id */
                receive_id = pack_info.id;
                /* 以只读方式打开文件 */
            } else if ((len = fread(data.buf, sizeof(char), BUFFER_SIZE, fp)) > 0) {
                data.head.id = send_id;    // 发送id放进包头,用于标记顺序
                data.head.buf_size = len;  // 记录数据长度
                data.head.fin = 0;         // 不是挥手包
                data.head.syn = 0;         // 不是握手包
                cout << "data_size : " << sizeof(data) << endl;
                GET_TIME(start);
                /*   发送报文到接收端   */
                if (sendto(client_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                    cerr << "Send File Failed:" << endl;  // 异常处理：sendto函数调用失败
                    fclose(fp);
                    exit(EXIT_FAILURE);
                }
                int ret;  // 存recvfrom返回值
                /*  尝试接收接收方发来的ACK */
                if ((ret = recvfrom(client_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&server_addr, &server_addr_length)) < 0) {
                    cerr << "----TIME OUT----" << endl;  // 超时，准备重传
                    continue;
                }
                GET_TIME(stop);
                cout << "RTT : " << stop - start << endl;
                cout << "N0: " << send_id << endl;
                /* 更新receive_id */
                receive_id = pack_info.id;
            } else {
                data.head.fin = 1;  // 挥手包，表示文件传输结束
                if (sendto(client_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                    cerr << "Send File Failed!" << endl;
                    fclose(fp);
                    exit(EXIT_FAILURE);
                }
                break;
            }
        } else {
            /* 重传 */
            cout << "------resending------" << endl;
            if (sendto(client_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                cerr << "Send File Failed!" << endl;
                fclose(fp);
                exit(EXIT_FAILURE);
            }
            /* 接收ACK */
            recvfrom(client_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&server_addr, &server_addr_length);
            receive_id = pack_info.id;
        }
    }
    /* 关闭文件 */
    fclose(fp);
}

int main() {
    /*  计时变量    */
    double start, stop;

    /* 变量声明 */
    struct sockaddr_in server_addr;  // 服务端地址
    socklen_t server_addr_length;    // 服务端地址长度
    int32_t client_socket_fd;        // 套接字

    Setup_ServerAndSocket_Cilent(server_addr, server_addr_length, client_socket_fd);  // 创建服务器和套接字

    /* 输入文件名到缓冲区 */
    char file_name[FILE_NAME_MAX_SIZE + 1];
    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
    printf("Please Input File Name On Client: ");
    scanf("%s", file_name);

    GET_TIME(start);
    /*  主要逻辑，向服务端传输文件    */
    Post(client_socket_fd, server_addr, server_addr_length, file_name);
    GET_TIME(stop);

    cout << "File:" << file_name << " Transfer Successful!" << endl;
    cout << "Spend " << stop - start << " Seconds for Transferring" << endl;

    /*  关闭套接字  */
    close(client_socket_fd);
    return 0;
}