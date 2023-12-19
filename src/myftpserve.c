/*
Final Project
Elijah Delavar
CS 360
12/10/2023

Compiling:
    gcc -o myftpserve myftpserve.c myftp.h

Running:
    ./myftpserve [-d]

Description:
    Creates a server which listens for clients to perform FTP commands.
*/

#include "myftp.h"


#define BACKLOG 5 // How many clients can queue up for a connection
#define CONNECTIONS_BEFORE_ZOMBIE_CLEANUP 5 // MUST be greater than 0

// Errors

#define E_NDIR "EFile is not a directory\n"
#define E_NREG "EFile is not regular\n"
#define E_DATA "EData connection missing\n"
#define E_BASE "EBase file name expected, pathname received\n"

/****************************************************************************************
 * 
 *                                      PROTOTYPES
 * 
 ****************************************************************************************/

// Useful

void chexit(int ischild);
void customERR(char *activity, int ischild);
void initSockAddr(struct sockaddr_in *addr, int port);
void closeDataConnections(int *datasockfd, int *dataservefd);
int checkFileType(char *path, int dir, int rw, int connectfd);

// Commands

void rcvEXIT(int connectfd);
void rcvD(int connectfd, int *datasockfd, int *dataservefd);
void rcvRLS(int connectfd, int *datasockfd, int *dataservefd);
void rcvRCD(int connectfd, char *path);
void rcvGET(int connectfd, int *datasockfd, int *dataservefd, char *path);
void rcvPUT(int connectfd, int *datasockfd, int *dataservefd, char *fn);

// Client

int clientDataConnection(int dataservefd, int port);
void clientAcceptMSG(int connectfd);
void clientSendMSG(char *message, int sockfd, int size);
void clientSendFormattedMSG(char cmd, char *message, int sockfd);
void clientParseMSG(char *buf, int connectfd, int *datasockfd, int *dataservefd);
void clientControlCommunication(int connectfd);
void clientConnection(struct sockaddr *clientAddr, int addrLen, int connectfd);

// Server

void serverAcceptConnections(int listenfd, int port);
int serverInit(int *port);

/****************************************************************************************
 * 
 *                                      USEFUL
 * 
 ****************************************************************************************/

/*
Exit with status 1.
*/
void chexit(int ischild) {
    if (ischild)    fprintf(stderr, KRED "!!! Child %d: Exiting with status 1\n", getpid());
    else            fprintf(stderr, KRED "!!! Parent: Exiting with status 1\n");
    exit(1);
}

/*
Error message during activity.
*/
void customERR(char *activity, int ischild) {
    if (ischild)    fprintf(stderr, KRED "!!! Child %d Error, %s: %s\n", 
                            getpid(), activity, strerror(errno));
    else            fprintf(stderr, KRED "!!! Parent Error, %s: %s\n", activity, strerror(errno));
}
/*
Initialize the clientAddr structure with port.
*/
void initSockAddr(struct sockaddr_in *addr, int port) {
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
}

/*
Parent process waits for child(ren) to exit before returning.
Parent uses pid = -1.
Child uses pid > 0.
*/
void waitForChildren(int pid, int options) {
    int status;
    int err;
    while (err = waitpid(pid, &status, options)) {
        if (errno == ECHILD) break;
        if (err < 0) {
            customERR("waiting for child(ren)", pid > 0);
            chexit(pid > 0);
        }
    }
    if (debug) {
        if (pid > 0)    printf(KGRN "?? Child %d: Done waiting for child(ren)\n", getpid());
        else            printf(KGRN "?? Parent: Done waiting for child(ren)\n");
    }
}

void closeDataConnections(int *datasockfd, int *dataservefd) {
    close(*datasockfd);
    close(*dataservefd);
    *datasockfd = -1;
}

/*
Taken from Assignment 4.

Connect a process's stdout (if rw is 1) or stdin (if rw is 0)
to a stream's (fd) stdin or stdout.  Then execvp the program with args.

If fd is NULL, just execvp the program with args.
*/
void mypipeCONNECT_AND_EXECVP(int *fd, char **args, int rw) {
    if (debug) printf(KGRN "?? Child %d: Exec'ing command '%s'\n", getpid(), args[0]);
    if (fd) {
        close(rw);          // Close the fd to be replaced by pipe fd
                            // Fill that closed fd slot with pipe fd
        if (dup(fd[rw]) < 0) { 
            customERR("duping FD", 1);
            chexit(1);
        }
        close(fd[rw]);      // Close original pipe fd
    }
    execvp(args[0], args);
    customERR("", 1);
    chexit(1);
}

