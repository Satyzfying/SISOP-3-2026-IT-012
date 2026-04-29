#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include "protocol.h"

time_t start_time;
NaviIdentity clients[MAX_CLIENTS];

void write_log(const char* type, const char* detail) {
    FILE *fp = fopen("history.log", "a");
    if (!fp) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    fprintf(fp, "[%s] [%s] [%s]\n", timestamp, type, detail);
    fclose(fp);
}

void broadcast(WiredPacket pkg, int sender_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd > 0 && clients[i].socket_fd != sender_fd) {
            send(clients[i].socket_fd, &pkg, sizeof(pkg), 0);
        }
    }
}

int main() {
    int master_sock, new_sock, max_sd;
    struct sockaddr_in address = {0};
    fd_set readfds;
    start_time = time(NULL);

    master_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (master_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(master_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(master_sock);
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(master_sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(master_sock);
        return 1;
    }

    if (listen(master_sock, 10) < 0) {
        perror("listen");
        close(master_sock);
        return 1;
    }
    write_log("System", "SERVER ONLINE"); // Poin 7

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_sock, &readfds);
        max_sd = master_sock;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket_fd > 0) {
                FD_SET(clients[i].socket_fd, &readfds);
                if (clients[i].socket_fd > max_sd) max_sd = clients[i].socket_fd;
            }
        }

        select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(master_sock, &readfds)) {
            new_sock = accept(master_sock, NULL, NULL);
            NaviIdentity temp = {0};
            if (recv(new_sock, &temp, sizeof(NaviIdentity), 0) <= 0) {
                close(new_sock);
                continue;
            }
            temp.username[MAX_NAME_LEN - 1] = '\0';
            
            int exists = 0;
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(clients[i].socket_fd > 0 && strcmp(clients[i].username, temp.username) == 0) exists = 1;
            }

            if (exists) {
                WiredPacket res = {0};
                strcpy(res.sender, "System");
                snprintf(res.content, sizeof(res.content), "Username '%s' is already in use.", temp.username);
                send(new_sock, &res, sizeof(res), 0);
                close(new_sock);
            } else {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].socket_fd == 0) {
                        clients[i] = temp;
                        clients[i].socket_fd = new_sock;
                        WiredPacket res = {0};
                        strcpy(res.sender, "System");
                        strcpy(res.content, "OK");
                        send(new_sock, &res, sizeof(res), 0);
                        char detail[BUFFER_SIZE];
                        snprintf(detail, sizeof(detail), "User '%s' connected", clients[i].username);
                        write_log("System", detail);
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].socket_fd;
            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                WiredPacket pkg = {0};
                int valread = recv(sd, &pkg, sizeof(pkg), 0);
                pkg.sender[MAX_NAME_LEN - 1] = '\0';
                pkg.content[BUFFER_SIZE - 1] = '\0';
                
                if (valread <= 0 || strcmp(pkg.content, "/exit") == 0) {
                    char detail[BUFFER_SIZE];
                    snprintf(detail, sizeof(detail), "User '%s' disconnected", clients[i].username);
                    write_log("System", detail);
                    close(sd);
                    clients[i].socket_fd = 0;
                } else {
                    // Poin 6: Jalur RPC untuk The Knights
                    if (pkg.is_rpc) {
                        int client_is_admin = 0;
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].socket_fd == sd && clients[j].is_admin) {
                                client_is_admin = 1;
                                break;
                            }
                        }
                        
                        if (!client_is_admin) {
                            WiredPacket res = {0};
                            res.is_rpc = 1;
                            strcpy(res.sender, "System");
                            strcpy(res.content, "Error: Admin privileges required");
                            send(sd, &res, sizeof(res), 0);
                            continue;
                        }
                        
                        WiredPacket res = {0};
                        res.is_rpc = 1;
                        strcpy(res.sender, "System");

                        if (strcmp(pkg.content, "RPC_GET_USERS") == 0) {
                            int count = 0;
                            for(int j=0; j<MAX_CLIENTS; j++) if(clients[j].socket_fd > 0 && !clients[j].is_admin) count++;
                            sprintf(res.content, "Active Entities: %d", count);
                            write_log("Admin", "RPC_GET_USERS");
                        } 
                        else if (strcmp(pkg.content, "RPC_GET_UPTIME") == 0) {
                            time_t current = time(NULL);
                            double diff = difftime(current, start_time);
                            sprintf(res.content, "Server Uptime: %.f seconds", diff);
                            write_log("Admin", "RPC_GET_UPTIME");
                        }
                        else if (strcmp(pkg.content, "RPC_SHUTDOWN") == 0) {
                            write_log("Admin", "RPC_SHUTDOWN");
                            write_log("System", "EMERGENCY SHUTDOWN INITIATED");
                            exit(0);
                        }
                        else {
                            strcpy(res.content, "Error: Unknown RPC command");
                        }
                        send(sd, &res, sizeof(res), 0);
                    } else {
                        // Poin 5: Broadcast pesan
                        broadcast(pkg, sd);
                        char detail[BUFFER_SIZE + MAX_NAME_LEN + 5];
                        snprintf(detail, sizeof(detail), "[%s]: %s", pkg.sender, pkg.content);
                        write_log("User", detail);
                    }
                }
            }
        }
    }
    return 0;
}
