# UDP Sockets

This program is meant to establish a UDP connection between a client and a remote server.


## Client
To run, use the command below:
> ./client [SERVER IP ADDRESS] [SERVER PORT NUMBER]

The program will be expecting a user input from the following commands:
* get [file_name]
* put [file_name]
* delete [file_name]
* ls
* exit

### Basic Mechanism
1. Verify that the syntax of the user's input is correct
2. Client will send a datagram to the server indicating which operation is required and any other data or parameters needed to satisfy the request.  
3. The client must then wait for the server's response.  
4. If the server fails to respond after an appropriate amount of time, an error message must be emitted to stderr.

### Client Response
Once the server responds, your client will print an appropriate message to stdout, indicating the result of the requested operation. In the case of a **put**, the client should source the indicated file in the current working directory.  In the case of a **get**, the client should store the returned file in the current working directory. For the **exit** command, the server should exit gracefully.

## Server
To run, specify what port to use (select port #s > 5000)
> ./server 5001

### Basic Mechanism
1. Your server should wait for a UDP connection after binding to requested port (ie. 5001 in the example above).  
2. Depending on the commands received, the server responds to the client's request in the following manner:
    * get [file_name]
        * the server transmits the requested file to the client
    * put [file_name]
        * the server receives the transmitted file from the client and stores it locally
    * delete [file_name] 
        * the server deletes the file if it exists
    * ls 
        * the server should search all the files it has in its current working directory and send a list of all these files to the client
    * exit 
        * the server should terminate gracefully
For any other command, the server should respond that the given command was not understood.  
3. In all of these cases, if the server is unable to perform an operation, it should respond to the client indicating a failure. 


## Headers
The header files needed for the socket programming are:

| Library | Description |
|---|-----|
| sys/socket.h | The header file socket.h includes a number of definitions of structures needed for sockets |
| netinet/in.h | The header file in.h contains constants and structures needed for internet domain addresses | 
| arpa/inet.h | The header file contains definitions for internet operations |
| netdb.h | This header file contains definitions for network database operations |

