#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <bits/types/sigevent_t.h>

#define GAME_PATH "tpf_unix_sock.game"
#define CHAT_PATH "tpf_unix_sock.chat"
#define SOCK_PATH "tpf_unix_sock.server"
#define DATA "Hello from server"
#define MAX_CLIENTS 100

typedef struct {
    int id;
    int client_socket;
} client_sock_un;

typedef struct {
    int server_socket;

    client_sock_un * client_list[MAX_CLIENTS];
} server_sock_un;


typedef struct {

} admin_server_info;

typedef struct {
    int server_socket;
    int game_socket;
    int chat_socket;
} ipc_socket_info;

typedef struct {
    const char * server_path;
    const char * client_path;
} ipc_path_pair;

int accept_ipc_connection(int server_socket);
void * thread_listen_and_ping(ipc_path_pair * ipcPathPair);
void exit_clean_connections(server_sock_un * serverSockUn, int exit_code);
void read_packet_from_socket(int client_socket);

int create_unix_stream_socket(const char *socket_path);

void accept_connection_and_get_name_unix_stream(server_sock_un *serverSockUn);
void remove_client(server_sock_un * serverSockUn, int client_id);
void remove_all_clients(server_sock_un * serverSockUn);
void send_data_to_client(int client_socket, char *buffer, size_t buffer_len);

int connect_to_unix_socket(ipc_path_pair * ipcPathPair);

void send_data_to_server(int client_socket, char *buffer, size_t buffer_len);

void read_server_message(int client_socket);

int main(void){

    ipc_socket_info ipcSocketInfo = {0};

    //server socket
    ipcSocketInfo.server_socket = create_unix_stream_socket(SOCK_PATH);

    // start client sockets; thread_listen_and_ping
    ipc_path_pair gameIpcPathPair = {SOCK_PATH, GAME_PATH};
    pthread_t game_thread_id;
    printf("Starting child thread\n");
    pthread_create(&game_thread_id, NULL, (void *(*)(void *)) thread_listen_and_ping, &gameIpcPathPair);
    ipcSocketInfo.game_socket = accept_ipc_connection(ipcSocketInfo.server_socket);

    ipc_path_pair chatIpcPathPair = {SOCK_PATH, CHAT_PATH};
    pthread_t chat_thread_id;
    printf("Starting child thread\n");
    pthread_create(&chat_thread_id, NULL, (void *(*)(void *)) thread_listen_and_ping, &chatIpcPathPair);
    ipcSocketInfo.chat_socket = accept_ipc_connection(ipcSocketInfo.server_socket);



    struct sigevent sigTermCatch;
    //pthread_join(thread_id, NULL);


    fd_set fdSet;

    const struct timeval tick_rate = {0, 10000};
    struct timeval timeout = tick_rate;

    int exit_flag = 0;
    struct timeval start_time;
    struct timeval end_time;
    gettimeofday(&start_time,NULL);

    printf("beginning main loop\n");
    while (!exit_flag) {

        gettimeofday(&end_time,NULL);

        long elapsed = ((end_time.tv_sec - start_time.tv_sec) * 1000000) + (end_time.tv_usec - start_time.tv_usec);
        printf("time elapsed after select returned %ld\n", elapsed);
        if (elapsed < timeout.tv_usec) {
            timeout.tv_usec -= elapsed;
        } else {
            timeout.tv_usec = 0;
        }

        if (timeout.tv_usec == 0) {
            timeout.tv_usec = tick_rate.tv_usec;
            const char * message = "##### server to client";
            char * buffer = strdup(message);
            size_t buffer_size = strlen(buffer);
            send_data_to_client(ipcSocketInfo.game_socket,  buffer, buffer_size);
            send_data_to_client(ipcSocketInfo.chat_socket,  buffer, buffer_size);
            free(buffer);
        }


        FD_ZERO(&fdSet);
        FD_SET(STDIN_FILENO, &fdSet);
        FD_SET(ipcSocketInfo.server_socket, &fdSet);
        FD_SET(ipcSocketInfo.game_socket, &fdSet);
        FD_SET(ipcSocketInfo.chat_socket, &fdSet);

        int maxfd = ipcSocketInfo.server_socket;

        if (ipcSocketInfo.game_socket > maxfd) {
            maxfd = ipcSocketInfo.game_socket;
        }
        if (ipcSocketInfo.chat_socket > maxfd) {
            maxfd = ipcSocketInfo.chat_socket;
        }


        // the big select statement
        if(select(maxfd + 1, &fdSet, NULL, NULL, &timeout) > 0){
//            gettimeofday(&start_time,NULL);
            printf("timeout %ld %ld\n", timeout.tv_sec, timeout.tv_usec);
            if(FD_ISSET(STDIN_FILENO, &fdSet)) {
                exit_flag = 1;
            }

            // check for connection on IPC
            if (FD_ISSET(ipcSocketInfo.server_socket, &fdSet))
            {
                // shouldn't be getting any new connections at this point.
                printf("server socket set\n");
            }
            if (FD_ISSET(ipcSocketInfo.game_socket, &fdSet))
            {
                read_packet_from_socket(ipcSocketInfo.game_socket);
            }
            if (FD_ISSET(ipcSocketInfo.chat_socket, &fdSet))
            {
                read_packet_from_socket(ipcSocketInfo.chat_socket);
            }

        } else {
//            gettimeofday(&start_time,NULL);
            //printf("select timed out\n");
        }
    }

    close(ipcSocketInfo.server_socket);
    close(ipcSocketInfo.game_socket);
    close(ipcSocketInfo.chat_socket);
    return 0;
}

