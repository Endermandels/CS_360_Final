/*
Final Project
Elijah Delavar
CS 360
12/10/2023
*/

#ifndef MYFTP
#define MYFTP

// Standard Libraries
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Debugging, Errors, String
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

// System
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Networking
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// Files
#include <dirent.h>
#include <fcntl.h>

#define BUF_SIZE    PATH_MAX+6
#define SERV_PORT   4987

// Colors

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"

short debug = 0;

void mypipeCONNECT_AND_EXECVP(int *fd, char **args, int child);
void waitForChildren(int pid, int options);
int transferContents(int fd1, int fd2);
int writeToFD(char *message, int sockfd, int size);

#endif
