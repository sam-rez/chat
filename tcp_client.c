/******************************************************************************
 * tcp_client.c
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
#include "testing.h"

#define NORMAL_HEADER_SIZE 7
#define BUFFER_SIZE 1400
u_int32_t sequence_number= 0; //increment after each message sent

struct normal_header{
    u_int32_t seq;     /* Sequence number */
    u_int16_t len;    /* Header length + payload */
    u_char flag;    /* 1 byte flag */
}__attribute__((packed));


struct message_packet{
    struct normal_header normal_head; //seq num, length, flag
    u_char len_dest_handle;
    u_char* dest_handle_name; //no nulls or padding allowed, 255 chars max
    u_char len_src_handle;
    u_char* src_handle_name; //no nulls or padding allowed, 255 chars max
    u_char* text_message; //null terminated string
};

void HandleTaken(char*);
void send_first_packet(int, char*);
void select_function(int, fd_set*);
void setup_fdvar(int, fd_set*, fd_set*);
void ReadyForAction(int socket_num, char* handle, int my_handle_len, fd_set fdvar, fd_set fdvar_backup, char* argv);
void InvalidCommand();
void recv_from_stdin(int, char*, int);
void recv_data(int, fd_set*, char*, int, char*);
void sendMessage(int, char*, char*, int, int);
void SendRestOfMessage(char*, char*, int, int);
void sendBroadcast(char*, int, char*, int, int);
void sendExitRequest(int);
void sendListRequest(int);

void ParseIncomingPacket(char*, char*, int);
void ReceiveMessage(char*, int);
void ReceiveBroadcast(char*);
void ClientDoesNotExist(char*);
void ReceiveListFirstPacket(char*);
void ReceiveListSecondPacket(char*);
void ReceiveExit();


int main(int argc, char * argv[]){
    
    if(argc != 4){ printf("usage: %s handle host-name port-number \n", argv[0]); exit(1); }
    
    int socket_num, my_handle_len = (int)strlen(argv[1]);
    fd_set fdvar, fdvar_backup;
    char* handle = argv[1];
    
    socket_num = tcp_send_setup(argv[2], argv[3]);
    send_first_packet(socket_num, handle);
    setup_fdvar(socket_num, &fdvar, &fdvar_backup);
    printf("$:");
    ReadyForAction(socket_num, handle, my_handle_len, fdvar, fdvar_backup, argv[1]);
    return 0;
}


void ReadyForAction(int socket_num, char* handle, int my_handle_len, fd_set fdvar, fd_set fdvar_backup, char* argv){
    
    while(1){
        fdvar = fdvar_backup;
        fflush(stdout);
        select_function(socket_num, &fdvar);
        recv_data(socket_num, &fdvar, handle, my_handle_len, argv);
    }
    
}


void setup_fdvar(int socket_num, fd_set *fdvar, fd_set *fdvar_backup){
    
    FD_ZERO(fdvar);
    FD_ZERO(fdvar_backup);
    FD_SET(0, fdvar_backup);
    FD_SET(socket_num, fdvar_backup);
    
}

void recv_data(int socket_num, fd_set *fdvar, char * handle, int my_handle_len, char * argv){

    char recv_buf[BUFFER_SIZE];
    int message_len = 0;
    int i = 0;
    
    for(i = 0; i <= socket_num; i++){
        //check if stdin
        if(FD_ISSET(i, fdvar)){
            if(i == 0){
                recv_from_stdin(socket_num, handle, my_handle_len);
            }else{
                //receive data from socket and process
                message_len = (int)recv(socket_num, recv_buf, BUFFER_SIZE, 0);
                if(message_len < 0){
                    perror("recv call");
                    exit(-1);
                }
                if(message_len == 0){
                    printf("\nserver terminated\n");
                    exit(1);
                }
                ParseIncomingPacket(recv_buf, argv, my_handle_len);
            }
        }
    }
}


void select_function(int socket_num, fd_set *fdvar){
    
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    //checks which sockets are ready in fdvar
    if(select(socket_num+1, (fd_set*)fdvar, (fd_set*)0, (fd_set*)0, &timeout) < 0){
        perror("select");
        exit(1);
    }
}

void recv_from_stdin(int socket_num, char* handle, int my_handle_len){
   
    char send_buf[BUFFER_SIZE];
    int bytes_read = 0;
    
    memset(&send_buf, 0, BUFFER_SIZE);
    bytes_read = (int)read(0, send_buf, BUFFER_SIZE);
    
    //parse message and create packet
    if(send_buf[0] != '%'){
        InvalidCommand();
        return;
    }
    if(send_buf[1] == 'M' || send_buf[1] == 'm'){
        if(send_buf[2] != ' '){
            InvalidCommand();
            return;
        }
        sendMessage(socket_num, send_buf, handle, my_handle_len, bytes_read);
    }else if(send_buf[1] == 'B' || send_buf[1] == 'b'){
        if(send_buf[2] != ' '){
            InvalidCommand();
            return;
        }
        sendBroadcast(send_buf, socket_num, handle, my_handle_len, bytes_read);
    }else if(send_buf[1] == 'E' || send_buf[1] == 'e'){
        sendExitRequest(socket_num);
    }else if(send_buf[1] == 'L' || send_buf[1] == 'l'){
        sendListRequest(socket_num);
    }else{
        InvalidCommand();
    }
}