void send_data_to_client(int client_socket, char *buffer, size_t buffer_len) {
    /******************************************/
    /* Send data back to the connected socket */
    /******************************************/
    size_t rc;
    //printf("Sending data to client...\n");
    rc = send(client_socket, buffer, buffer_len, 0);
    if (rc == -1) {
        exit(1);
    }
    else {
        printf("Data sent to client!\n");
    }
}

int accept_ipc_connection(int server_socket) {
    /*********************************/
    /* Accept an incoming connection */
    /*********************************/
    int client_socket;
    socklen_t client_socklen;
    struct sockaddr_un client_sockaddr;

    client_socket = accept(server_socket, (struct sockaddr *) &client_sockaddr, &client_socklen);
    if (client_socket == -1){
        close(server_socket);
        close(client_socket);
    }

    /****************************************/
    /* Get the name of the connected socket */
    /****************************************/
    int return_value;
    client_socklen = sizeof(client_sockaddr);
    return_value = getpeername(client_socket, (struct sockaddr *) &client_sockaddr, &client_socklen);
    if (return_value == -1){
        close(server_socket);
        close(client_socket);
    }
    else {
        printf("Client socket filepath: %s\n", client_sockaddr.sun_path);
    }
    return client_socket;
}

int create_unix_stream_socket(const char *socket_path) {
    socklen_t server_socklen;
    int server_socket;
    struct sockaddr_un server_sockaddr;
    int backlog = 10;

    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1){
        printf("SOCKET ERROR: %d\n", errno);
        exit(1);
    }

    server_sockaddr.sun_family = AF_UNIX;
    strcpy(server_sockaddr.sun_path, socket_path);
    server_socklen = sizeof(server_sockaddr);
    int return_value;
    unlink(SOCK_PATH);
    return_value = bind(server_socket, (struct sockaddr *) &server_sockaddr, server_socklen);
    if (return_value == -1){
        printf("BIND ERROR: %d\n", errno);
        close(server_socket);
        exit(1);
    }

    /*********************************/
    /* Listen for any client sockets */
    /*********************************/
    return_value = listen(server_socket, backlog);
    if (return_value == -1){
        printf("LISTEN ERROR: %d\n", errno);
        close(server_socket);
        exit(1);
    }
    printf("socket listening...\n");

    return server_socket;
}

