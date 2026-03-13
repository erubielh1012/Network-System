/* 
* udpserver.c - A simple UDP echo server 
* usage: udpserver <port>
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>

#define BUFF_SIZE 1024+96 // 1024 bytes for the payload, 96 bytes for the header
#define PAYLOAD_SIZE 1024
#define PATH_SIZE 512

#define TYPE_CMD 1
#define TYPE_RESP 2
#define TYPE_DATA 3
#define TYPE_DONE 4
#define TYPE_ACK  5

int parse_packet(char *packet, int packet_bytes, int *request_id, int *type, int *transfer_id, int *length, char *payload);
int send_text_packet(int sockfd, struct sockaddr_in serveraddr, socklen_t serverlen, int request_id, int type, int transfer_id, char *payload);
int send_data_packet(int sockfd, struct sockaddr_in serveraddr, socklen_t serverlen, int request_id, int transfer_id, const char *data, int data_len);
int get_file(int fd, char *rsp_payload);
int put_file(int *fd, const char *body, int body_len, char *rsp_payload);
int ls(char *rsp_payload);
int delete_file(char *file_name, char *rsp_payload);

/*
* error - wrapper for perror
*/
void error(char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    socklen_t clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    // struct hostent *hostp; /* client host info */
    // char *hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    int received; /* message byte size */
    int sent; /* message byte size */
    char rsp_payload[PAYLOAD_SIZE]; /* response payload to send*/
    char req_payload[PAYLOAD_SIZE]; /* request payload to send */
    int type, request_id, transfer_id, length; /* parsed from client packet header */
    char req_packet[BUFF_SIZE]; /* request packet to send */

    /* 
    * check command line arguments 
    */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    
    /* 
    * socket: create the parent socket 
    */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) error("ERROR opening socket");
    
    /* setsockopt: Handy debugging trick that lets 
    * us rerun the server immediately after we kill it; 
    * otherwise we have to wait about 20 secs. 
    * Eliminates "ERROR on binding: Address already in use" error. 
    * 
    * SO_REUSEADDR is a socket option that allows the socket to be 
    * reused immediately after it is closed (without waiting for 
    * the TIME_WAIT state to expire)
    */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));
    
    /*
    * build the server's Internet address
    */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);
    
    /* 
    * bind: associate the parent socket with a port 
    */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
        error("ERROR on binding");
    
    /* 
    * main loop: wait for a datagram, then echo it
    */
    clientlen = sizeof(clientaddr);
    for (;;) {
        /*
        * recvfrom: receive a UDP datagram from a client
        */
        bzero(req_packet, BUFF_SIZE);
        received = recvfrom(sockfd, req_packet, BUFF_SIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);
        if (received < 0) {
            printf("Error: failed to receive packet\n");
            continue;
            // at this point, we should use algorithm to resend the packet
        }

        /* parse the header: type: %d\r\nrequest_id: %d\r\nlength: %d\r\n\r\n%s */
        if (parse_packet(req_packet, received, &request_id, &type, &transfer_id, &length, req_payload) < 0) {
            printf("Error: failed to parse packet\n");
            continue;
        }

        // print the packet received and by whom
        // char client_ip[INET_ADDRSTRLEN];
        // inet_ntop(AF_INET, &clientaddr.sin_addr, client_ip, INET_ADDRSTRLEN);
        // int client_port = ntohs(clientaddr.sin_port);
        // printf("From: %s:%d\n", client_ip, client_port);
        // printf("Type: %d, Request ID: %u, Length: %d, Payload: '%s'\n", type, request_id, length, req_payload);

        if (type != TYPE_CMD) {
            strncpy(rsp_payload, "Error: Invalid type, request a command for server\n", PAYLOAD_SIZE);
        }

        /*
        * parse the command and file name from the payload
        */
        char *delimiter = " \n\r";
        char *cmd = strtok(req_payload, delimiter);
        char *file_name = NULL;
        char file_path[PATH_SIZE];
        strcpy(file_path, "./");
        if (cmd == NULL) {
            strncpy(rsp_payload, "Error: No command provided\n", PAYLOAD_SIZE);


        } else if (strcmp(cmd, "ls") == 0) {
            ls(rsp_payload);
            if (send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_DONE, 0, rsp_payload) < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

        } else if (strcmp(cmd, "get") == 0) {
            if ((file_name = strtok(NULL, delimiter)) == NULL) {
                printf("Error: No file name provided\n");
                continue;
            }

            // open the file
            strncat(file_path, file_name, PATH_SIZE - strlen(file_path) - 1);
            int fd = open(file_path, O_RDONLY);
            if (fd < 0) {
                printf("Error: Failed to open file '%s'\n", file_path);
                continue;
            }

            // get the file size of expected to be sent
            struct stat st;
            if (stat(file_name, &st) == -1) {
                printf("Error: Failed to get file size\n");
            }
            size_t file_size = (size_t)st.st_size;
            char file_size_str[10];
            snprintf(file_size_str, 10, "%zu", file_size);

            // send back a response with file size
            strcpy(rsp_payload, file_size_str);
            send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_RESP, -1, rsp_payload);

            /* Stop-and-Wait: set timeout for ACK wait */
            struct timeval ack_timeout;
            ack_timeout.tv_sec = 2;
            ack_timeout.tv_usec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &ack_timeout, sizeof(ack_timeout));

            size_t read_len = 0;
            size_t nread = 0;
            int chunk_id = 0;
            const int max_retries = 5;
            while (read_len < file_size) {
                bzero(rsp_payload, PAYLOAD_SIZE);

                if ((nread = read(fd, rsp_payload, PAYLOAD_SIZE)) > 0) {
                    read_len += nread;
                    int ack_received = 0;
                    for (int retry = 0; retry < max_retries && !ack_received; retry++) {
                        if (send_data_packet(sockfd, clientaddr, clientlen, request_id, chunk_id, rsp_payload, (int)nread) < 0) {
                            printf("Error: failed to send packet\n");
                            break;
                        }
                        /* Wait for ACK before sending next packet */
                        bzero(req_packet, BUFF_SIZE);
                        if ((received = recvfrom(sockfd, req_packet, BUFF_SIZE, 0, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen)) >= 0) {
                            int ack_rid, ack_type, ack_tid, ack_len;
                            if (parse_packet(req_packet, received, &ack_rid, &ack_type, &ack_tid, &ack_len, req_payload) >= 0
                                && ack_type == TYPE_ACK && ack_rid == request_id && ack_tid == chunk_id) {
                                ack_received = 1;
                            }
                        }
                        if (!ack_received && retry < max_retries - 1) {
                            printf("ACK timeout, retransmitting chunk %d\n", chunk_id);
                        }
                    }
                    if (!ack_received) {
                        printf("Error: no ACK after %d retries\n", max_retries);
                        break;
                    }
                    chunk_id++;
                } else {
                    printf("Error: Failed to read file\n");
                }
            }
            if (send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_DONE, -1, "done") < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }
            close(fd);


        } else if (strcmp(cmd, "put") == 0) {
            if ((file_name = strtok(NULL, delimiter)) != NULL) {
                strncat(file_path, file_name, PATH_SIZE - strlen(file_path) - 1);
            } else {
                strncpy(rsp_payload, "Error: No file name provided\n", PAYLOAD_SIZE);
                if (send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_DONE, -1, rsp_payload) < 0) {
                    printf("Error: failed to send packet\n");
                    continue;
                }
            }

            int fd = open(file_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0) {
                strncpy(rsp_payload, "Error: Failed to open file\n", PAYLOAD_SIZE);
                continue;
            }

            char file_size_str[10];
            strncpy(file_size_str, strtok(NULL, delimiter), 10);
            if (strlen(file_size_str) == 0) {
                strncpy(rsp_payload, "Error: No file size provided\n", PAYLOAD_SIZE);
                continue;
            }
            ssize_t file_size = atoi(file_size_str);

            printf("File name: %s\n", file_path);
            printf("File size: %ld\n", file_size);

            // send a packet to the client to inform them that the server is ready to receive the file
            sent = send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_RESP, -1, "clear");
            if (sent < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

            /* Stop-and-Wait: receive one data packet at a time, then send ACK */
            int temp_type, temp_request_id, temp_transfer_id, temp_length, received_len = 0;
            while (received_len < file_size) {
                printf("Waiting for data packet...\n");
                bzero(req_packet, BUFF_SIZE);
                if ((received = recvfrom(sockfd, req_packet, BUFF_SIZE, 0, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen)) < 0) {
                    printf("Error: failed to receive packet\n");
                    continue;
                }
                if (parse_packet(req_packet, received, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, req_payload) < 0) {
                    printf("Error: failed to parse packet\n");
                    continue;
                }

                if (temp_type == TYPE_DATA && temp_request_id == request_id && temp_length > 0) {
                    received_len += temp_length;
                    printf("Writing file...\n");
                    put_file(&fd, req_payload, temp_length, rsp_payload);
                    /* Send ACK so client sends next packet Stop-and-Wait*/
                    if (send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_ACK, temp_transfer_id, "ack") < 0) {
                        printf("Error: failed to send ACK\n");
                        continue;
                    }
                } else {
                    strncpy(rsp_payload, "Error: something went wrong with the file transfer\n", PAYLOAD_SIZE);
                    printf("Error: something went wrong with the file transfer\n");
                }
            }
            printf("File transfer complete\n");
            
            // send a packet to the client to inform them that the file transfer is done
            sent = send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_DONE, -1, "done");
            if (sent < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

        } else if (strcmp(cmd, "delete") == 0) {
            if ((file_name = strtok(NULL, delimiter)) != NULL) {
                strncat(file_path, file_name, PATH_SIZE - strlen(file_path) - 1);
                delete_file(file_path, rsp_payload);
            } else {
                strncpy(rsp_payload, "Error: No file name provided\n", PAYLOAD_SIZE);
            }
            if (send_text_packet(sockfd, clientaddr, clientlen, request_id, TYPE_DONE, -1, "done") < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }
        } else if (strcmp(cmd, "exit") == 0) {
            exit(0);
        } else {
            strncpy(rsp_payload, "Error: Invalid command\n", PAYLOAD_SIZE);
        }
    }
}

