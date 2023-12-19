/*
Final Project
Elijah Delavar
CS 360
12/10/2023

Compiling:
    gcc -o myftp myftp.c myftp.h

Running:
    ./myftp [-d] <hostname | IP address>
*/

#include "myftp.h"

/****************************************************************************************
 * 
 *                                      CLIENT PROTOTYPES
 * 
 ****************************************************************************************/

// Useful

void extractFileName(char *dst, char *path);
void formatCMD(char *message, char *tok, char cmd, int len);
int checkArg(char *arg);
int checkFileType(char *path, int dir, int rw);

// Pipe / Execvp

void mypipe(char **left, char **right);
void pipeToMore(int *streamfd);

// Commands

void cmdEXIT(int sockfd);
void cmdLS();
void cmdRLS(int sockfd, const char *addr);
void cmdCD(char *path);
void cmdRCD(char *path, int sockfd);
void cmdGET(char *path, int sockfd, const char *addr);
void cmdSHOW(char *path, int sockfd, const char *addr);
void cmdPUT(char *path, int sockfd, const char *addr);

// User

void userParseInput(char *cmd, char *arg, int sockfd, const char *addr);
void userInput(int sockfd, const char *addr);

// Server 

int serverParseMSG(char *buf);
int serverReceiveMSG(char *dst, int sockfd);
int serverSendAndReceiveMSG(char *message, int sockfd, int size);
int serverDataConnection(int sockfd, const char *addr);
int serverConnectAndSend(int sockfd, int size, const char *addr, char *message);

// Client

int clientInit(char *port, const char *addr);

/****************************************************************************************
 * 
 *                                      USEFUL
 * 
 ****************************************************************************************/

/*
Extract filename from null-terminated path into dst.
Does not alter path.
*/
void extractFileName(char *dst, char *path) {
    char pathcpy[BUF_SIZE];
    char *prev;
    char *tok;

    strcpy(pathcpy, path);

    tok = strtok(pathcpy, "/");
    prev = tok;
    
    while (tok) {
        prev = tok;
        tok = strtok(NULL, "/");
    }
    
    strcpy(dst, prev);
}

/*
Parent process waits for children to exit before returning.
*/
void waitForChildren(int pid, int options) {
    int status;
    int err;
    while (err = waitpid(pid, &status, options)) {
        if (errno == ECHILD) break;
        if (err < 0) {
            fprintf(stderr, KRED "!!! Parent Error, waiting for child(ren)\n");
            exit(1);
        }
    }
    if (debug) printf(KGRN "?? Parent: Done waiting for child(ren)\n");
}

/*
Read from FD 1.  Write to FD 2.

@return 0: success 1: failure
*/
int transferContents(int fd1, int fd2) {
    char buf[BUF_SIZE];
    int actual;

    if (debug) printf(KGRN "?? Transferring contents from FD %d to FD %d...\n", fd1, fd2);

    errno = 0;
    while (actual = read(fd1, buf, BUF_SIZE)) {
        if (errno && actual < 0) {
            fprintf(stderr, KRED "!!! Error, reading from FD %d: %s\n", fd1, strerror(errno));
            exit(1);
        }
        if (writeToFD(buf, fd2, actual)) return 1;
        if (debug) printf(KGRN "?? Transferred %d bytes from FD %d to FD %d\n", actual, fd1, fd2);
    }

    if (debug) printf(KGRN "?? Finished transferring file contents\n");
    return 0;
}

/*
Taken from Assignment 3.

Checks the file at path for its accessibility specified in __type (OR'd together).
Checks that the file is a directory (if dir is 1) or a regular file (if dir is 0).

@return 0: success 1: failure
*/
int checkFileType(char *path, int dir, int __type) {
	struct stat finfo;

    // Check if path is __type
    if (access(path, __type)) {
        fprintf(stderr, KRED "!!! Error, accessing file type\n");
        return 1;
    }

	// Get file status
	if (lstat(path, &finfo) < 0) {
        fprintf(stderr, KRED "!!! Error, getting file status\n");
		return 1;
	}

	// Check file type
    if (dir) {
        if (S_ISDIR(finfo.st_mode)) return 0;
        fprintf(stderr, KRED "!!! Error: Pathname '%s' is not a directory\n", path);
    } else {
        if (S_ISREG(finfo.st_mode)) return 0;
        fprintf(stderr, KRED "!!! Error: Pathname '%s' is not a regular file\n", path);
    }
    
	// Failure
	return 1;
}