/*
Send size bytes of null-terminated buf to fd.

@return 0: success 1: failure
*/
int writeToFD(char *buf, int fd, int size) {
    int actual;
    int head;

    head = 0;
    errno = 0;
    while (actual = write(fd, buf+head, size-head)) {
        if (errno && actual < 0) {
            fprintf(stderr, KRED "!!! Child %d Error, writing to FD %d: %s\n", 
                    getpid(), fd, strerror(errno));
            return 1;
        }
        if (actual == size-head) return 0;
        head += actual;
    }
    if (actual != size-head)    fprintf(stderr, KRED "!!! Child %d Error, writing to FD %d: "
                                        "Unexpected EOF\n", getpid(), fd);
    return actual!=size-head;
}

/*
Read from FD 1.  Write to FD 2.

@return 0: success 1: failure
*/
int transferContents(int fd1, int fd2) {
    char buf[BUF_SIZE];
    int actual;

    if (debug)  printf(KGRN "?? Child %d: Transferring contents from FD %d to FD %d...\n", 
                        getpid(), fd1, fd2);

    errno = 0;
    while (actual = read(fd1, buf, BUF_SIZE)) {
        if (errno && actual < 0) {
            fprintf(stderr, KRED "!!! Child %d Error, reading from FD %d: %s\n", 
                    getpid(), fd1, strerror(errno));
            return 1;
        }
        if (writeToFD(buf, fd2, actual)) return 1;
    }

    if (debug) printf(KGRN "?? Child %d: Finished transferring file contents\n", getpid());
    return 0;
}

/*
Taken from Assignment 3.

Checks the file at path for its accessibility specified in __type (OR'd together).
Checks that the file is a directory (if dir is 1) or a regular file (if dir is 0).

@return 0: success 1: failure
*/
int checkFileType(char *path, int dir, int __type, int connectfd) {
	struct stat finfo;
    char err[BUF_SIZE];
    int errsv;

    // Check if path is __type
    if (access(path, __type)) {
        errsv = errno;
        customERR("accessing file type", 1);
        clientSendFormattedMSG('E', strerror(errsv), connectfd);
        return 1;
    }

	// Get file status
	if (lstat(path, &finfo) < 0) {
        errsv = errno;
        customERR("checking file status", 1);
        clientSendFormattedMSG('E', strerror(errsv), connectfd);
		return 1;
	}

	// Check file type
    if (dir) {
        if (S_ISDIR(finfo.st_mode)) return 0;
        fprintf(stderr, KRED "!!! Child %d Error: File '%s' is not a directory\n", getpid(), path);
        clientSendMSG(E_NDIR, connectfd, strlen(E_NDIR));
    } else {
        if (S_ISREG(finfo.st_mode)) return 0;
        fprintf(stderr, KRED "!!! Child %d Error: File '%s' is not a regular file\n", getpid(), path);
        clientSendMSG(E_NREG, connectfd, strlen(E_NREG));
    }
    
	// Failure
	return 1;
}


/****************************************************************************************
 * 
 *                                      COMMANDS
 * 
 ****************************************************************************************/

/*
Exit command: Send acceptance, exit child process
*/
void rcvEXIT(int connectfd) {
    clientAcceptMSG(connectfd);
    printf(KNRM "* Child %d: Exiting normally\n", getpid());
    exit(0);
}

/*
D command: Establish a data connection with the client on a newly-initialized socketfd
*/
void rcvD(int connectfd, int *datasockfd, int *dataservefd) {
    char buf[BUF_SIZE];
    int port;

    port = 0;
    *dataservefd = serverInit(&port);
    snprintf(buf, BUF_SIZE, "A%d\n", port);
    clientSendMSG(buf, connectfd, strlen(buf));
    *datasockfd = clientDataConnection(*dataservefd, port);
}

/*
RLS command: Check for data connection, then pipe ls to datasockfd
*/
void rcvRLS(int connectfd, int *datasockfd, int *dataservefd) {
    int datafd[2];
    int pid;
    datafd[1] = *datasockfd;
    if (*datasockfd < 0) {
        fprintf(stderr, KRED "!!! Child %d Error: Data connection missing\n", getpid());
        clientSendMSG(E_DATA, connectfd, strlen(E_DATA));
        return;
    }
    clientAcceptMSG(connectfd);

    if (debug) printf(KGRN "?? Child %d: Forking child to run ls...\n", getpid());
    if (pid = fork()) {
        waitForChildren(pid, 0);
        closeDataConnections(datasockfd, dataservefd);
        printf(KNRM "* Child %d: Finished executing ls command\n", getpid());
        return;
    }

    if (debug) printf(KGRN "?? Child %d: Created for ls\n", getpid());
    char *args[] = {"ls", "-l", NULL};
    mypipeCONNECT_AND_EXECVP(datafd, args, 1);
}

