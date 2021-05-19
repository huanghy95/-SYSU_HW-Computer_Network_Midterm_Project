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
#include <string>

using namespace std;

#define SERVER_PORT 8000                  // 服务端口
#define BUFFER_SIZE 1024                  // 数据段长度
#define FILE_NAME_MAX_SIZE 512            // 文件名最大长度
#define LENGTH 256

const char* server_ip = "172.19.11.160";  // 服务端IP

bool has_acked[10240] = {0};
sem_t sem;
sem_t sem2;
int FLAG = -1;
int ending = 0;

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
 * @brief 将文件分片发送到服务端
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
    // int32_t send_id = 0;  // 初始化接受包号和发送包号
    FILE* fp;  // 文件指针
    // double start, stop;                   //  记录时刻的变量， 用于算RTT

    cout << "-----2-----" << endl;

    /* 每读取一段数据，便将其发给客户端 */
    int base = 1;
    bool finish = 0;
    while (1) {
        bool check = 1;
        for (int i = base; i < base + LENGTH; ++i) {
            if (FLAG > 0 && i >= FLAG)
                break;
            // cout << "3------ : " << i << endl;

            if (has_acked[i] == 0) {
                // cout << "4------ : " << i << endl;
                check = 0;
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
                    cout << "data_size : " << sizeof(packet) << endl;
                    /*   发送报文到接收端   */
                    if (sendto(client_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                        cerr << "Send File Failed:" << endl;  // 异常处理：sendto函数调用失败
                        exit(EXIT_FAILURE);
                    }
                    // cout << "RTT : " << stop - start << endl;
                    cout << "N0: " << i << endl;
                    /* 更新receive_id */
                } else {
                    // FILE* fp_offset = fp;
                    cout << "++++++++" << (BUFFER_SIZE * (i - 2)) << endl;
                    fseek(fp, (BUFFER_SIZE * (i - 2)), 1);
                    // cout << "--------------++++++" << fp_offset - fp << endl;
                    len = fread(packet.buf, 1, BUFFER_SIZE, fp);
                    fseek(fp, -(min(len, BUFFER_SIZE)), 1);
                    fseek(fp, -(BUFFER_SIZE * (i - 2)), 1);
                    cout << "len : " << len << " i: " << i << endl;
                    if (len > 0) {
                        packet.head.id = i;          // 发送id放进包头,用于标记顺序
                        packet.head.buf_size = len;  // 记录数据长度
                        packet.head.fin = 0;         // 不是挥手包
                        packet.head.syn = 0;         // 不是握手包
                        cout << "data_size : " << sizeof(packet) << endl;
                        /*   发送报文到接收端   */
                        if (sendto(client_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                            cerr << "Send File Failed:" << endl;  // 异常处理：sendto函数调用失败
                            fclose(fp);
                            exit(EXIT_FAILURE);
                        }
                        // cout << "RTT : " << stop - start << endl;
                        cout << "N0: " << i << endl;
                        // /* 更新receive_id */
                        // receive_id = ack.id;
                    } else {
                        packet.head.fin = 1;  // 挥手包，表示文件传输结束
                        cout << "final--------=========" << endl;
                        if (sendto(client_socket_fd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, server_addr_length) < 0) {
                            cerr << "Send File Failed!" << endl;
                            fclose(fp);
                            exit(EXIT_FAILURE);
                        }
                        finish = 1;
                        // sem_wait(&sem2);
                        FLAG = i;
                        // sem_post(&sem2);
                        cout << "flag : +++++++++++++++++++++++++++++++++++++++++++++++++++++++" << FLAG << endl;
                        // cout << "finish:" << finish << endl;
                        break;
                    }
                }
            }
        }
        if (ending) return nullptr;
        if (check) {
            // cout<<"fuckdsfds finish : " << finish <<endl;
            base += LENGTH;
            if (finish) {
                break;
            }
        }
        sleep(0.05);
        // fseek(fp, (BUFFER_SIZE * 1024), 1);
    }
    /* 关闭文件 */
    // cout << "////////////////////////////" << endl;
    fclose(fp);
    return nullptr;
}

void* Get_ACK(void* arg) {
    struct parameter* p = (parameter*)arg;
    int32_t client_socket_fd = p->client_socket_fd;
    struct sockaddr_in server_addr = p->server_addr;
    socklen_t server_addr_length = p->server_addr_length;

    while (1) {
        PacketHead ack;
        int ret;  // 存recvfrom返回值
        /*  尝试接收接收方发来的ACK */
        if ((ret = recvfrom(client_socket_fd, (char*)&ack, sizeof(ack), 0, (struct sockaddr*)&server_addr, &server_addr_length)) < 0) {
            cerr << "----FINISH----" << endl;  // 超时，准备重传
            ending = 1;
            return nullptr;
        }
        // if (FLAG == 1)
        //     return nullptr;
        // cout << "RTT : " << stop - start << endl;
        // cout << "N0: " << send_id << endl;
        /* 更新receive_id */
        // sem_wait(&sem);
        has_acked[ack.id] = 1;
        // sem_post(&sem);
        cout << ">>>>>>>" << ack.id << endl;
    }
}

int main() {
    /*  计时变量    */
    double start, stop;

    /* 变量声明 */
    struct sockaddr_in server_addr;  // 服务端地址
    socklen_t server_addr_length;    // 服务端地址长度
    int32_t client_socket_fd;        // 套接字

    Setup_ServerAndSocket_Client(server_addr, server_addr_length, client_socket_fd);  // 创建服务器和套接字

    /*  sockopt使能设置超时重传 */
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

    GET_TIME(start);
    /*  主要逻辑，向服务端传输文件    */

    long thread;                // 线程号
    pthread_t* thread_handles;  // 线程指针

    // 为线程指针申请内存

    int thread_count = 2;

    thread_handles = (pthread_t*)malloc(thread_count * sizeof(pthread_t));

    sem_init(&sem, 0, 1);
    sem_init(&sem2, 0, 1);

    // 开启thread_count个线程，每个线程启用线程函数

    struct parameter* par = new parameter;
    par->client_socket_fd = client_socket_fd;
    par->file_name = file_name;
    par->server_addr = server_addr;
    par->server_addr_length = server_addr_length;

    cout << "--------1---------" << endl;

    pthread_create(&thread_handles[0], NULL, Get_ACK,
                   (void*)par);
    pthread_create(&thread_handles[1], NULL, Post,
                   (void*)par);
    // for (thread = 0; thread < thread_count; thread++) {
    //     struct parameter* par = new parameter;
    //     par->rank = (void*)thread;
    //     par->len = cnt;
    //     par->ar = b;
    //     pthread_create(&thread_handles[thread], NULL, Thread_work,
    //                    (void*)par);
    // }

    // 同步线程
    for (thread = 0; thread < thread_count; thread++)
        pthread_join(thread_handles[thread], NULL);

    // Post(client_socket_fd, server_addr, server_addr_length, file_name);
    GET_TIME(stop);

    cout << "File:" << file_name << " Transfer Successful!" << endl;
    cout << "Spend " << stop - start << " Seconds for Transferring" << endl;

    free(thread_handles);
    sem_destroy(&sem);

    /*  关闭套接字  */
    close(client_socket_fd);
    return 0;
}