int parse_packet(char *packet, int packet_bytes, 
    int *request_id, int *type, 
    int *transfer_id, int *length, 
    char *payload) {
    /* parse the packet and return the type, request_id, transfer_id, and length */
    
    // find the header end marker
    char *header_end = strstr(packet, "\r\n\r\n");
    if (!header_end) return -1;

    printf("\nReceived packet: \n%s\n", packet);
    if (sscanf(packet, "request_id: %d\r\ntype: %d\r\ntransfer_id: %d\r\nlength: %d", request_id, type, transfer_id, length) != 4) {
        return -1;
    }

    if (*length < 0 || *length > PAYLOAD_SIZE) return -1;

    char *body = header_end + 4;
    int header_len = (int)(body - packet);
    int available_len = packet_bytes - header_len;

    if (*length > available_len) return -1;
    
    memcpy(payload, body, *length);
    payload[*length] = '\0';
    return 0;
}

int send_text_packet(int sockfd, struct sockaddr_in serveraddr, 
    socklen_t serverlen, int request_id, int type, 
    int transfer_id, char *payload) {
    /* build the UDP payload 
        request_id: [int]\r\n 
        type: [int]\r\n
        transfer_id: [int]\r\n
        length: [int]\r\n
        \r\n
        [payload]
    */
    if (!payload) return -1;

    char packet[BUFF_SIZE];
    int payload_len = strlen(payload);

    if (payload_len > PAYLOAD_SIZE) return -1;

    int header_len = snprintf(packet, BUFF_SIZE, 
        "request_id: %d\r\n"
        "type: %d\r\n"
        "transfer_id: %d\r\n"
        "length: %d\r\n"
        "\r\n", 
        request_id, type, transfer_id, payload_len);

    if (header_len < 0 || header_len >= BUFF_SIZE) return -1;

    if (header_len + payload_len > BUFF_SIZE) return -1;

    memcpy(packet + header_len, payload, payload_len);
    int packet_len = header_len + payload_len;

    printf("\nSent packet: \n%s\n", packet);
    return sendto(sockfd, packet, packet_len, 0, (struct sockaddr *)&serveraddr, (socklen_t)serverlen);
}

