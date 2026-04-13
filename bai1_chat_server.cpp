#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        6000
#define MAX_CLIENTS 10
#define BUFSIZE     1024


typedef enum {
    STATE_WAIT_ID,    
    STATE_CHATTING   
} ClientState;

typedef struct {
    int         sock;
    ClientState state;
    char        clientName[128]; 
} ClientInfo;

void getTimeStr(char* buf, int bufSize)
{
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    snprintf(buf, bufSize, "%04d/%02d/%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
}

void broadcastMessage(ClientInfo clients[], int senderIdx, const char* msg, int msgLen)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i == senderIdx) continue;
        if (clients[i].sock < 0) continue;
        if (clients[i].state != STATE_CHATTING) continue;
        send(clients[i].sock, msg, msgLen, 0);
    }
}

int main()
{
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        perror("Khong tao duoc socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(PORT);

    if (bind(listenSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind that bai");
        close(listenSock);
        return 1;
    }

    if (listen(listenSock, 5) < 0) {
        perror("Listen that bai");
        close(listenSock);
        return 1;
    }

    printf("=== CHAT SERVER (select) ===\n");
    printf("Server dang lang nghe tren cong %d ...\n\n", PORT);

    ClientInfo clients[MAX_CLIENTS];
    int numClients = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].sock = -1;

    fd_set readSet;
    char buf[BUFSIZE];

    while (1) {
        FD_ZERO(&readSet);
        FD_SET(listenSock, &readSet);
        int maxFd = listenSock;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock >= 0) {
                FD_SET(clients[i].sock, &readSet);
                if (clients[i].sock > maxFd)
                    maxFd = clients[i].sock;
            }
        }

        int ret = select(maxFd + 1, &readSet, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select() that bai");
            break;
        }

        if (FD_ISSET(listenSock, &readSet)) {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int newSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
            if (newSock < 0) {
                perror("accept() that bai");
            } else {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].sock < 0) {
                        slot = i;
                        break;
                    }
                }
                if (slot == -1) {
                    printf("Server day, tu choi ket noi moi.\n");
                    const char* full = "Server day, vui long thu lai sau.\r\n";
                    send(newSock, full, strlen(full), 0);
                    close(newSock);
                } else {
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
                    printf("[+] Client moi ket noi: %s:%d (slot %d)\n",
                           ipStr, ntohs(clientAddr.sin_port), slot);

                    clients[slot].sock  = newSock;
                    clients[slot].state = STATE_WAIT_ID;
                    memset(clients[slot].clientName, 0, sizeof(clients[slot].clientName));
                    numClients++;

                    const char* msg = "=== CHAT SERVER ===\r\n"
                                      "Vui long nhap ten theo cu phap:\r\n"
                                      "client_id: ten_cua_ban\r\n"
                                      "Vi du: abc\r\n> ";
                    send(newSock, msg, strlen(msg), 0);
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock < 0) continue;
            if (!FD_ISSET(clients[i].sock, &readSet)) continue;

            memset(buf, 0, BUFSIZE);
            int bytesRecv = recv(clients[i].sock, buf, BUFSIZE - 1, 0);

            if (bytesRecv <= 0) {
                printf("[-] Client '%s' (slot %d) ngat ket noi.\n",
                       clients[i].clientName[0] ? clients[i].clientName : "unknown", i);

                if (clients[i].state == STATE_CHATTING) {
                    char notify[256];
                    snprintf(notify, sizeof(notify),
                             "*** %s da roi phong chat ***\r\n",
                             clients[i].clientName);
                    broadcastMessage(clients, i, notify, strlen(notify));
                }

                close(clients[i].sock);
                clients[i].sock  = -1;
                clients[i].state = STATE_WAIT_ID;
                numClients--;
                continue;
            }

            buf[strcspn(buf, "\r\n")] = '\0';

            switch (clients[i].state) {
                case STATE_WAIT_ID: {
                    char* colon = strchr(buf, ':');
                    char  name[128];
                    memset(name, 0, sizeof(name));

                    if (colon != NULL) {
                        int len = (int)(colon - buf);
                        if (len > 0 && len < 128) {
                            strncpy(name, buf, len);
                            name[len] = '\0';
                            char* start = name;
                            while (*start == ' ') start++;
                            memmove(name, start, strlen(start) + 1);
                            int end = strlen(name) - 1;
                            while (end >= 0 && name[end] == ' ') name[end--] = '\0';
                        }
                    }

                    if (name[0] == '\0') {
                        strncpy(name, buf, sizeof(name) - 1);
                        char* start = name;
                        while (*start == ' ') start++;
                        memmove(name, start, strlen(start) + 1);
                        int end = strlen(name) - 1;
                        while (end >= 0 && name[end] == ' ') name[end--] = '\0';
                    }

                    if (name[0] == '\0') {
                        const char* err = "Ten khong hop le. Vui long nhap lai:\r\n"
                                          "client_id: ten_cua_ban\r\n> ";
                        send(clients[i].sock, err, strlen(err), 0);
                    } else {
                        strncpy(clients[i].clientName, name, sizeof(clients[i].clientName) - 1);
                        clients[i].state = STATE_CHATTING;

                        printf("[slot %d] Client dang ky ten: '%s'\n", i, clients[i].clientName);

                        char welcome[256];
                        snprintf(welcome, sizeof(welcome),
                                 "Chao mung '%s' den phong chat! Bat dau chat:\r\n",
                                 clients[i].clientName);
                        send(clients[i].sock, welcome, strlen(welcome), 0);

                        char notify[256];
                        snprintf(notify, sizeof(notify),
                                 "*** %s da tham gia phong chat ***\r\n",
                                 clients[i].clientName);
                        broadcastMessage(clients, i, notify, strlen(notify));
                    }
                    break;
                }

                case STATE_CHATTING: {
                    char timeStr[64];
                    getTimeStr(timeStr, sizeof(timeStr));

                    char broadcastMsg[BUFSIZE + 256];
                    snprintf(broadcastMsg, sizeof(broadcastMsg),
                             "%s %s: %s\r\n",
                             timeStr, clients[i].clientName, buf);

                    printf("[CHAT] %s", broadcastMsg);
                    broadcastMessage(clients, i, broadcastMsg, strlen(broadcastMsg));
                    break;
                }

                default:
                    break;
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sock >= 0)
            close(clients[i].sock);
    }
    close(listenSock);
    return 0;
}
