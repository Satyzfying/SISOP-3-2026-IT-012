#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "protocol.h"

int active_sock = -1;

void handle_sigint(int sig) {
    (void)sig;
    const char notice[] = "\n[System] Disconnecting from The Wired...\n";
    write(STDOUT_FILENO, notice, sizeof(notice) - 1);
    if (active_sock >= 0) close(active_sock);
    _exit(0);
}

void *receive_handler(void *sock_desc) {
    int sock = *(int*)sock_desc;
    while (1) {
        WiredPacket pkg = {0};
        if (recv(sock, &pkg, sizeof(pkg), 0) <= 0) break;
        pkg.sender[MAX_NAME_LEN - 1] = '\0';
        pkg.content[BUFFER_SIZE - 1] = '\0';
        printf("\n[%s]: %s\n> ", pkg.sender, pkg.content);
        fflush(stdout);
    }
    return NULL;
}

void show_admin_menu() {
    printf("\n=== THE KNIGHTS CONSOLE ===\n");
    printf("1. Check Active Entities (Users)\n");
    printf("2. Check Server Uptime\n");
    printf("3. Execute Emergency Shutdown\n");
    printf("4. Disconnect\n");
}

int main() {
    int sock;
    struct sockaddr_in server = {0};
    pthread_t thread_id;
    NaviIdentity me = {0};
    char password[50];

    signal(SIGINT, handle_sigint);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    active_sock = sock;
    server.sin_addr.s_addr = inet_addr(IP_ADDRESS);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Enter your name: ");
    if (scanf(" %49[^\n]", me.username) != 1) {
        close(sock);
        return 1;
    }
    printf("Welcome to The Wired, %s\n", me.username);

    // Otentikasi khusus untuk The Knights (Poin 6)
    int is_admin = 0;
    me.is_admin = 0;
    if (strcmp(me.username, "The Knights") == 0) {
        printf("Enter Password: ");
        if (scanf("%49s", password) != 1) {
            close(sock);
            return 1;
        }
        
        if (strcmp(password, KNIGHTS_PASSWORD) == 0) {
            printf("\n[System] Authentication Successful. Granted Admin privileges.\n");
            is_admin = 1;
        } else {
            printf("\n[System] Authentication Failed. Access Denied.\n");
            close(sock);
            return 0;
        }
        me.is_admin = is_admin;
    }

    send(sock, &me, sizeof(NaviIdentity), 0);
    WiredPacket auth_res = {0};
    if (recv(sock, &auth_res, sizeof(auth_res), 0) <= 0) {
        printf("[System] Failed to register username.\n");
        close(sock);
        return 1;
    }
    auth_res.content[BUFFER_SIZE - 1] = '\0';
    if (strcmp(auth_res.content, "OK") != 0) {
        printf("[System] %s\n", auth_res.content);
        close(sock);
        return 1;
    }

    pthread_create(&thread_id, NULL, receive_handler, (void*)&sock);

    WiredPacket pkg = {0};
    strcpy(pkg.sender, me.username);

    if (is_admin) {
        show_admin_menu();
        while (1) {
            printf("Command >> ");
            int choice;
            if (scanf("%d", &choice) <= 0) break;
            
            pkg.is_rpc = 1;
            if (choice == 1) strcpy(pkg.content, "RPC_GET_USERS");
            else if (choice == 2) strcpy(pkg.content, "RPC_GET_UPTIME");
            else if (choice == 3) strcpy(pkg.content, "RPC_SHUTDOWN");
            else if (choice == 4) break;
            else continue;

            send(sock, &pkg, sizeof(WiredPacket), 0);
            sleep(1); // Memberi jeda agar balasan server tercetak dulu
        }
    } else {
        printf("Connected to The Wired. Type '/exit' to quit.\n");
        while (1) {
            printf("> ");
            char buf[BUFFER_SIZE];
            if (scanf(" %1023[^\n]", buf) != 1) break;

            pkg.is_rpc = 0;
            strcpy(pkg.content, buf);
            send(sock, &pkg, sizeof(WiredPacket), 0);
            if (strcmp(buf, "/exit") == 0) break;
        }
    }

    printf("[System] Disconnecting from The Wired...\n");
    close(sock);
    active_sock = -1;
    return 0;
}
