#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        7000
#define MAX_CLIENTS 10
#define BUFSIZE     4096
#define USER_FILE   "users.txt"     

typedef enum {
    STATE_WAIT_USER,   
    STATE_WAIT_PASS,   
    STATE_LOGGED_IN    
} TelnetState;

typedef struct {
    int         sock;
    TelnetState state;
    char        username[128];
} TelnetClient;

int checkLogin(const char* user, const char* pass)
{
    FILE* fp = fopen(USER_FILE, "r");
    if (fp == NULL) {
        printf("[!] Khong mo duoc file '%s'\n", USER_FILE);
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';

        char fileUser[128], filePass[128];
        if (sscanf(line, "%127s %127s", fileUser, filePass) == 2) {
            if (strcmp(user, fileUser) == 0 && strcmp(pass, filePass) == 0) {
                fclose(fp);
                return 1; 
            }
        }
    }

    fclose(fp);
    return 0; 
}

void executeCommand(int clientSock, const char* cmd)
{
    char sysCmd[512];
    snprintf(sysCmd, sizeof(sysCmd), "%s 2>&1", cmd);

    FILE* fp = popen(sysCmd, "r");
    if (fp == NULL) {
        const char* err = "Loi: Khong thuc thi duoc lenh.\r\n> ";
        send(clientSock, err, strlen(err), 0);
        return;
    }

    char readBuf[BUFSIZE];
    while (fgets(readBuf, sizeof(readBuf), fp) != NULL) {
        send(clientSock, readBuf, strlen(readBuf), 0);
    }
    pclose(fp);
    const char* prompt = "\r\n> ";
    send(clientSock, prompt, strlen(prompt), 0);
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

    // Bind
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

    // Listen
    if (listen(listenSock, 5) < 0) {
        perror("Listen that bai");
        close(listenSock);
        return 1;
    }

    printf("=== TELNET SERVER (select) ===\n");
    printf("Server dang lang nghe tren cong %d ...\n", PORT);
    printf("File tai khoan: %s\n\n", USER_FILE);

    TelnetClient clients[MAX_CLIENTS];
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
                    clients[slot].state = STATE_WAIT_USER;
                    memset(clients[slot].username, 0, sizeof(clients[slot].username));
                    numClients++;

                    const char* msg = "=== TELNET SERVER ===\r\n"
                                      "Vui long dang nhap:\r\n"
                                      "Username: ";
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
                       clients[i].username[0] ? clients[i].username : "unknown", i);
                close(clients[i].sock);
                clients[i].sock  = -1;
                clients[i].state = STATE_WAIT_USER;
                numClients--;
                continue;
            }

            buf[strcspn(buf, "\r\n")] = '\0';

            switch (clients[i].state) {
                case STATE_WAIT_USER: {
                    strncpy(clients[i].username, buf, sizeof(clients[i].username) - 1);
                    printf("[slot %d] Username: %s\n", i, clients[i].username);

                    const char* msg = "Password: ";
                    send(clients[i].sock, msg, strlen(msg), 0);
                    clients[i].state = STATE_WAIT_PASS;
                    break;
                }

                case STATE_WAIT_PASS: {
                    printf("[slot %d] Dang kiem tra dang nhap...\n", i);

                    if (checkLogin(clients[i].username, buf)) {
                        printf("[slot %d] Dang nhap thanh cong: %s\n", i, clients[i].username);
                        char welcome[256];
                        snprintf(welcome, sizeof(welcome),
                                 "\r\nDang nhap thanh cong! Chao mung '%s'.\r\n"
                                 "Nhap lenh de thuc thi (go 'exit' de thoat):\r\n> ",
                                 clients[i].username);
                        send(clients[i].sock, welcome, strlen(welcome), 0);
                        clients[i].state = STATE_LOGGED_IN;
                    } else {
                        printf("[slot %d] Dang nhap that bai cho user: %s\n", i, clients[i].username);
                        const char* err = "\r\nLoi dang nhap! Sai username hoac password.\r\n"
                                          "Ket noi se bi dong.\r\n";
                        send(clients[i].sock, err, strlen(err), 0);
                        close(clients[i].sock);
                        clients[i].sock  = -1;
                        clients[i].state = STATE_WAIT_USER;
                        numClients--;
                    }
                    break;
                }

                case STATE_LOGGED_IN: {
                    if (strlen(buf) == 0) {
                        const char* prompt = "> ";
                        send(clients[i].sock, prompt, strlen(prompt), 0);
                        break;
                    }

                    if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) {
                        const char* bye = "Tam biet! Dang dong ket noi...\r\n";
                        send(clients[i].sock, bye, strlen(bye), 0);
                        printf("[slot %d] Client '%s' thoat.\n", i, clients[i].username);

                        close(clients[i].sock);
                        clients[i].sock  = -1;
                        clients[i].state = STATE_WAIT_USER;
                        numClients--;
                        break;
                    }

                    printf("[slot %d] '%s' thuc thi lenh: %s\n", i, clients[i].username, buf);

                    executeCommand(clients[i].sock, buf);
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