/*
Check if arg is not null.

@return 0: success 1: failure
*/
int checkArg(char *arg) {
    if (!arg) {
        fprintf(stderr, KRED "!!! Error: No parameter detected\n");
        return 1;
    }
    if (debug) printf(KGRN "?? Parameter detected: '%s'\n", arg);
    return 0;
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
            fprintf(stderr, KRED "!!! Error, writing to FD %d: %s\n", fd, strerror(errno));
            exit(1);
        }
        if (actual == size-head) return 0;
        head += actual;
    }
    return actual != size-head;
}

/****************************************************************************************
 * 
 *                                      PIPE / EXECVP
 * 
 ****************************************************************************************/

/*
Taken from Assignment 4.

Connect a process's stdout (if rw is 1) or stdin (if rw is 0)
to a stream's (fd) stdin or stdout.  Then execvp the program with args.

If fd is NULL, just execvp the program with args.
*/
void mypipeCONNECT_AND_EXECVP(int *fd, char **args, int rw) {
    if (debug) printf(KGRN "?? Child %d: Exec'ing command '%s'" KNRM "\n", getpid(), args[0]);
    if (fd) {
        close(rw);          // Close the fd to be replaced by pipe fd
                            // Fill that closed fd slot with pipe fd
        if (dup(fd[rw]) < 0) { 
            fprintf(stderr, KRED "!!! Child %d Error: %s\n", getpid(), strerror(errno));
            exit(1);
        }
        close(fd[rw]);      // Close original pipe fd
    }
    execvp(args[0], args);
    fprintf(stderr, KRED "!!! Child %d Error: %s\n", getpid(), strerror(errno));
    exit(1);
}

/*
Taken from Assignment 4.

Pipe stdout of left program to stdin of right program.
left and right MUST not be NULL.
*/
void mypipe(char **left, char **right) {
    if (left && right) {
        // Pipe stdin of left program to stdout of right program

        // Create pipe
        int fd[2];
        if (pipe(fd) < 0) {
            fprintf(stderr, KRED "!!! Child %d Error: %s\n", getpid(), strerror(errno));
            exit(1);
        }

        if (debug) printf(KGRN "?? Child %d: Forking...\n", getpid());

        if (fork()) {
            if (debug) printf(KGRN "?? Child %d: Started\n", getpid());
            close(fd[1]);   // Close unnecessary pipe fd (write)
            mypipeCONNECT_AND_EXECVP(fd, right, 0);    // Does not return
        }
        if (debug) printf(KGRN "?? Child %d: Started\n", getpid());
        close(fd[0]);   // Close unnecessary pipe fd (read)
        mypipeCONNECT_AND_EXECVP(fd, left, 1);     // Does not return
    } else {
        fprintf(stderr, KRED "!!! Child %d Error: Left or right program missing\n", getpid());
        exit(1);
    }
}

/*
Fork a new process to pipe data from streamfd to more.
*/
void pipeToMore(int *streamfd) {
    if (fork()) {
        // Parent
        close(streamfd[0]); // Close unused data socket fd
        waitForChildren(-1, 0);
        return;
    }

    // Child
    if (debug) printf(KGRN "?? Child %d: Started\n", getpid());
    char *args[] = {"more", "-n", "20", NULL};
    mypipeCONNECT_AND_EXECVP(streamfd, args, 0); // Does not return
}

/****************************************************************************************
 * 
 *                                      COMMANDS
 * 
 ****************************************************************************************/

/*
Send exit command to server, await response, then exit.
*/
void cmdEXIT(int sockfd) {
    char message[BUF_SIZE];
    if (debug) printf(KGRN "?? Exit command encountered\n");
    writeToFD("Q\n", sockfd, 2);
    serverReceiveMSG(message, sockfd);
    if (debug) printf(KGRN "?? Client exiting normally\n");
    exit(0);
}

/*
Fork off a new process to pipe the ls -l command into more -n 20.
Local operation.
*/
void cmdLS() {
    if (debug) printf(KGRN "?? Forking child to run ls...\n");
    if (fork()) {
        // Parent
        waitForChildren(-1, 0);
        return;
    }

    // Child
    if (debug) printf(KGRN "?? Child %d: Started\n", getpid());
    char *left[] = {"ls", "-l", NULL};
    char *right[] = {"more", "-n", "20", NULL};
    mypipe(left, right); // Does not return
}

