# Final Project - FTP System

### CS 360

### Elijah Delavar

### 12/10/2023

## Execution

To compile both client.c and server.c using a Makefile:

	$ make

To run client:

    $ ./myftp [-d] <hostname | IP address>

To run server:

    $ ./myftpserve [-d]

## Description

### myftp

Creates a client which connects to a server to perform FTP commands.  FTP commands are taken from standard input.

Client FTP Commands:

    exit                Client quits connection with server and exits
    cd <pathname>       Client changes directory to pathname
    rcd <pathname>      Server changes directory to pathname
    ls                  Client lists CWD
    rls                 Server lists CWD
    get <pathname>      Client stores file at pathname on server in client's CWD
    show <pathname>     Client redirects file at pathname on server to more
    put <pathname>      Client puts file at pathname in server's CWD

The client establishes a control connection with the server to send server FTP commands and receive responses.  The client establishes a data connection when transferring potentially large amounts of data between the client and the server.  The commands rls, get, show, and put must have a data connection established in order to execute properly.

### myftpserve

Creates a server which listens for clients to perform FTP commands.  FTP commands are received from the client control connection.  They are formatted as a single letter representing the command, followed by a pathname, if specified.

Server FTP Commands:

    D               Establish a data connection with the client
    C<pathname>     Change directory to pathname
    L               List CWD
    G<pathname>     Send file at pathname to client (works for both "get" and "show" commands)
    P<filename>     Put specified file in CWD
    Q               Quit server child for this client

The server forks off child processes for each client connection.  Every once in a while, the server will clean up any zombie processes.  For each command, the server either sends an acknowledgement, A, or an error message, E<_message>, to the client.
