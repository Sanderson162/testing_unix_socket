#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>

#define CLIENT_PATH "tpf_unix_sock.client"
#define SOCK_PATH "tpf_unix_sock.server"
#define DATA "Hello from server"
#define MAX_CLIENTS 100

typedef struct {
    int id;
    int client_socket;
    socklen_t client_socklen;
    struct sockaddr_un client_sockaddr;
} client_sock_un;

typedef struct {
    int server_socket;
    socklen_t server_socklen;
    struct sockaddr_un server_sockaddr;
    client_sock_un * client_list[MAX_CLIENTS];
} server_sock_un;

void * thread_listen_and_ping(const char * parent_socket_path);
void exit_clean_connections(server_sock_un * serverSockUn, int exit_code);
void read_packet_from_socket(server_sock_un * serverSockUn, int client_id);

server_sock_un *create_unix_stream_socket(const char *socket_path);

void accept_connection_and_get_name_unix_stream(server_sock_un *serverSockUn);
void remove_client(server_sock_un * serverSockUn, int client_id);
void remove_all_clients(server_sock_un * serverSockUn);
void send_data_to_client(server_sock_un *serverSockUn, int client_id, char *buffer, size_t buffer_len);

client_sock_un *connect_to_unix_socket(const char *parent_socket_path);

void send_data_to_server(client_sock_un *clientSockUn, char *buffer, size_t buffer_len);

void read_server_message(const client_sock_un *clientSockUn);

int main(void){

    //server socket
    server_sock_un * serverSockUn;
    serverSockUn = create_unix_stream_socket(SOCK_PATH);

    // start client sockets; thread_listen_and_ping
    pthread_t thread_id;
    printf("Starting child thread\n");
    pthread_create(&thread_id, NULL, (void *(*)(void *)) thread_listen_and_ping, SOCK_PATH);
    //pthread_join(thread_id, NULL);

    fd_set fdSet;

    //
    const struct timeval tick_rate = {0, 1000000};
    struct timeval timeout = tick_rate;

    int exit_flag = 0;

    while (!exit_flag) {

        if (timeout.tv_usec == 0) {
            timeout.tv_usec = tick_rate.tv_usec;
            const char * message = "##### server to client";
            char * buffer = strdup(message);
            size_t buffer_size = strlen(buffer);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (serverSockUn->client_list[i] != NULL) {
                    send_data_to_client(serverSockUn, 0, buffer, buffer_size);
                }
            }
            free(buffer);
        }


        FD_ZERO(&fdSet);
        FD_SET(serverSockUn->server_socket, &fdSet);
        FD_SET(STDIN_FILENO, &fdSet);

        int maxfd = serverSockUn->server_socket;

        // add clients to list of connections
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (serverSockUn->client_list[i]) {
                FD_SET(serverSockUn->client_list[i]->client_socket, &fdSet);
                if(serverSockUn->client_list[i]->client_socket > maxfd) {
                    maxfd = serverSockUn->client_list[i]->client_socket;
                }
            }
        }

        // the big select statement
        if(select(maxfd + 1, &fdSet, NULL, NULL, &timeout) > 0){

            if(FD_ISSET(STDIN_FILENO, &fdSet)) {
                exit_flag = 1;
            }

            // check for connection on IPC
            if (FD_ISSET(serverSockUn->server_socket, &fdSet))
            {
                accept_connection_and_get_name_unix_stream(serverSockUn);
            }

            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (serverSockUn->client_list[i] && FD_ISSET(serverSockUn->client_list[i]->client_socket, &fdSet)) {
                    read_packet_from_socket(serverSockUn, i);
                }
            }

        } else {
            //printf("select timed out\n");
        }
    }

    exit_clean_connections(serverSockUn, 0);
    return 0;
}

void send_data_to_client(server_sock_un *serverSockUn, int client_id, char *buffer, size_t buffer_len) {
    /******************************************/
    /* Send data back to the connected socket */
    /******************************************/
    size_t rc;
    //printf("Sending data to client...\n");
    rc = send(serverSockUn->client_list[client_id]->client_socket, buffer, buffer_len, 0);
    if (rc == -1) {
        exit_clean_connections(serverSockUn, -1);
    }
    else {
        //printf("Data sent to client!\n");
    }
}