/*
Establish data connection with server.
Fork off a new process to pipe the ls -l command into more -n 20.
Remote operation.
*/
void cmdRLS(int sockfd, const char *addr) {
    char message[BUF_SIZE];
    int datasockfd[2];

    strcpy(message, "L\n");

    // Establish data connection
    datasockfd[0] = serverConnectAndSend(sockfd, 2, addr, message);
    if (datasockfd[0] < 0) return;

    // Pipe to more
    if (debug) printf(KGRN "?? Forking child process to pipe ls ouptut into more\n");
    pipeToMore(datasockfd);
}

/*
CD into path stored in second token of buf.
Local Operation.
*/
void cmdCD(char *path) {
    if (checkArg(path)) return;
    if (checkFileType(path, 1, R_OK | X_OK)) return;
    
    if (debug) printf(KGRN "?? Entering directory '%s'\n", path);
	if (chdir(path) < 0) fprintf(stderr, KRED "!!! Error, changing directory\n");
    else if (debug) printf(KGRN "?? Successfully changed directory\n");
}

/*
CD into path stored in second token of buf.
Server Operation.
*/
void cmdRCD(char *path, int sockfd) {
    char message[BUF_SIZE+2];
    int datasockfd;
    int len;

    if (checkArg(path)) return;

    // Prepare server message
    snprintf(message, BUF_SIZE+2, "C%s\n", path);

    // Send control message to cd to path on the server
    if (!serverSendAndReceiveMSG(message, sockfd, strlen(message)))
        if (debug) printf(KGRN "?? Server successfully changed directory\n");
}

/*
Show specified file in server's cwd.
*/
void cmdSHOW(char *path, int sockfd, const char *addr) {
    char message[BUF_SIZE+2];
    int datasockfd[2];

    if (checkArg(path)) return;

    // Prepare server message
    snprintf(message, BUF_SIZE+2, "G%s\n", path);

    // Establish data connection
    datasockfd[0] = serverConnectAndSend(sockfd, strlen(message), addr, message);
    if (datasockfd[0] < 0) return;

    // Pipe to more
    if (debug) printf(KGRN "?? Forking child process to pipe the file '%s' into more\n", path);
    pipeToMore(datasockfd);
}

/*
Get a file from server's cwd.
*/
void cmdGET(char *path, int sockfd, const char *addr) {
    char message[BUF_SIZE+2];
    char fn[BUF_SIZE];
    int datasockfd;
    int fd;

    if (checkArg(path)) return;
    if (checkFileType(".", 1, W_OK)) return;

    // Create file
    extractFileName(fn, path);
    if ((fd = open(fn, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRGRP | S_IROTH)) < 0) {
        fprintf(stderr, KRED "!!! Error, creating file '%s': %s\n", fn, strerror(errno));
        return;
    }
    if (debug)  printf(KGRN "?? Created file '%s' in current working directory with FD %d\n", 
                        fn, fd);

    // Prepare server message
    snprintf(message, BUF_SIZE+2, "G%s\n", path);

    // Establish data connection
    if ((datasockfd = serverConnectAndSend(sockfd, strlen(message), addr, message)) < 0) {
        close(fd);
        close(datasockfd);
        if (remove(fn) < 0) {
            fprintf(stderr, KRED "!!! Error, removing file '%s': %s\n", fn, strerror(errno));
            exit(1);
        }
        if (debug) printf(KGRN "?? Removed file '%s'\n", fn);
        return;
    }
    
    transferContents(datasockfd, fd);
    close(fd);
    close(datasockfd);
}

/*
Put a file into server's cwd.
*/
void cmdPUT(char *path, int sockfd, const char *addr) {
    char message[BUF_SIZE+2];
    int datasockfd;
    int fd;

    if (checkArg(path)) return;
    if (checkFileType(path, 0, R_OK)) return;

    if ((fd = open(path, O_RDONLY)) < 0) {
        fprintf(stderr, KRED "!!! Error, opening file '%s': %s\n", path, strerror(errno));
        return;
    }
    if (debug)  printf(KGRN "?? Opened file '%s' in current working directory with FD %d\n", 
                        path, fd);

    // Prepare server message
    message[0] = 'P';
    extractFileName(message+1, path);
    strcat(message, "\n");

    // Establish data connection
    if ((datasockfd = serverConnectAndSend(sockfd, strlen(message), addr, message)) < 0) {
        close(fd);
        close(datasockfd);
        return;
    }

    // Open file and transfer contents
    transferContents(fd, datasockfd);
    close(fd);
    close(datasockfd);
}

