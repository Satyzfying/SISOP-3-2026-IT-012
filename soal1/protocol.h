#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>

#define PORT 8080
#define IP_ADDRESS "127.0.0.1"

#define MAX_CLIENTS 100
#define MAX_NAME_LEN 50
#define BUFFER_SIZE 1024

typedef struct {
    int socket_fd;
    char username[MAX_NAME_LEN];
    int is_admin;
} NaviIdentity;

typedef struct {
    char sender[MAX_NAME_LEN];
    char content[BUFFER_SIZE];
    int is_rpc; 
} WiredPacket;

#define KNIGHTS_PASSWORD "admin123"

#endif