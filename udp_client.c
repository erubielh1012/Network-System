/* 
* udpclient.c - A simple UDP client
* usage: udpclient <host> <port>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <fcntl.h>
#include <sys/stat.h>

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

int main(int argc, char **argv) {
    int sockfd, portno;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFF_SIZE];
    int temp_type, temp_request_id, temp_transfer_id, temp_length;
    
    int serverlen = sizeof(serveraddr);

    /* check command line arguments */
    if (argc != 3) {
        fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        printf("Error: failed to open socket\n");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /* set the timeout for the socket */
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int request_id = 0;
    int transfer_id = 0;

    while (1) {
        temp_type = 0;
        temp_request_id = 0;
        temp_transfer_id = 0;
        temp_length = 0;
        request_id++;
        transfer_id = 0;
        /* Prompt client to choose a command */
        bzero(buf, BUFF_SIZE);
        printf("--------------------------------\n");
        printf("1. get [file_name]\n");
        printf("2. put [file_name]\n");
        printf("3. delete [file_name]\n");
        printf("4. ls\n");
        printf("5. exit\n");
        printf("Please enter a command: ");
        fgets(buf, BUFF_SIZE, stdin);    
        // printf("String entered: '%s'\n", buf);

        char *delimiter = " \n\r";
        char buf_copy[BUFF_SIZE];
        strcpy(buf_copy, buf);
        char *command = strtok(buf_copy, delimiter);
        char *file_name = NULL;
        char payload[PAYLOAD_SIZE];
        int nrecv = 0;
        bzero(payload, PAYLOAD_SIZE);
        strcpy(payload, command);
        char file_path[PATH_SIZE];
        strcpy(file_path, "./");
        
        if (strcmp(command, "get") == 0) {
            // set up command payload
            if ((file_name = strtok(NULL, delimiter)) != NULL) {
                strncat(file_path, file_name, PATH_SIZE - strlen(file_path) - 1);
            } else {
                printf("Error: No file name provided\n");
                continue;
            }

            int fd = open(file_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0) {
                printf("Error: Failed to open file %s\n", file_name);
                continue;
            }

            // build "get <file_name>" payload (must null-terminate before strncat)
            strcpy(payload, "get ");
            strncat(payload, file_name, PAYLOAD_SIZE - strlen(payload) - 1);

            // send "get" packet to server
            if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_CMD, transfer_id, payload) < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

            // get the file size from the server (discard stale packets from previous requests)
            // infinite loop to keep receiving packets until the server sends a response to request_id
            for (;;) {
                bzero(buf, BUFF_SIZE);
                if ((nrecv = recvfrom(sockfd, buf, BUFF_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen)) < 0) {
                    printf("Error: failed to receive packet\n");
                    break;
                }
                if (parse_packet(buf, nrecv, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, payload) < 0) {
                    continue; /* discard unparseable */
                }
                if (temp_type == TYPE_RESP && temp_request_id == request_id)
                    break;
                /* stale packet for different request_id, discard and recv again */
            }

            if (temp_type == TYPE_RESP && temp_request_id == request_id) {
                // parse the file size from the response
                char file_size_str[10];
                strncpy(file_size_str, strtok(payload, delimiter), 10);
                if (strlen(file_size_str) == 0) {
                    printf("Error: No file size provided\n");
                    continue;
                }
                ssize_t file_size = atoi(file_size_str);
                
                temp_type = 0;
                temp_request_id = 0;
                int read_len = 0;
                // loop to receive the file until the file size is reached
                while (read_len < file_size) {
                    /* receive the server's data response (Stop-and-Wait: one packet at a time) */
                    bzero(buf, BUFF_SIZE);
                    if ((nrecv = recvfrom(sockfd, buf, BUFF_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen)) < 0) {
                        printf("Error: failed to receive packet\n");
                        break;
                    }
                    if (parse_packet(buf, nrecv, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, payload) < 0) {
                        printf("Error: failed to parse packet\n");
                        continue;
                    }
                    if (temp_type == TYPE_DATA && temp_request_id == request_id && temp_length > 0) {
                        read_len += temp_length;
                        if (write(fd, payload, temp_length) < 0) {
                            printf("Error: Failed to write file %s\n", file_name);
                            break; // ideally you should try to request the same packet from server again
                        }
                        /* Stop-and-Wait: send ACK so server can send next packet */
                        if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_ACK, temp_transfer_id, "ack") < 0) {
                            printf("Error: failed to send ACK\n");
                            break;
                        }
                    }
                }
            }

            close(fd);
        } else if (strcmp(command, "put") == 0) {
            file_name = strtok(NULL, delimiter);
            if (!file_name) { printf("Error: No file name provided\n"); continue; }

            int fd = open(file_name, O_RDONLY);
            if (fd < 0) { printf("Error: File '%s' not found\n", file_name); continue; }

            // get the file size of expected to be sent
            struct stat st;
            if (stat(file_name, &st) == -1) {
                printf("Error: Failed to get file size\n");
            }
            size_t file_size = (size_t)st.st_size;
            char file_size_str[10];
            snprintf(file_size_str, 10, "%zu", file_size);

            // build "put <file> <file_size>" prefix
            strncat(payload, " ", PAYLOAD_SIZE - strlen(payload) - 1);
            strncat(payload, file_name, PAYLOAD_SIZE - strlen(payload) - 1);
            strncat(payload, " ", PAYLOAD_SIZE - strlen(payload) - 1);
            strncat(payload, file_size_str, PAYLOAD_SIZE - strlen(payload) - 1);
            
            // send a packet that checks if the server is ready to receive the file
            if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_CMD, transfer_id, payload) < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

            // wait for the server to send a clear (discard stale packets from previous requests)
            char req_payload[PAYLOAD_SIZE];
            for (;;) {
                bzero(buf, BUFF_SIZE);
                if ((nrecv = recvfrom(sockfd, buf, BUFF_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen)) < 0) {
                    printf("Error: failed to receive packet\n");
                    break;
                }
                if (parse_packet(buf, nrecv, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, req_payload) < 0) {
                    continue;
                }
                if (temp_type == TYPE_RESP && temp_request_id == request_id)
                    break;
                /* stale packet, discard and recv again */
            }

            if (temp_type == TYPE_RESP && temp_request_id == request_id) {
                /* Stop-and-Wait: send one packet, wait for ACK, then next */
                size_t nread = 0;
                const int max_retries = 5;
                while ((nread = read(fd, payload, PAYLOAD_SIZE)) > 0) {
                    int ack_received = 0;
                    for (int retry = 0; retry < max_retries && !ack_received; retry++) {
                        // send the data packet to the server
                        if (send_data_packet(sockfd, serveraddr, serverlen, request_id, transfer_id, payload, (int)nread) < 0) {
                            printf("Error: failed to send packet\n");
                            break;
                        }
                        /* Wait for ACK before sending next packet */
                        bzero(buf, BUFF_SIZE);
                        if ((nrecv = recvfrom(sockfd, buf, BUFF_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen)) >= 0) {
                            if (parse_packet(buf, nrecv, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, req_payload) >= 0
                                && temp_type == TYPE_ACK && temp_request_id == request_id && temp_transfer_id == transfer_id) {
                                ack_received = 1;
                            }
                        }
                        if (!ack_received && retry < max_retries - 1) {
                            printf("ACK timeout, retransmitting packet (transfer_id=%d)\n", transfer_id);
                        }
                    }
                    if (!ack_received) {
                        printf("Error: no ACK after %d retries\n", max_retries);
                        break;
                    }
                    transfer_id++;
                }
            }
            close(fd);

            if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_DONE, transfer_id, "done") < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

        } else if (strcmp(command, "delete") == 0) {
            file_name = strtok(NULL, delimiter);
            if (!file_name) { printf("Error: No file name provided\n"); continue; }
            strncat(payload, " ", PAYLOAD_SIZE - strlen(payload) - 1);
            strncat(payload, file_name, PAYLOAD_SIZE - strlen(payload) - 1);

            if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_CMD, transfer_id, payload) < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

            for (;;) {
                bzero(buf, BUFF_SIZE);
                if ((nrecv = recvfrom(sockfd, buf, BUFF_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen)) < 0) {
                    printf("Error: failed to receive packet\n");
                    break;
                }
                if (parse_packet(buf, nrecv, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, payload) < 0) {
                    continue;
                }
                if (temp_type == TYPE_DONE && temp_request_id == request_id)
                    break;
            }
            if (temp_type == TYPE_DONE && temp_request_id == request_id) {
                printf("File deleted successfully\n");
            } else {
                printf("Error: failed to delete file\n");
            }
        } else if (strcmp(command, "ls") == 0) {
            strncpy(payload, "ls", strlen("ls"));

            if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_CMD, transfer_id, payload) < 0) {
                printf("Error: failed to send packet\n");
                continue;
            }

            for (;;) {
                bzero(buf, BUFF_SIZE);
                if ((nrecv = recvfrom(sockfd, buf, BUFF_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen)) < 0) {
                    printf("Error: failed to receive packet\n");
                    break;
                }
                if (parse_packet(buf, nrecv, &temp_request_id, &temp_type, &temp_transfer_id, &temp_length, payload) < 0) {
                    continue;
                }
                if (temp_type == TYPE_DONE && temp_request_id == request_id)
                    break;
            }

        } else if (strcmp(command, "exit") == 0) {
            strncpy(payload, "exit", 5);  /* include null terminator */
            if (send_text_packet(sockfd, serveraddr, serverlen, request_id, TYPE_CMD, transfer_id, payload) < 0) {
                printf("Error: failed to send packet\n");
            }
            exit(0);
        } else {
            printf("Invalid command\n");
            continue;
        }

        // printf(" Payload: %s\n", payload);
        // printf(" Command: %s\n", command);
        // printf(" File name: %s\n", file_name);
    }
    return 0;
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
    packet[header_len + payload_len] = '\0';  /* null-terminate for debug printf */
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