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
const char* server_ip = "172.19.10.14";

double start, stop;
int cnt = 0;

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
void Setup_ServerAndSocket_Cilent(struct sockaddr_in& server_addr, socklen_t& server_addr_length, int32_t client_socket_fd) {
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(SERVER_PORT);

    /* 创建socket */
    if (client_socket_fd < 0) {
        perror("Create Socket Failed:");
        exit(1);
    }
}

/**
 * @brief 将文件分片发送到服务端
 * @param server_socket_fd  socket描述符
 * @param client_addr   接收方服务
 * @param client_addr_length    接收方服务长度
 * @param fp    文件指针
 * @return void
 */
void Post(int32_t server_socket_fd, struct sockaddr_in& client_addr, socklen_t& client_addr_length, const char* file_name) {
    int32_t len = 0;
    int32_t receive_id = 0, send_id = 0;
    FILE* fp;

    /*  sockopt使能设置超时重传 */
    struct timeval timeout;
    timeout.tv_sec = 0;       //秒
    timeout.tv_usec = 100000;  //微秒
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        cerr << "setsockopt failed:" << endl;
        exit(EXIT_FAILURE);
    }

    /* 每读取一段数据，便将其发给客户端 */
    while (1) {
        PackInfo pack_info;

        if (receive_id == send_id) {
            ++send_id;
            if (send_id == 1) {
                bzero(data.buf, BUFFER_SIZE);
                strncpy(data.buf, file_name, strlen(file_name) > BUFFER_SIZE ? BUFFER_SIZE : strlen(file_name));

                data.head.id = send_id;
                data.head.buf_size = BUFFER_SIZE;
                data.head.fin = 0;
                data.head.syn = 1;
                cout << "data_size : " << sizeof(data) << endl;
                GET_TIME(start);
                if (sendto(server_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&client_addr, client_addr_length) < 0) {
                    perror("Send File Failed:");
                    break;
                }
                int ret;
                if ((ret = recvfrom(server_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&client_addr, &client_addr_length)) < 0) {
                    cerr << "recvfrom err:" << ret << endl;
                    continue;
                }
                GET_TIME(stop);
                cout << "RTT : " << stop - start << endl;
                cout << "N0: " << ++cnt << endl;
                receive_id = pack_info.id;
                fp = fopen(file_name, "r");
                cout << "correct here!" << endl;
                if (NULL == fp) {
                    printf("File:%s Not Found.\n", file_name);
                    exit(1);
                }
            } else if ((len = fread(data.buf, sizeof(char), BUFFER_SIZE, fp)) > 0) {
                data.head.id = send_id;   /* 发送id放进包头,用于标记顺序 */
                data.head.buf_size = len; /* 记录数据长度 */
                data.head.fin = 0;
                data.head.syn = 0;
                cout << "data_size : " << sizeof(data) << endl;
                GET_TIME(start);
                if (sendto(server_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&client_addr, client_addr_length) < 0) {
                    perror("Send File Failed:");
                    break;
                }
                int ret;
                if ((ret = recvfrom(server_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&client_addr, &client_addr_length)) < 0) {
                    cerr << "recvfrom err:" << ret << endl;
                    continue;
                }
                GET_TIME(stop);
                cout << "RTT : " << stop - start << endl;
                cout << "N0: " << ++cnt << endl;
                receive_id = pack_info.id;
            } else {
                data.head.fin = 1;  // 挥手包，表示文件传输结束
                if (sendto(server_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&client_addr, client_addr_length) < 0) {
                    perror("Send File Failed:");
                    break;
                }
                break;
            }
        } else {
            /* 如果接收的id和发送的id不相同,重新发送 */
            cout << "------resending------" << endl;
            if (sendto(server_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&client_addr, client_addr_length) < 0) {
                perror("Send File Failed:");
                break;
            }
            /* 接收确认消息 */
            recvfrom(server_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&client_addr, &client_addr_length);
            receive_id = pack_info.id;
        }
    }
    /* 关闭文件 */
    fclose(fp);
}

int main() {
    int32_t id = 1;

    /* 变量声明 */
    struct sockaddr_in server_addr;                             // 服务端地址
    socklen_t server_addr_length = sizeof(server_addr);         // 服务端地址长度
    int32_t client_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);  // 创建套接字，第二个参数代表类型为UDP

    Setup_ServerAndSocket_Cilent(server_addr, server_addr_length, client_socket_fd);  // 创建服务器和套接字

    /* 输入文件名到缓冲区 */
    char file_name[FILE_NAME_MAX_SIZE + 1];
    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
    printf("Please Input File Name On Client: ");
    scanf("%s", file_name);

    Post(client_socket_fd, server_addr, server_addr_length, file_name);

    printf("File:%s Transfer Successful!\n", file_name);

    close(client_socket_fd);
    return 0;
}