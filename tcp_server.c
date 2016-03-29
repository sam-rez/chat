/******************************************************************************
 * tcp_server.c
 *
 * CPE 464 - Program 2
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#include "networks.h"

#define BUFFER_SIZE 1400
#define NORMAL_HEADER_SIZE 7

u_int16_t sequence_number = 0; //increment after each message sent
u_int32_t number_of_handles = 0;

struct normal_header{
    u_int32_t seq;     /* Sequence number */
    u_int16_t len;    /* Header length + paylaod */
    u_char flag;    /* 1 byte flag */
}__attribute__((packed));


struct clientNode{
    int sockfd;
    char * name;
    struct clientNode * next;
}*head;

void connection_accept(int*, int, int*, fd_set*);
void select_function(int, fd_set*);
void setup_fdvar(int, fd_set*, fd_set*);

//Managing client table
void appendHandle(int, char*);
void removeHandle(int);
void recv_data(int*, fd_set*, fd_set*, int, char*);
void recv_packet(char*, int, fd_set*, int);

//Handle different types of messages
void HandleInitialPacket(int i, char *);
void HandleMessage(char *, int);
void HandleList(int i, char *);
void PutHandlesIntoBuffer(char*);
void HandleBroadcast(char *, int, int, fd_set*);
void HandleExit(char *,int, fd_set*);

void SendFlag6(char*, char*, int, int, char*, int);
void SendFlag7(char * send_buf, char* dest_handle, int dest_handle_len, int source_socket);

int SearchTableAndGetSockfd(char*);
int CheckHandleAvailability(char*);

int main(int argc, char *argv[]){
    int server_socket = 0, portnumber = 0, fdmax = 0;;
    char buf[BUFFER_SIZE];
    fd_set fdvar, fdvar_backup; //make backup because select() clears it if nothing to do
    
    if(argc == 2) { portnumber = atoi(argv[1]); }
    server_socket = tcp_recv_setup(portnumber);
    tcp_listen(server_socket, 5);
    setup_fdvar(server_socket, &fdvar, &fdvar_backup);
    fdmax = server_socket;
    while(1){
        fdvar = fdvar_backup;
        select_function(fdmax, &fdvar);
        recv_data(&fdmax, &fdvar, &fdvar_backup, server_socket, buf);
    }
}


void setup_fdvar(int server_socket, fd_set *fdvar, fd_set *fdvar_backup){
    
    FD_ZERO(fdvar);
    FD_ZERO(fdvar_backup);
    FD_SET(server_socket, fdvar);
    FD_SET(server_socket, fdvar_backup);
}

void select_function(int fdmax, fd_set *fdvar){
    
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    //checks which sockets are ready in fdvar
    if(select(fdmax+1, (fd_set*)fdvar, (fd_set*)0, (fd_set*)0, &timeout) < 0){
        perror("select");
        exit(1);
    }
}

void recv_data(int *fdmax, fd_set *fdvar, fd_set *fdvar_backup, int server_socket, char *buf){

    int new_client_socket= 0, i = 0;       //socket descriptor for the client socket
    
    for(i = 0; i <= *fdmax; i++){
        if(FD_ISSET(i, fdvar)){
            if (i == server_socket){
                connection_accept(&new_client_socket, server_socket, fdmax, fdvar_backup);
            }else{
                recv_packet(buf, i, fdvar_backup, *fdmax);
            }
        }
    }
}


void recv_packet(char * buf, int client_socket, fd_set *fdvar_backup, int fdmax){
    
    u_int8_t flag= 0;
    int message_len= 0;         //length of the received message
    
    //receive message
    memset(buf, '\0', BUFFER_SIZE);
    if ((message_len = (int)recv(client_socket, buf, BUFFER_SIZE, 0)) < 0){
        perror("recv call");
        exit(-1);
    }
    if(message_len == 0){
        //close fd and continue
        removeHandle(client_socket);
        FD_CLR(client_socket, fdvar_backup);
        close(client_socket);
        return;
    }
    
    //check flag and run appropriate function.
    memcpy(&flag, buf+6, 1);
    
    switch (flag){
        case 1:
            HandleInitialPacket(client_socket, buf);
            break;
        case 4:
            HandleBroadcast(buf, client_socket, fdmax, fdvar_backup);
            break;
        case 5:
            HandleMessage(buf, client_socket);
            break;
        case 8:
            HandleExit(buf, client_socket, fdvar_backup);
            break;
        case 10:
            HandleList(client_socket, buf);
            break;
        default:
            //part of a longer message. just forward text to destination.
            //need to keep track of last destination sent by a client
            break;
    }
}