/****************************************************************************************
 * 
 *                                      USER
 * 
 ****************************************************************************************/

/*
Interpret null-terminated user command (cmd) with arg.
*/
void userParseInput(char *cmd, char *arg, int sockfd, const char *addr) {
    if (debug) printf(KGRN "?? Received command: '%s'\n", cmd);

    if (!strcmp(cmd, "exit")) {
        cmdEXIT(sockfd);
    } else if (!strcmp(cmd, "ls")) {
        cmdLS();
    } else if (!strcmp(cmd, "rls")) {
        cmdRLS(sockfd, addr);
    } else if (!strcmp(cmd, "cd")) {
        cmdCD(arg);
    } else if (!strcmp(cmd, "rcd")) {
        cmdRCD(arg, sockfd);
    } else if (!strcmp(cmd, "show")) {
        cmdSHOW(arg, sockfd, addr);
    } else if (!strcmp(cmd, "get")) {
        cmdGET(arg, sockfd, addr);
    } else if (!strcmp(cmd, "put")) {
        cmdPUT(arg, sockfd, addr);
    } else {
        fprintf(stderr, KRED "!!! Error: Unknown command: '%s'\n", cmd);
        return;
    }
}

/*
Extract the command and argument tokens.
Pass them into userParseInput.
*/
void userCleanAndParseInput(char *buf, int sockfd, const char *addr) {
    char *cmd;
    char *arg;

    if (!(cmd = strtok(buf, " \n\t\v\f\r"))) return;

    arg = cmd;

    // Lowercase command
    while (*arg) {
        *arg = tolower(*arg);
        arg++;
    }

    arg = strtok(NULL, " \n\t\v\f\r");
    userParseInput(cmd, arg, sockfd, addr);
}

/*
Take in input from the user.
*/
void userInput(int sockfd, const char *addr) {
    char buf[BUF_SIZE];
    int actual;
    
    while (1) {
        printf(KNRM "MYFTP > ");
        fflush(stdout);

        errno = 0;
        while (actual = read(0, buf, BUF_SIZE-1)) {
            if (errno && actual < 0) {
                fprintf(stderr, KRED "!!! Error, reading user input: %s\n", strerror(errno));
                exit(1);
            }

            buf[actual] = 0;
            userCleanAndParseInput(buf, sockfd, addr);

            printf(KNRM "MYFTP > ");
            fflush(stdout);
        }

        fprintf(stderr, "\n! Error, reading user input: Unexpected EOF received\n");
    }
}

/****************************************************************************************
 * 
 *                                      SERVER
 * 
 ****************************************************************************************/

/*
Interpret server messsage.
If server sends any additional data, that data is stored in buf+1 with a null terminator.

@return 0: success 1: failure
*/
int serverParseMSG(char *buf) {
    if (buf[0] == 'A') {
        if (debug)      printf(KGRN "?? Received server response '%s'\n", buf);
        return 0;
    }
    if (buf[0] == 'E')  fprintf(stderr, KRED "!!! Error, server error message: '%s'\n", buf+1);
    else                fprintf(stderr, KRED "!!! Error, invalid server response: '%s'\n", buf);
    return 1;
}

/*
Receive message from server and store it in dst (null-terminated).
dst MUST be of size BUF_SIZE or more.

@return 0: success 1: failure
*/
int serverReceiveMSG(char *dst, int sockfd) {
    int actual;
    int head;
    
    if (debug) printf(KGRN "?? Awaiting server response...\n");

    head = 0;
    errno = 0;
    while (actual = read(sockfd, dst+head, BUF_SIZE-head-1)){
        if (errno && actual < 0) {
            fprintf(stderr, KRED "!!! Error, reading from FD %d: %s\n", sockfd, strerror(errno));
            exit(1);
        }
        dst[head+actual] = 0;
        if (actual == BUF_SIZE-head-1 || strchr(dst, '\n')) {
            dst[head+actual-1] = 0;
            return serverParseMSG(dst);
        }
        head += actual;
    }

    fprintf(stderr, KRED "!!! Error, reading server message: "
                    "Control socket closed unexpectedly\n");
    exit(1);
}