void InvalidCommand(){
    printf("Invalid Command.\n");
    fflush(stdin);
    printf("$:");
}

int tcp_send_setup(char *host_name, char *port){
    
    int socket_num;
    struct sockaddr_in remote;       // socket address for remote side
    struct hostent *hp;              // address of remote host

    // create the socket
    if((socket_num = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    { perror("socket call"); exit(-1); }
    
    // designate the addressing family
    remote.sin_family= AF_INET;

    // get the address of the remote host and store
    if ((hp = gethostbyname(host_name)) == NULL){
	  printf("Error getting hostname: %s\n", host_name);
	  exit(-1);
	}
    memcpy((char*)&remote.sin_addr, (char*)hp->h_addr, hp->h_length);

    // get the port used on the remote side and store
    remote.sin_port= htons(atoi(port));

    if(connect(socket_num, (struct sockaddr*)&remote, sizeof(struct sockaddr_in)) < 0)
    { perror("connect call"); exit(-1);}
    return socket_num;
}


void send_first_packet(int socket_num, char* handle){
    
    struct first_packet{
        struct normal_header nh;
        u_int8_t hl;
        char * handle;
    };
    struct first_packet fp;
    int sent = 0;
    char buf[BUFFER_SIZE];
    
    fp.nh.seq = sequence_number++;
    fp.nh.len = 0;
    fp.nh.flag = 1; //client to server flag
    fp.hl = strlen(handle);
    fp.handle = malloc(strlen(handle));
    memcpy(fp.handle, handle, strlen(handle));
    memset(buf, 0, BUFFER_SIZE);
    memcpy(buf, &fp, BUFFER_SIZE);
    memcpy(buf+8, handle, strlen(handle));
    
    sent = (int)send(socket_num, &buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
}


void HandleTaken(char * handle){
    printf("Handle already in use: %s\n", handle);
    exit(1);
}


void ParseIncomingPacket(char * recv_buf, char * handle, int my_handle_len){

    struct normal_header nh;
    memcpy(&nh, recv_buf, NORMAL_HEADER_SIZE);
    switch(nh.flag){
        case 3:
            //client handle taken, exit
            HandleTaken(handle);
            break;
        case 4:
            //receive broadcast message
            ReceiveBroadcast(recv_buf);
            break;
        case 5:
            //receive message and print to stdout
            ReceiveMessage(recv_buf, my_handle_len);
            break;
        case 6:
            //client found destination client, don't do anything
            break;
        case 7:
            //tell client that destination not found
            ClientDoesNotExist(recv_buf);
            break;
        case 9:
            //ACK the clients exit request then exit
            ReceiveExit();
            break;
        case 11:
            //numer of handles on server
            ReceiveListFirstPacket(recv_buf);
            break;
        case 12:
            //the handles currently on the server
            ReceiveListSecondPacket(recv_buf);
            break;
        default:
            //extra data, just print it out.
            break;
    }
}


void sendMessage(int server_socket, char * send_buf, char*handle, int my_handle_len, int bytes_read){
    
    char packet[BUFFER_SIZE];
    int dest_handle_len = 0;
    struct normal_header nh;
    
    nh.seq = ntohl(sequence_number++);
    nh.len = ntohs(bytes_read);
    nh.flag = 5;
    
    memcpy(packet, &nh, NORMAL_HEADER_SIZE);
    
    //get client handle and size
    char *p = &send_buf[3];
    int i = 8;
    
    while(*p != ' '){
        if(*p == '\n') //nothing in message field
            break;
        packet[i] = *p;
        dest_handle_len++;
        i++; p++;
    }
    packet[7] = dest_handle_len;
    
    //enter source handle len and name into packet
    packet[7+1+dest_handle_len] = my_handle_len;
    memcpy(&packet[7+1+dest_handle_len+1], handle, my_handle_len);
    
    //copy message into packet
    p = &send_buf[3 + dest_handle_len + 1]; //find start of message from user input
    i = 7 + 1 + dest_handle_len + 1 + my_handle_len; //place in packet to start writing message
    
    if(*(p - 1) != '\n'){
        while(*p != '\n'){ //need to add case where bytes_read > 1400
            packet[i] = *p;
            i++; p++;
        }
        packet[i] = '\0'; //null terminate text message
    }else{
        packet[i] = '\0';
    }
    
    int sent = (int)send(server_socket, packet, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
    
    //if bytes_read == 1400 there is more info to process from stdin
    if(bytes_read == BUFFER_SIZE){
        SendRestOfMessage(packet, send_buf, server_socket, bytes_read);
    }
    printf("$:");
    fflush(stdout);
}


void SendRestOfMessage(char* packet, char * send_buf, int server_socket, int bytes_read){
    
    int total_bytes_read = bytes_read;
        //keep reading from stdin
        while((bytes_read = (int)read(0, send_buf, BUFFER_SIZE)) > 0){
            
            total_bytes_read += bytes_read;
            if(total_bytes_read > 32000){
                int keep_reading_bytes = total_bytes_read;
                //message too long but keep reading to find total number of bytes
                while((bytes_read = (int)read(0, send_buf, BUFFER_SIZE)) > 0){
                    keep_reading_bytes += bytes_read;
                }
                printf("Message is %d bytes, this is too long. Message truncated to 32k bytes.\n", keep_reading_bytes);
            }
            
            int sent = (int)send(server_socket, packet, BUFFER_SIZE, 0);
            if(sent < 0){ perror("send call"); exit(-1); }
        }
}

void sendListRequest(int server_socket){
    
    char send_buf[BUFFER_SIZE];
    struct normal_header nh;
    int sent;
    
    nh.seq = ntohl(sequence_number++);
    nh.len = 0;
    nh.flag = 10;
    
    memcpy(&send_buf, &nh, NORMAL_HEADER_SIZE);
    
    sent = (int)send(server_socket, send_buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
}

void sendBroadcast(char * send_buf, int server_socket, char* handle, int my_handle_length, int bytes_read){
 
    char *p;
    char packet[BUFFER_SIZE];
    
    memset(packet, 0, BUFFER_SIZE);
    struct normal_header nh;
    nh.seq = ntohl(sequence_number++);
    nh.len = ntohs(bytes_read);
    nh.flag = 4;
    
    //copy handle len and handle
    memcpy(packet, &nh, NORMAL_HEADER_SIZE);
    p = packet + NORMAL_HEADER_SIZE;
    *p = my_handle_length;
    p++;
    
    memcpy(p, handle, my_handle_length);
    p += my_handle_length;
    
    //copy message
    memcpy(p, send_buf+3, bytes_read);
    int sent = (int)send(server_socket, packet, BUFFER_SIZE, 0);
    if(sent < 0){ perror("send call"); exit(-1); }
    printf("$:");
    fflush(stdout);
    
}


void sendExitRequest(int server_socket){
    
    char buf[BUFFER_SIZE];
    struct normal_header nh;
    nh.seq = ntohl(sequence_number++);
    nh.len = 0;
    nh.flag = 8;
    
    memcpy(&buf, &nh, NORMAL_HEADER_SIZE);

    int sent = (int)send(server_socket, buf, BUFFER_SIZE, 0);
    if(sent < 0){ perror("sent"); exit(-1);}
}


void ReceiveBroadcast(char * message_packet){
    
    //get payload
    struct normal_header nh;
    memcpy(&nh, message_packet, NORMAL_HEADER_SIZE);
    int payload = htons(nh.len);
    //unpack handle
    int their_handle_len = message_packet[NORMAL_HEADER_SIZE];
    
    char*p = &message_packet[NORMAL_HEADER_SIZE + 1];
    
    int i = 0;
    //print out handle
    printf("\n");
    while(i < their_handle_len){
        printf("%c", *p);
        i++; p++;
    }
    printf(": ");
    
    //print message
    while(payload > 0){
        printf("%c", *p);
        payload--;
        p++;
    }
    printf("$:");
    fflush(stdout);
}

void ReceiveMessage(char * message_packet, int my_handle_len){
    
    int their_handle_len = message_packet[NORMAL_HEADER_SIZE + 1 + my_handle_len];
    int i = 0;
    printf("\n");
    
    char *p = &message_packet[NORMAL_HEADER_SIZE + 1 + my_handle_len + 1];
    
    //print out handle
    while(i < their_handle_len){
        printf("%c", *p);
        i++; p++;
    }
    printf(": ");
    fflush(stdout);
    //print message

    while(*p != '\0'){
        printf("%c", *p);
        p++;
    }
    printf("\n$:");
    fflush(stdout);
    
}

void ClientDoesNotExist(char * recv_buf){
    
    int handle_len = recv_buf[NORMAL_HEADER_SIZE];
    char * p = &recv_buf[8];
    
    printf("Client with handle ");
    
    while(handle_len > 0){
        printf("%c", *p);
        p++;
        handle_len--;
    }
    
    printf(" does not exist.\n$:");
    
}

void ReceiveListFirstPacket(char * recv_buf){
    
    printf("Number of Handles: ");
    u_int32_t num_handles;
    
    memcpy(&num_handles, &recv_buf[NORMAL_HEADER_SIZE], sizeof(u_int32_t));
    printf("%d\n$:", num_handles);
    fflush(stdout);
}

void ReceiveListSecondPacket(char * recv_buf){

    char * p = recv_buf + 7;
    int current_handle_length;
    
    while(*p != '\0'){
        current_handle_length = *p;
        p++;
        while(current_handle_length > 0){
            printf("%c", *p);
            current_handle_length--;
            p++;
        }
        printf("\n");
    }
    printf("$:");
    fflush(stdout);
}


void ReceiveExit(){
    exit(0);
}