void connection_accept(int *new_client_socket, int server_socket, int *fdmax, fd_set *fdvar_backup){

    if ((*new_client_socket = accept(server_socket, (struct sockaddr*)0, (socklen_t *)0)) < 0){
        perror("accept call");
        exit(-1);
    }else{
        FD_SET(*new_client_socket, *(&fdvar_backup));
        if(*new_client_socket > *fdmax){
            *fdmax = *new_client_socket;
        }
    }
}

/* This function sets the server socket.  It lets the system
   determine the port number.  The function returns the server
   socket number and prints the port number to the screen.  */

int tcp_recv_setup(int portnumber)
{
    int server_socket= 0;
    struct sockaddr_in local;      /* socket address for local side  */
    socklen_t len= sizeof(local);  /* length of local address        */

    /* create the socket  */
    server_socket= socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0){
      perror("socket call");
      exit(1);
    }

    local.sin_family= AF_INET;         //internet family
    local.sin_addr.s_addr= INADDR_ANY; //wild card machine address
    local.sin_port= htons(portnumber);
    
    /* bind the name (address) to a port */
    if(bind(server_socket, (struct sockaddr *) &local, sizeof(local)) < 0){
        perror("bind call");
        exit(-1);
    }
    
    //get the port name and print it out
    if (getsockname(server_socket, (struct sockaddr*)&local, &len) < 0){
        perror("getsockname call");
        exit(-1);
    }

    printf("socket has port %d \n", ntohs(local.sin_port));
	        
    return server_socket;
}


void tcp_listen(int server_socket, int back_log)
{
    if (listen(server_socket, back_log) < 0){
      perror("listen call");
      exit(-1);
    }
}


void HandleInitialPacket(int sockfd, char * buf){
    
    char send_buf[BUFFER_SIZE];
    int clientHandleLength = buf[7]; //size of handle
    char *p = &buf[8];
    char handle[clientHandleLength + 1]; //plus one for null terminator
    int i = 0;
    
    for(i = 0; i < clientHandleLength; i++){
        handle[i] = *p;
        p++;
    }
    handle[clientHandleLength] = '\0';
    
    //check if handle is taken
    if(CheckHandleAvailability(handle)){
        //if handle available
        appendHandle(sockfd, handle);
        //create and send packet with flag 2
        struct normal_header nh;
        nh.seq = ntohl(sequence_number++);
        nh.len = ntohs(0);
        nh.flag = 2;
        
        memcpy(send_buf, &nh, NORMAL_HEADER_SIZE);
        int sent = (int)send(sockfd, send_buf, BUFFER_SIZE, 0);
        if(sent < 0){ perror("send call"); exit(-1); }
    }else{
        //handle is taken, send error packet and close socket.
        struct normal_header nh;
        nh.seq = ntohl(sequence_number++);
        nh.len = ntohs(0);
        nh.flag = 3;
        
        memcpy(send_buf, &nh, NORMAL_HEADER_SIZE);
        int sent = (int)send(sockfd, send_buf, BUFFER_SIZE, 0);
        if(sent < 0){ perror("send call"); exit(-1); }
    }
}


void HandleMessage(char * buf, int source_socket){
    
    char send_buf[BUFFER_SIZE];
    //get destintation handle length
    int dest_handle_len = buf[NORMAL_HEADER_SIZE];
    //buffer for destination handle
    char dest_handle[dest_handle_len + 1];

    //copy dest handle into dest_handle and null terminate
    memcpy(&dest_handle, &buf[NORMAL_HEADER_SIZE + 1], dest_handle_len);
    dest_handle[dest_handle_len] = '\0';
    
    int sock_fd = SearchTableAndGetSockfd(dest_handle);
    
    if(sock_fd == -1){
        SendFlag7(send_buf, dest_handle, dest_handle_len, source_socket);
    }else{
        SendFlag6(send_buf, buf, source_socket, sock_fd, dest_handle, dest_handle_len);
    }
}

