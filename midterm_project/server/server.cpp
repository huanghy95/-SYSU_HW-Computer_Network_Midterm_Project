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
#include <iostream>
#include <string>

using namespace std;

#define SERVER_PORT 8000
#define BUFFER_SIZE 1024
#define FILE_NAME_MAX_SIZE 512

double start, stop;

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
struct SendPack {
    PackInfo head;
    char buf[BUFFER_SIZE];
} data;

/**
 * @brief 创建Server和Socket
 * @param server_addr   UDP套接口
 * @param server_socket_fd  socket描述符
 * @return void
 */
void Setup_ServerAndSocket_Server(struct sockaddr_in& server_addr, int32_t& server_socket_fd) {
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    if (server_socket_fd == -1) {
        perror("Create Socket Failed:");
        exit(1);
    }

    /* 绑定套接口 */
    if (-1 == (bind(server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)))) {
        perror("Server Bind Failed:");
        exit(1);
    }
}

/**
 * @brief 创建并打开文件
 * @param file_name 要创建打开的文件名
 * @return File* 返回对应文件指针
 */
FILE* Create_And_Open_File(char* file_name) {
    FILE* fp = fopen(file_name, "w");
    if (NULL == fp) {
        printf("File:\t%s Can Not Open To Write\n", file_name);
    }
    return fp;
}

/**
 * @brief 监听客户端发来的分组并将其接收，写入文件内
 * @param server_addr   发送方服务地址，UDP套接口
 * @param client_socket_fd  Socket的文件描述符
 * @param server_addr_length    发送方地址长度
 * @return void
 */
void Listening(const struct sockaddr_in& server_addr, const int32_t client_socket_fd, socklen_t& server_addr_length) {
    int32_t id = 1;
    int32_t len = 0;
    char file_name[FILE_NAME_MAX_SIZE + 1];
    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
    FILE* fp;

    /*  sockopt使能设置超时重传 */
    struct timeval timeout;
    timeout.tv_sec = 10;  //秒
    timeout.tv_usec = 0;  //微秒
    if (setsockopt(client_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        cerr << "setsockopt failed:" << endl;
        exit(EXIT_FAILURE);
    }

    while (1) {
        PackInfo pack_info;

        if ((len = recvfrom(client_socket_fd, (char*)&data, sizeof(data), 0, (struct sockaddr*)&server_addr, &server_addr_length)) > 0) {
            if (data.head.fin == 1) {
                printf("Receive File:\t%s From Client IP Successful!\n", file_name);
                fclose(fp);
                id = 1;
                len = 0;
                bzero(file_name, FILE_NAME_MAX_SIZE + 1);
                fp = NULL;
            }
            if (data.head.id == id) {
                pack_info.id = data.head.id;
                pack_info.buf_size = data.head.buf_size;
                ++id;
                /* 发送数据包确认信息 */
                if (sendto(client_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                    printf("Send confirm information failed!");
                }
                /*  如果是握手包    */
                if (data.head.syn == 1) {
                    /*  从第一个包中读出文件名  */
                    strncpy(file_name, data.buf, strlen(data.buf) > FILE_NAME_MAX_SIZE ? FILE_NAME_MAX_SIZE : strlen(data.buf));
                    printf("%s\n", file_name);
                    /*  打开文件    */
                    fp = Create_And_Open_File(file_name);
                }
                /* 写入文件 */
                else if (fwrite(data.buf, sizeof(char), data.head.buf_size, fp) < data.head.buf_size) {
                    printf("File:\t%s Write Failed\n", file_name);
                    fclose(fp);
                    id = 1;
                    len = 0;
                    bzero(file_name, FILE_NAME_MAX_SIZE + 1);
                    fp = NULL;
                }
            } else if (data.head.id < id) /* 如果是重发的包 */
            {
                pack_info.id = data.head.id;
                pack_info.buf_size = data.head.buf_size;
                /* 重发数据包确认信息 */
                if (sendto(client_socket_fd, (char*)&pack_info, sizeof(pack_info), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                    printf("Send confirm information failed!");
                }
            } else {
            }
        } else {
            if (fp == NULL)
                continue;
            printf("Time Exceeded! Close File!\n");
            fclose(fp);
            id = 1;
            len = 0;
            bzero(file_name, FILE_NAME_MAX_SIZE + 1);
            fp = NULL;
        }
    }
    return;
}

/**
 * @brief 将文件分片发送到服务端
 * @param server_socket_fd  socket描述符
 * @param client_addr   接收方服务
 * @param client_addr_length    接收方服务长度
 * @param fp    文件指针
 * @return void
 */

int main() {
    /* 创建UDP套接口 */
    struct sockaddr_in server_addr;
    /* 创建socket */
    int32_t server_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    Setup_ServerAndSocket_Server(server_addr, server_socket_fd);

    /* 数据传输 */

    /* 定义一个地址，用于捕获客户端地址 */
    struct sockaddr_in client_addr;
    socklen_t client_addr_length = sizeof(client_addr);

    Listening(client_addr, server_socket_fd, client_addr_length);

    close(server_socket_fd);
    return 0;
}