/*
RCD command: Change directory to path
*/
void rcvRCD(int connectfd, char *path) {
    int errsv;
    if (checkFileType(path, 1, R_OK | X_OK, connectfd)) return;
    
    if (debug) printf(KGRN "?? Child %d: Entering directory '%s'\n", getpid(), path);
	if (chdir(path) < 0) {
        errsv = errno;
        customERR("changing directory", 1);
        clientSendFormattedMSG('E', strerror(errsv), connectfd);
        return;
    }
    
    printf(KNRM "* Child %d: Successfully changed directory to '%s'\n", getpid(), path);
    clientAcceptMSG(connectfd);
}

/*
GET command: Open specified file at path and send to datasockfd
*/
void rcvGET(int connectfd, int *datasockfd, int *dataservefd, char *path) {
    char err[BUF_SIZE];
    int fd;

    if (*datasockfd < 0) {
        fprintf(stderr, KRED "!!! Child %d Error: Data connection missing\n", getpid());
        clientSendMSG(E_DATA, connectfd, strlen(E_DATA));
        return;
    }

    // Check file at pathname is readable and regular
    if (checkFileType(path, 0, R_OK, connectfd)) {
        closeDataConnections(datasockfd, dataservefd);
        return;
    }

    if ((fd = open(path, O_RDONLY)) < 0) {
        int errsv = errno;
        fprintf(stderr, KRED "!!! Child %d Error, opening file '%s': %s\n", 
                getpid(), path, strerror(errsv));
        clientSendFormattedMSG('E', strerror(errsv), connectfd);
        closeDataConnections(datasockfd, dataservefd);
        return;
    }
    if (debug)  printf(KGRN "?? Child %d: Opened file '%s' in current working directory with FD %d\n", 
                        getpid(), path, fd);

    clientAcceptMSG(connectfd);

    transferContents(fd, *datasockfd);
    close(fd);
    closeDataConnections(datasockfd, dataservefd);
    printf(KNRM "* Child %d: Finished executing get command\n", getpid());
}

/*
PUT command: Create specified file (fn) and receive its contents from datasockfd
*/
void rcvPUT(int connectfd, int *datasockfd, int *dataservefd, char *fn) {
    char err[BUF_SIZE];
    int fd;

    if (*datasockfd < 0) {
        fprintf(stderr, KRED "!!! Child %d Error: Data connection missing\n", getpid());
        clientSendMSG(E_DATA, connectfd, strlen(E_DATA));
        return;
    }

    // Check CWD is writable
    if (checkFileType(".", 1, W_OK, connectfd)) {
        closeDataConnections(datasockfd, dataservefd);
        return;
    }
    
    // Make sure fn is a filename, not a path
    if (strchr(fn, '/')) {
        fprintf(stderr, KRED "!!! Child %d Error: Base file name expected; pathname '%s' received\n",
                getpid(), fn);
        clientSendMSG(E_BASE, connectfd, strlen(E_BASE));
        closeDataConnections(datasockfd, dataservefd);
        return;
    }
    
    if ((fd = open(fn, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRGRP | S_IROTH)) < 0) {
        int errsv = errno;
        fprintf(stderr, KRED "!!! Child %d Error, creating file '%s': %s\n", 
                getpid(), fn, strerror(errsv));
        clientSendFormattedMSG('E', strerror(errsv), connectfd);
        closeDataConnections(datasockfd, dataservefd);
        return;
    }
    if (debug)  printf(KGRN "?? Child %d: Created file '%s' in current working directory with FD %d\n", 
                        getpid(), fn, fd);

    clientAcceptMSG(connectfd);

    if (transferContents(*datasockfd, fd)) chexit(1);
    close(fd);
    closeDataConnections(datasockfd, dataservefd);
    printf(KNRM "* Child %d: Finished executing put command\n", getpid());
}

/****************************************************************************************
 * 
 *                                      CLIENT
 * 
 ****************************************************************************************/