void SendFlag7(char * send_buf, char* dest_handle, int dest_handle_len, int source_socket){
    //send flag 7
    struct normal_header nh;
    nh.seq = ntohl(sequence_number++);
    nh.len = 0;
    nh.flag = 7;
    
    memcpy(send_buf, &nh, NORMAL_HEADER_SIZE);
    memcpy(&send_buf[NORMAL_HEADER_SIZE], &dest_handle_len, 1);
    memcpy(&send_buf[NORMAL_HEADER_SIZE+1], dest_handle, dest_handle_len);
    int sent = (int)send(source_socket, send_buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
}

void SendFlag6(char * send_buf, char * buf, int source_socket, int sock_fd, char * dest_handle, int dest_handle_len){

    int sent = 0;
    struct normal_header nhToSrc;
    nhToSrc.seq = ntohl(sequence_number++);
    nhToSrc.len = 0;
    nhToSrc.flag = 6;
    
    memcpy(send_buf, &nhToSrc, NORMAL_HEADER_SIZE);
    memcpy(&send_buf[NORMAL_HEADER_SIZE + 1], &dest_handle_len, 1);
    memcpy(send_buf, dest_handle, dest_handle_len);
    
    //send confirmation message back to source
    sent = (int)send(source_socket, send_buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
    
    //send message to destination
    sent = (int)send(sock_fd, buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
}


void HandleList(int sockfd, char * buf){
    
    struct normal_header nh_flag11;
    char send_buf[BUFFER_SIZE];
    
    nh_flag11.seq = ntohl(sequence_number++);
    nh_flag11.len = ntohs(4);
    nh_flag11.flag = 11;
    
    memcpy(send_buf, &nh_flag11, NORMAL_HEADER_SIZE);
    memcpy(&send_buf[NORMAL_HEADER_SIZE], &number_of_handles, sizeof(u_int32_t));
    
    int sent = (int)send(sockfd, send_buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
    
    //flag 12
    PutHandlesIntoBuffer(send_buf);
    //send list back to socket
    sent = (int)send(sockfd, send_buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
    
    
}

void PutHandlesIntoBuffer(char * send_buf){
    
    struct normal_header nh;
    nh.seq = ntohl(sequence_number++);
    nh.len = 0;
    nh.flag = 12;
    
    memcpy(send_buf, &nh, NORMAL_HEADER_SIZE);
    
    struct clientNode * p = head;
    char *buffer_pointer = send_buf + 7;
    int handle_len;
    
    while(p){
        //put handle length in send_buf
        *buffer_pointer = handle_len = (int)strlen(p->name);
        buffer_pointer++;
        //copy handle into buffer
        memcpy(buffer_pointer, p->name, handle_len);
        buffer_pointer += handle_len;
        p = p->next;
    }
    
}

void HandleBroadcast(char * buf, int client_socket, int fdmax, fd_set *fdvar){
    
    //forward packet to everyone except socket
    int i = 0;
    for(i = 0; i <= fdmax; i++){
        if(FD_ISSET(i, fdvar) && i != client_socket && i != 3){
            int sent = (int)send(i, buf, BUFFER_SIZE, 0);
            if(sent < 0){
                perror("send call");
                exit(-1);
            }
        }
    }
}



void HandleExit(char * buf, int fd, fd_set *fdvar_backup){
    
    removeHandle(fd);
    struct normal_header nh;
    char send_buf[BUFFER_SIZE];
    
    nh.seq = ntohl(sequence_number++);
    nh.len = 0;
    nh.flag = 9;
    
    memcpy(&send_buf, &nh, NORMAL_HEADER_SIZE);
    int sent = (int)send(fd, send_buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
    
    //close socket and remove from set
    FD_CLR(fd, fdvar_backup);
    close(fd);
}


//Returns 1 if handle is available, 0 otherwise
int CheckHandleAvailability(char *handle){
    
    struct clientNode * p = head;
    
    if(!head){
        return 1;
    }
    while(p){
        if(strcmp(handle, p->name) == 0){
            return 0;
        }
        p = p->next;
    }
    return 1;
}


int SearchTableAndGetSockfd(char * the_handle){
    
    struct clientNode* p = head;
    
    while(p){
        if(strcmp(the_handle, p->name) == 0){
            return p->sockfd;
        }
        p = p->next;
    }
    return -1;
}


void appendHandle(int sockfd, char * handle){
    
    struct clientNode *newNode = (struct clientNode *)malloc(sizeof(struct clientNode));
    
    newNode->sockfd = sockfd;
    newNode->name = malloc(strlen(handle) + 1);
    strcpy(newNode->name, handle);
    
    //newNode->name = handle;
    newNode->next = NULL;
    
    if(head == NULL){
        head = newNode;
        number_of_handles++;
        return;
    }
    
    struct clientNode * p = head;
    while(p->next){
        p = p->next;
    }
    p->next = newNode;
    number_of_handles++;
}


void removeHandle(int fd){
    
    struct clientNode *p, *q, *check;
    p = q = check = head;
    
    //check if fd is in list
    while(check){
        if(check->sockfd == fd){
            break;
        }
        check = check->next;
    }
    
    if(check == NULL){
        return;
    }
    
    if(head->sockfd == fd){
        head = head->next;
        free(p->name);
        free(p);
        number_of_handles--;
        return;
    }
    
    p = head->next;
    while(p->sockfd != fd){
        q = p;
        p = p->next;
    }
    
    
    q->next = p->next;
    free(p->name);
    free(p);
    number_of_handles--;
}