int send_data_packet(int sockfd, struct sockaddr_in serveraddr, 
        socklen_t serverlen, int request_id, 
        int transfer_id, const char *data, int data_len) {

    if (!data || data_len < 0 || data_len > PAYLOAD_SIZE) return -1;

    char packet[BUFF_SIZE];

    int header_len = snprintf(packet, BUFF_SIZE,
    "request_id: %d\r\n"
    "type: %d\r\n"
    "transfer_id: %d\r\n"
    "length: %d\r\n"
    "\r\n",
    request_id, TYPE_DATA, transfer_id, data_len);

    if (header_len < 0 || header_len >= BUFF_SIZE) return -1;
    if (header_len + data_len > BUFF_SIZE) return -1;

    memcpy(packet + header_len, data, data_len);

    int packet_len = header_len + data_len;

    return sendto(sockfd, packet, packet_len, 0, (struct sockaddr *)&serveraddr, serverlen);
}



int get_file(int fd, char *rsp_payload) {
    ssize_t n = read(fd, rsp_payload, PAYLOAD_SIZE);
    if (n < 0) {
        strncpy(rsp_payload, "Error: Failed to read file\n", PAYLOAD_SIZE);
        return -1;
    }
    return (int)n;
}

int put_file(int *fd, const char *body, int body_len, char *rsp_payload) {
    if (!body || body_len < 0 || body_len > PAYLOAD_SIZE) {
        strncpy(rsp_payload, "Error: Invalid body\n", PAYLOAD_SIZE);
        return -1;
    }
    
    size_t written = 0;
    size_t nw = 0;
    while (written < (size_t)body_len) {
        nw = write(*fd, body + written, body_len - written);
        if (nw < 0) {
            strncpy(rsp_payload, "Error: Failed to write file\n", PAYLOAD_SIZE);
            return -1;
        }
        written += nw;
    }

    strncpy(rsp_payload, "OK\n", PAYLOAD_SIZE);
    return written;
}

int ls(char *rsp_payload) {
    DIR *dir = opendir("."); // open the current directory
    struct dirent *entry; // pointer to the current directory entry
    while ((entry = readdir(dir)) != NULL) { // read the current directory entry
        strncat(rsp_payload, entry->d_name, strlen(entry->d_name));
        strncat(rsp_payload, "\n", 1);
    }

    closedir(dir);
    return 0;
}

int delete_file(char *file_path, char *rsp_payload) {
    // printf("Deleting file %s\n", file_path);
    if (unlink(file_path) == -1) {
        strncpy(rsp_payload, "Error: Failed to delete file\n", PAYLOAD_SIZE);
        return -1;
    }
    strncpy(rsp_payload, "OK\n", PAYLOAD_SIZE);
    return 0;
}