/*
Establish a data connection on datasockfd.

@return client's fd
*/
int clientDataConnection(int dataservefd, int port) {
    struct sockaddr_in dataAddr;
    int len;
    int connectfd;

    initSockAddr(&dataAddr, port);
    len = sizeof(dataAddr);
    
    if (debug)  printf(KGRN "?? Child %d: Listening for data connection on FD %d...\n", 
                        getpid(), dataservefd);

    // Accept incoming client connections
    if ((connectfd = accept(dataservefd, (struct sockaddr*)&dataAddr, &len)) < 0) {
        customERR("accepting data connection", 1);
        chexit(1);
    }

    printf(KNRM "* Child %d: Data connection established\n", getpid());
    return connectfd;
}

/*
Send an A message to client.
*/
void clientAcceptMSG(int connectfd) {
    clientSendMSG("A\n", connectfd, 2);
}

/*
Send client size bytes of null-terminated message.
Make sure size cuts off the null terminator of message.
*/
void clientSendMSG(char *message, int sockfd, int size) {
    char buf[BUF_SIZE];
    if (writeToFD(message, sockfd, size)) chexit(1);
    strncpy(buf, message, size-1);
    buf[size-1] = 0;
    if (debug) printf(KGRN "?? Child %d: Successfully sent response '%s'\n", getpid(), buf);
}

/*
Send cmd response to client with message (null-terminated).
*/
void clientSendFormattedMSG(char cmd, char *message, int sockfd) {
    char tosend[BUF_SIZE+2];
    snprintf(tosend, BUF_SIZE+2, "%c%s\n", cmd, message);
    clientSendMSG(tosend, sockfd, strlen(tosend));
}

/*
Parse client's null-terminated message (in buf).
*/
void clientParseMSG(char *buf, int connectfd, int *datasockfd, int *dataservefd) {
    printf(KNRM "* Child %d: Received client command '%s'\n", getpid(), buf);

    if (buf[0] == 'Q') {
        rcvEXIT(connectfd);
    } else if (buf[0] == 'C') {
        rcvRCD(connectfd, buf+1);
    } else if (buf[0] == 'D') {
        rcvD(connectfd, datasockfd, dataservefd);
    } else if (buf[0] == 'L') {
        rcvRLS(connectfd, datasockfd, dataservefd);
    } else if (buf[0] == 'G') {
        rcvGET(connectfd, datasockfd, dataservefd, buf+1);
    } else if (buf[0] == 'P') {
        rcvPUT(connectfd, datasockfd, dataservefd, buf+1);
    } else {
        fprintf(stderr, KRED "!!! Child %d Error: invalid client command '%s'\n", 
                getpid(), buf);
        clientSendMSG("EInvalid command\n", connectfd, strlen("EInvalid command\n"));
    }
}

/*
Server listens for client control commands and then parses them.
*/
void clientControlCommunication(int connectfd) {
    char buf[BUF_SIZE];
    int datasockfd;
    int dataservefd;
    int actual;
    int head;
    
    if (debug)  printf(KGRN "?? Child %d: Listening for client commands on FD %d...\n", 
                        getpid(), connectfd);

    datasockfd = -1;
    dataservefd = -1;
    head = 0;
    errno = 0;
    while (actual = read(connectfd, buf+head, BUF_SIZE-head-1)){
        if (errno && actual < 0) {
            fprintf(stderr, KRED "!!! Child %d Error, reading from FD %d: %s\n", 
                    getpid(), connectfd, strerror(errno));
            chexit(1);
        }
        
        buf[head+actual] = 0;
        if (actual == BUF_SIZE-head-1 || strchr(buf, '\n')) {
            buf[head+actual-1] = 0;
            clientParseMSG(buf, connectfd, &datasockfd, &dataservefd);
            head = 0;
            continue;
        }
        head += actual;
    }

    fprintf(stderr, KRED "!!! Child %d Error, reading client message: "
                    "Control socket closed unexpectedly\n", getpid());
    chexit(1);
}

/*
Establish and identify a new client control connection.
*/
void clientConnection(struct sockaddr *clientAddr, int addrLen, int connectfd) {
    char hostName[NI_MAXHOST];
    int err;

    err = getnameinfo(clientAddr, 
                        addrLen,
                        hostName,
                        sizeof(hostName),
                        NULL,
                        0,
                        NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, KRED "!!! Child %d Error, getting client host name: %s\n", 
                getpid(), gai_strerror(err));
        chexit(1);
    }

    printf(KNRM "* Child %d: Connection accepted from host '%s'\n", getpid(), hostName);
    clientControlCommunication(connectfd);
}

/****************************************************************************************
 * 
 *                                      SERVER
 * 
 ****************************************************************************************/

