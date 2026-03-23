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

## How the Python grader runs
  1. Build: `make clean` then `make`
  2. Start server (in background): `cd server && ./udp_server <port>`
  3. Start one client via PTY: `cd client && ./udp_client 127.0.0.1 <port>`
  4. Issue commands to the client (see test list below)  

## Test Cases
| # | Test                        | Points | How it’s graded                                                                                                                                       |
| - | --------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 | `ls`                        | 15     | Client output must contain `foo1`, `foo2`, `foo3`.                                                                                                    |
| 2 | `get foo1`                  | 15     | After \~5s (configurable), SHA-256(**server/foo1**) == SHA-256(**client/foo1**).                                                                      |
| 3 | `delete foo1` → `ls`        | 15     | `foo1` must **not** appear in the listing.                                                                                                            |
| 4 | `put foo1`                  | 15     | After \~5s, SHA-256(**server/foo1**) == SHA-256(**client/foo1**).                                                                                     |
| 5 | `exit`                      | 15     | Server process must exit cleanly (exit code = `0`) within a short timeout.                                                                                              |
| 6 | **Reliability under netem** | 25     | Restart server/client, apply `tc netem delay X loss Y%`, create \~N MB `server/foo2`, `get foo2`, then hashes must match. qdisc is removed afterward. |

## Usage

Depending on your implementation (go-back-N or stop-n-wait) and file size, the wait time for reliable file transfer test would be different. 

You have to experiment with those arguments. 

Also, `netem` is a commandline tool on `Ubuntu`, thus you have to run the script on the course VM or your own Ubuntu machine.

Running the script on other OS won't be able to apply packet loss and latency.

Basic run (defaults shown):
```
python3 grade.py \
  --host 127.0.0.1 \
  --port 8000 \
  --timeout 3.0 \
  --wait 60.0 \
  --delete-wait 1.0 \
  --exit-wait 2.0 \
  --foo2-size-mb 2 \
  --loss 5.0 \
  --delay 1ms
```

Argument reference
  - `--host` / `--port` — server address/port passed to udp_client
  - `--timeout` — how long to capture output for ls tests (seconds)
  - `--wait` — wait time after get/put & reliability fetch before hashing (seconds)
  - `--delete-wait` — short settle time after delete (seconds)
  - `--exit-wait` — how long to wait for the server to exit (seconds)
  - `--foo2-size-mb` — size of server/foo2 for the reliability test (MB). Script builds it by concatenating server/foo1 until ~size.
  - `--loss` — packet loss percentage (e.g., 5%, 10%)
  - `--delay` — latency string for netem (e.g., 1ms, 5ms, 0ms)

## Safety notes for netem
The grader applies:
```
sudo tc qdisc add dev lo root netem delay <DELAY> loss <LOSS>%
```
and removes it with:
```
sudo tc qdisc del dev lo root
```
This affects only the loopback interface `lo`. Still, don’t interrupt the script to avoid leaving qdisc configured. If you do, you can manually clear it:
```
sudo tc qdisc del dev lo root
```