void read_packet_from_socket(int client_socket) {
    ssize_t bytes_rec;
    char buf[256];

    /************************************/
    /* Read and print the data          */
    /* incoming on the connected socket */
    /************************************/
    //printf("waiting to read...\n");
    bytes_rec = recv(client_socket, buf, sizeof(buf), 0);
    if (bytes_rec == -1){
        //printf("RECV ERROR: %d\n", sock_errno());
        close(client_socket);
        exit(1);
    } else if (bytes_rec == 0) {
        printf("server: client disconnected\n");
        close(client_socket);
        exit(1);
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


void * thread_listen_and_ping(ipc_path_pair * ipcPathPair) {
    printf("client: starting child thread\n");



    char buf[256];



    client_sock_un * clientSockUn;
    int client_socket;
    client_socket = connect_to_unix_socket(ipcPathPair);


    fd_set fdSet;

//    //
//    const struct timeval tick_rate = {0, 500000};
//    struct timeval timeout = tick_rate;

    int exit_flag = 0;

    while (!exit_flag) {

//        if (timeout.tv_usec == 0) {
//            timeout.tv_usec = tick_rate.tv_usec;
//            const char * message = "##### client to server";
//            char * buffer = strdup(message);
//            size_t buffer_size = strlen(buffer);
//            send_data_to_server(client_socket, buffer, buffer_size);
//            free(buffer);
//        }


        FD_ZERO(&fdSet);
        FD_SET(client_socket, &fdSet);
        FD_SET(STDIN_FILENO, &fdSet);

        int maxfd = client_socket;


        // the big select statement
        if(select(maxfd + 1, &fdSet, NULL, NULL, NULL) > 0){

            if(FD_ISSET(STDIN_FILENO, &fdSet)) {
                exit_flag = 1;
            }

            // check for connection on IPC
            if (FD_ISSET(client_socket, &fdSet))
            {
                read_server_message(client_socket);
            }


        } else {
            //printf("select timed out\n");
        }
    }

    /******************************/
    /* Close the socket and exit. */
    /******************************/
    close(client_socket);
    return 0;
}

void read_server_message(int client_socket) {
    /**************************************/
    /* Read the data sent from the server */
    /* and print it.                      */
    /**************************************/
    char buf[256];
    size_t bytes_rec;
    //printf("Waiting to recieve data...\n");
    memset(buf, 0, sizeof(buf));
    bytes_rec = read(client_socket, buf, sizeof(buf));
    if (bytes_rec == -1) {
        printf("client: RECV ERROR = %d\n", errno);
        close(client_socket);
        exit(1);
    } else if (bytes_rec == 0) {
        // server disconnected
        printf("client: server disconnected\n");
        close(client_socket);
        exit(1);
    } else {
        printf("client: DATA RECEIVED = %s\n", buf);
    }
}

void send_data_to_server(int client_socket, char *buffer, size_t buffer_len) {
    /************************************/
    /* Copy the data to the buffer and  */
    /* send it to the server socket.    */
    /************************************/
    ssize_t bytes_sent;
    //printf("client: Sending data...\n");
    bytes_sent = write(client_socket, buffer, buffer_len);
    if (bytes_sent == -1) {
        printf("client: SEND ERROR = %d\n", errno);
        close(client_socket);
        exit(1);
    } else {
        //printf("client: Data sent!\n");
    }
}

int connect_to_unix_socket(ipc_path_pair * ipcPathPair) {
    client_sock_un * returnSocket;
    returnSocket = calloc(1, sizeof(client_sock_un));
    int rc;
    struct sockaddr_un server_sockaddr;
    struct sockaddr_un client_sockaddr;
    int client_socket;
    socklen_t client_socklen;
    memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));


    client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_socket == -1) {
        printf("client: SOCKET ERROR = %d\n", errno);
        exit(1);
    }

    client_sockaddr.sun_family = AF_UNIX;
    strcpy(client_sockaddr.sun_path, ipcPathPair->client_path);
    client_socklen = sizeof(client_sockaddr);

    unlink(ipcPathPair->client_path);
    rc = bind(client_socket, (struct sockaddr *) &client_sockaddr, client_socklen);
    if (rc == -1){
        printf("client: BIND ERROR: %d\n", errno);
        close(client_socket);
        exit(1);
    }

    /***************************************/
    /* Set up the UNIX sockaddr structure  */
    /* for the server socket and connect   */
    /* to it.                              */
    /***************************************/
    server_sockaddr.sun_family = AF_UNIX;
    strcpy(server_sockaddr.sun_path, ipcPathPair->server_path);
    socklen_t server_socklen;
    server_socklen = sizeof(server_sockaddr);
    rc = connect(client_socket, (struct sockaddr *) &server_sockaddr, server_socklen);
    if(rc == -1){
        printf("client: CONNECT ERROR = %d\n", errno);
        close(client_socket);
        exit(1);
    }
    return client_socket;
}