void accept_connection_and_get_name_unix_stream(server_sock_un *serverSockUn) {
    /*********************************/
    /* Accept an incoming connection */
    /*********************************/
    client_sock_un  temp_client;

    temp_client.client_socket = accept(serverSockUn->server_socket, (struct sockaddr *) &temp_client.client_sockaddr, &temp_client.client_socklen);
    if (temp_client.client_socket == -1){
        exit_clean_connections(serverSockUn, -1);
    }

    /****************************************/
    /* Get the name of the connected socket */
    /****************************************/
    int rc;
    temp_client.client_socklen = sizeof(temp_client.client_sockaddr);
    rc = getpeername(temp_client.client_socket, (struct sockaddr *) &temp_client.client_sockaddr, &temp_client.client_socklen);
    if (rc == -1){
        exit_clean_connections(serverSockUn, -1);
    }
    else {
        printf("Client socket filepath: %s\n", temp_client.client_sockaddr.sun_path);
    }

    /****************************************/
    /* Add to the client list or reject conn */
    /****************************************/
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!serverSockUn->client_list[i]) {
            serverSockUn->client_list[i] = calloc(1, sizeof(server_sock_un));

            serverSockUn->client_list[i]->id = i;
            serverSockUn->client_list[i]->client_socket = temp_client.client_socket;
            memcpy(&serverSockUn->client_list[i]->client_sockaddr, &temp_client.client_sockaddr, temp_client.client_socklen);
            serverSockUn->client_list[i]->client_socklen = temp_client.client_socklen;

            return;
        }
    }



}

server_sock_un *create_unix_stream_socket(const char *socket_path) {
    server_sock_un * returnSocket;
    returnSocket = calloc(1, sizeof(server_sock_un));

    int backlog = 10;

    /**************************************/
    /* Create a UNIX domain stream socket */
    /**************************************/
    returnSocket->server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (returnSocket->server_socket == -1){
        //printf("SOCKET ERROR: %d\n", sock_errno());
        exit(1);
    }

    /***************************************/
    /* Set up the UNIX sockaddr structure  */
    /* by using AF_UNIX for the family and */
    /* giving it a filepath to bind to.    */
    /*                                     */
    /* Unlink the file so the bind will    */
    /* succeed, then bind to that file.    */
    /***************************************/
    returnSocket->server_sockaddr.sun_family = AF_UNIX;
    strcpy(returnSocket->server_sockaddr.sun_path, socket_path);
    returnSocket->server_socklen = sizeof(returnSocket->server_sockaddr);
    int rc;
    unlink(SOCK_PATH);
    rc = bind(returnSocket->server_socket, (struct sockaddr *) &returnSocket->server_sockaddr, returnSocket->server_socklen);
    if (rc == -1){
        //printf("BIND ERROR: %d\n", sock_errno());
        close(returnSocket->server_socket);
        exit(1);
    }

    /*********************************/
    /* Listen for any client sockets */
    /*********************************/
    rc = listen(returnSocket->server_socket, backlog);
    if (rc == -1){
        //printf("LISTEN ERROR: %d\n", sock_errno());
        close(returnSocket->server_socket);
        exit(1);
    }
    printf("socket listening...\n");

    return returnSocket;
}

void read_packet_from_socket(server_sock_un *serverSockUn, int client_id) {
    ssize_t bytes_rec;
    struct sockaddr_un client_socket;
    socklen_t client_socket_len;
    char buf[256];

/************************************/
    /* Read and print the data          */
    /* incoming on the connected socket */
    /************************************/
    //printf("waiting to read...\n");
    bytes_rec = recv(serverSockUn->client_list[client_id]->client_socket, buf, sizeof(buf), 0);
    if (bytes_rec == -1){
        //printf("RECV ERROR: %d\n", sock_errno());
        close(serverSockUn->server_socket);
        close(serverSockUn->client_list[client_id]->client_socket);
        exit(1);
    } else if (bytes_rec == 0) {
        printf("server: client disconnected\n");
        remove_client(serverSockUn, client_id);
    }
    else {
        printf("server: DATA RECEIVED = %s\n", buf);
    }
}

void remove_client(server_sock_un * serverSockUn, int client_id) {
    if(serverSockUn->client_list[client_id]) {
        close(serverSockUn->client_list[client_id]->client_socket);
        free(serverSockUn->client_list[client_id]);
        serverSockUn->client_list[client_id] = NULL;
    }
}

void remove_all_clients(server_sock_un * serverSockUn) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        remove_client(serverSockUn, i);
    }
}



void exit_clean_connections(server_sock_un * serverSockUn, int exit_code) {
    /******************************/
    /* Close the sockets and exit */
    /******************************/

    remove_all_clients(serverSockUn);
    close(serverSockUn->server_socket);
    free(serverSockUn);
    exit(exit_code);
}