/*
Send size bytes of null-terminated message to the server.
Puts server response into message.

@return 0: success 1: failure
*/
int serverSendAndReceiveMSG(char *message, int sockfd, int size) {
    if (debug) printf(KGRN "?? Sending %c command to server\n", message[0]);
    if (writeToFD(message, sockfd, size)) {
        fprintf(stderr, KRED "!!! Error, writing to server: Unexpected EOF\n");
        exit(1);
    }
    return serverReceiveMSG(message, sockfd);
}

/*
Establish a data connection with the server.

@return Data socket file descriptor
*/
int serverDataConnection(int sockfd, const char *addr) {
    char port[BUF_SIZE];

    strcpy(port, "D\n");
    if (serverSendAndReceiveMSG(port, sockfd, 2)) {
        fprintf(stderr, KRED "!!! Error: Unable to establish data connection\n");
        exit(1);
    }

    if (debug) printf(KGRN "?? Connecting to server '%s' on port number '%s'\n", addr, port+1);
    return clientInit(port+1, addr);
}

/*
Establish a data connection with the server.
Then send size bytes of null-terminated message to server.
message MUST be initialized with BUF_SIZE or more.
Server response stored in message.

@return Data socket FD (-1 for errors)
*/
int serverConnectAndSend(int sockfd, int size, const char *addr, char *message) {
    int datasockfd;
    int len;
    datasockfd = serverDataConnection(sockfd, addr);
    if (debug) printf(KGRN "?? Data connection secured on server '%s'\n", addr);

    // Send control message to ls -l on the server
    if (serverSendAndReceiveMSG(message, sockfd, size)) {
        close(datasockfd);
        return -1;
    }
    return datasockfd;
}


/****************************************************************************************
 * 
 *                                      CLIENT
 * 
 ****************************************************************************************/

/*
Taken from Assignment 8.

Create a new client connecting to server (addr) on port (stored in buf).

@return client's FD
*/
int clientInit(char *port, const char *addr) {
    struct addrinfo hints, *actualdata;
    int sockfd;
    int err;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    if (err = getaddrinfo(addr, port, &hints, &actualdata)) {
        fprintf(stderr, KRED "!!! Error, translating host name '%s': %s\n", addr, gai_strerror(err));
        exit(1);
    }

    // Create socket
    if ((sockfd = socket(actualdata->ai_family, actualdata->ai_socktype, 0)) < 0) {
        fprintf(stderr, KRED "!!! Error, creating client socket\n");
        exit(1);
    }

    if (debug) printf(KGRN "?? Created socket with descriptor %d\n", sockfd);

    // Connect to server
    if (connect(sockfd, actualdata->ai_addr, actualdata->ai_addrlen) < 0) {
        fprintf(stderr, KRED "!!! Error, connecting to server\n");
        exit(1);
    }

    freeaddrinfo(actualdata);
    return sockfd;
}

/****************************************************************************************
 * 
 *                                      MAIN
 * 
 ****************************************************************************************/

/*
Checks for proper arguments.
Debug flag "-d" must be first argument if debugging is desired.
*/
void mainParseArgs(int argc, char const **argv) {
    // Check for correct number of args
    if (argc < 2 || argc > 3) {
        fprintf(stderr, KRED "!!! Usage: ./myftps [-d] <hostname | IP address>\n");
        exit(1);
    }

    // Init debug
    if (argc == 3) {
        if (strcmp(argv[1], "-d")) {
            fprintf(stderr, KRED "!!! Encountered unknown token '%s'\n", argv[1]);
            fprintf(stderr, KRED "!!! Usage: ./myftps [-d] <hostname | IP address>\n");
            exit(1);
        }
        printf(KGRN "?? Debug output enabled\n");
        debug = 1;
    }
}

int main(int argc, char const *argv[]){
    char port[BUF_SIZE];
    int sockfd;

    mainParseArgs(argc, argv);

    if (snprintf(port, BUF_SIZE, "%d", SERV_PORT) < 0) {
        fprintf(stderr, KRED "!!! Error, initializing port num\n");
        exit(1);
    }

    if (debug)  printf(KGRN "?? Attempting to connect to server '%s' on port %d\n", 
                        argv[argc-1], SERV_PORT);

    sockfd = clientInit(port, argv[argc-1]);
    printf(KNRM "* Connected to server '%s' on port %d\n", argv[argc-1], SERV_PORT);

    // Start communications
    userInput(sockfd, argv[argc-1]); // Does not return

    fprintf(stderr, KRED "!!! Error: Client exiting abnormally\n");
    exit(1);
}