/*
Taken from Assignment 8.

Accept incoming client connections.
Every CONNECTIONS_BEFORE_ZOMBIE_CLEANUP connections, clear zombies.
*/
void serverAcceptConnections(int listenfd, int port) {
    struct sockaddr_in clientAddr;
    int numConnections;
    int connectfd;
    int len;

    numConnections = 0;
    
    // Initialize client address
    initSockAddr(&clientAddr, port);
    len = sizeof(clientAddr);

    while (1) {
        if (debug) printf(KGRN "?? Parent: Listening for clients...\n");

        // Accept incoming client connections
        if ((connectfd = accept(listenfd, (struct sockaddr*)&clientAddr, &len)) < 0) {
            perror(KRED "!!! Parent Error, accepting connection");
            chexit(0);
        }

        numConnections++;

        if (fork()) {
            close(connectfd);

            // Clean up zombies
            if (numConnections % CONNECTIONS_BEFORE_ZOMBIE_CLEANUP == 0) {
                if (debug) printf(KGRN "?? Parent: Clearing zombies...\n");
                waitForChildren(-1, WNOHANG);
            }
            continue;
        }

        printf(KNRM "* Child %d: Started\n", getpid());
        clientConnection((struct sockaddr*)&clientAddr, len, connectfd);
        printf(KRED "!!! Child %d Error: Exiting abnormally\n", getpid());
        chexit(1);
    }
}

/*
Taken from Assignment 8.

Initialize a new server on given port.
Child will pass port 0; on return, port will contain the ephemeral port used.
Server will pass port SERV_PORT.

@return server's fd
*/
int serverInit(int *port) {
    struct sockaddr_in servAddr;
    int listenfd;
    int len;
    int ischild = !(*port);

    // Create socket
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        customERR("creating socket", ischild);
        chexit(ischild);
    }

    if (debug) {
        if (ischild)    printf(KGRN "?? Child %d: Socket created with descriptor %d\n", 
                                getpid(), listenfd);
        else            printf(KGRN "?? Parent: Socket created with descriptor %d\n", listenfd);
    }

    // Clear socket if server is killed
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        customERR("setting socket options", ischild);
        chexit(ischild);
    }

    // Initialize server address
    initSockAddr(&servAddr, *port);
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    len = sizeof(servAddr);

    // Bind socket to server address
    if (bind(listenfd, (struct sockaddr*)&servAddr, len) < 0) {
        customERR("binding", ischild);
        chexit(ischild);
    }

    // Get port number if port was 0 initially
    if (ischild) {
        memset(&servAddr, 0, len);

        if (getsockname(listenfd, (struct sockaddr*)&servAddr, &len) < 0) {
            customERR("getting socket name", ischild);
            chexit(ischild);
        }

        *port = ntohs(servAddr.sin_port);
    }

    if (debug) {
        if (ischild)    printf(KGRN "?? Child %d: Socket bound to port %d\n", getpid(), *port);
        else            printf(KGRN "?? Parent: Socket bound to port %d\n", *port);
    }

    // Listen with a max queue of BACKLOG connection requests
    if (listen(listenfd, BACKLOG) < 0) {
        customERR("creating socket", ischild);
        chexit(ischild);
    }
    
    if (debug) {
        if (ischild)    printf(KGRN "?? Child %d: Listening with connection queue of %d\n", 
                                getpid(), BACKLOG);
        else            printf(KGRN "?? Parent: Listening with connection queue of %d\n", BACKLOG);
    }

    return listenfd;
}

/****************************************************************************************
 * 
 *                                      MAIN
 * 
 ****************************************************************************************/

/*
Checks for proper arguments.
Debug flag "-d" is optional.
*/
void mainParseArgs(int argc, char const **argv) {
    // Check for correct number of args
    if (argc > 2) {
        fprintf(stderr, KRED "!!! Usage: ./myftpserve [-d]\n");
        exit(1);
    }

    // Init debug
    if (argc == 2) {
        if (strcmp(argv[1], "-d")) {
            fprintf(stderr, KRED "!!! Error: Encountered unknown token '%s'\n", argv[1]);
            fprintf(stderr, KRED "!!! Usage: ./myftpserve [-d]\n");
            exit(1);
        }
        printf(KGRN "?? Parent: Debug output enabled\n");
        debug = 1;
    }
}

int main(int argc, char const *argv[]) {
    int port;
    int listenfd;
    mainParseArgs(argc, argv);

    port = SERV_PORT;
    listenfd = serverInit(&port);
    serverAcceptConnections(listenfd, port);

    fprintf(stderr, KRED "!!! Parent Error: Exiting abnormally\n");
    return 1;
}