void * thread_listen_and_ping(const char * parent_socket_path) {
    printf("client: starting child thread\n");



    char buf[256];



    client_sock_un * clientSockUn;

    clientSockUn = connect_to_unix_socket(parent_socket_path);


    fd_set fdSet;

    //
    const struct timeval tick_rate = {0, 500000};
    struct timeval timeout = tick_rate;

    int exit_flag = 0;

    while (!exit_flag) {

        if (timeout.tv_usec == 0) {
            timeout.tv_usec = tick_rate.tv_usec;
            const char * message = "##### client to server";
            char * buffer = strdup(message);
            size_t buffer_size = strlen(buffer);
            send_data_to_server(clientSockUn, buffer, buffer_size);
            free(buffer);
        }


        FD_ZERO(&fdSet);
        FD_SET(clientSockUn->client_socket, &fdSet);
        FD_SET(STDIN_FILENO, &fdSet);

        int maxfd = clientSockUn->client_socket;


        // the big select statement
        if(select(maxfd + 1, &fdSet, NULL, NULL, &timeout) > 0){

            if(FD_ISSET(STDIN_FILENO, &fdSet)) {
                exit_flag = 1;
            }

            // check for connection on IPC
            if (FD_ISSET(clientSockUn->client_socket, &fdSet))
            {
                read_server_message(clientSockUn);
            }


        } else {
            //printf("select timed out\n");
        }
    }

    /******************************/
    /* Close the socket and exit. */
    /******************************/
    close(clientSockUn->client_socket);
    return 0;
}

void read_server_message(const client_sock_un *clientSockUn) {
    /**************************************/
    /* Read the data sent from the server */
    /* and print it.                      */
    /**************************************/
    char buf[256];
    size_t bytes_rec;
    //printf("Waiting to recieve data...\n");
    memset(buf, 0, sizeof(buf));
    bytes_rec = read(clientSockUn->client_socket, buf, sizeof(buf));
    if (bytes_rec == -1) {
        printf("client: RECV ERROR = %d\n", errno);
        close(clientSockUn->client_socket);
        exit(1);
    } else if (bytes_rec == 0) {
        // server disconnected
        printf("client: server disconnected\n");
        close(clientSockUn->client_socket);
        exit(1);
    } else {
        printf("client: DATA RECEIVED = %s\n", buf);
    }
}

void send_data_to_server(client_sock_un *clientSockUn, char *buffer, size_t buffer_len) {
    /************************************/
    /* Copy the data to the buffer and  */
    /* send it to the server socket.    */
    /************************************/
    ssize_t bytes_sent;
    //printf("client: Sending data...\n");
    bytes_sent = write(clientSockUn->client_socket, buffer, buffer_len);
    if (bytes_sent == -1) {
        printf("client: SEND ERROR = %d\n", errno);
        close(clientSockUn->client_socket);
        exit(1);
    } else {
        //printf("client: Data sent!\n");
    }
}

client_sock_un *connect_to_unix_socket(const char *parent_socket_path) {
    client_sock_un * returnSocket;
    returnSocket = calloc(1, sizeof(client_sock_un));
    int rc;
    struct sockaddr_un server_sockaddr;
    memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
    /**************************************/
    /* Create a UNIX domain stream socket */
    /**************************************/
    returnSocket->client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (returnSocket->client_socket == -1) {
        printf("client: SOCKET ERROR = %d\n", errno);
        exit(1);
    }

    /***************************************/
    /* Set up the UNIX sockaddr structure  */
    /* by using AF_UNIX for the family and */
    /* giving it a filepath to bind to.    */
    /*                                     */
    /* Unlink the file so the bind will    */
    /* succeed, then bind to that file.    */
    /***************************************/
    returnSocket->client_sockaddr.sun_family = AF_UNIX;
    strcpy(returnSocket->client_sockaddr.sun_path, CLIENT_PATH);
    returnSocket->client_socklen = sizeof(returnSocket->client_sockaddr);

    unlink(CLIENT_PATH);
    rc = bind(returnSocket->client_socket, (struct sockaddr *) &returnSocket->client_sockaddr, returnSocket->client_socklen);
    if (rc == -1){
        printf("client: BIND ERROR: %d\n", errno);
        close(returnSocket->client_socket);
        exit(1);
    }

    /***************************************/
    /* Set up the UNIX sockaddr structure  */
    /* for the server socket and connect   */
    /* to it.                              */
    /***************************************/
    server_sockaddr.sun_family = AF_UNIX;
    strcpy(server_sockaddr.sun_path, parent_socket_path);
    socklen_t server_socklen;
    server_socklen = sizeof(server_sockaddr);
    rc = connect(returnSocket->client_socket, (struct sockaddr *) &server_sockaddr, server_socklen);
    if(rc == -1){
        printf("client: CONNECT ERROR = %d\n", errno);
        close(returnSocket->client_socket);
        exit(1);
    }
    return returnSocket;
}
