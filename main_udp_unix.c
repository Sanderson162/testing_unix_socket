#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>



void * thread_listen_and_ping(int parent_socket);

void read_packet_from_socket(int server_socket);

#define SOCK_PATH "tpf_unix_sock.server"


int main(void){

    int server_sock, len, rc;
    socklen_t server_socket_len;
    ssize_t bytes_rec = 0;
    struct sockaddr_un server_sockaddr;
    char buf[256];
    memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
    memset(buf, 0, 256);

    /****************************************/
    /* Create a UNIX domain datagram socket */
    /****************************************/
    server_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_sock == -1){
        //printf("SOCKET ERROR = %d", sock_errno());
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
    server_sockaddr.sun_family = AF_UNIX;
    strcpy(server_sockaddr.sun_path, SOCK_PATH);
    server_socket_len = sizeof(server_sockaddr);
    unlink(SOCK_PATH);
    rc = bind(server_sock, (struct sockaddr *) &server_sockaddr, server_socket_len);
    if (rc == -1){
        //printf("BIND ERROR = %d", sock_errno());
        close(server_sock);
        exit(1);
    }

    fd_set fdSet;

    const struct timeval tick_rate = {0, 10000};
    struct timeval timeout = tick_rate;

    int exit_flag = 0;

    while (!exit_flag) {

        if (timeout.tv_usec == 0) {
            timeout.tv_usec = tick_rate.tv_usec;
            //printf("putting out packet %lu\n", packet_no);
            //packet_no++;
            //send_game_state(serverInfo, udp_server_sd);
        }


        FD_ZERO(&fdSet);
        FD_SET(server_sock, &fdSet);
        FD_SET(STDIN_FILENO, &fdSet);

        int maxfd = server_sock;

        // the big select statement
        if(select(maxfd + 1, &fdSet, NULL, NULL, &timeout) > 0){

            if(FD_ISSET(STDIN_FILENO, &fdSet)) {
                exit_flag = 1;
            }

            // check for message on IPC
            if (FD_ISSET(server_sock, &fdSet))
            {
                read_packet_from_socket(server_sock);
            }

        } else {
            //printf("select timed out\n");
        }
    }



    /*****************************/
    /* Close the socket and exit */
    /*****************************/
    close(server_sock);

    return 0;
}

void read_packet_from_socket(int server_socket) {
    ssize_t bytes_rec;
    struct sockaddr_un client_socket;
    socklen_t client_socket_len;
    char buf[256];

    /****************************************/
    /* Read data on the server from clients */
    /* and print the data that was read.    */
    /****************************************/
    printf("waiting to recvfrom...\n");
    bytes_rec = recvfrom(server_socket, buf, 256, 0, (struct sockaddr *) &client_socket, &client_socket_len);
    if (bytes_rec == -1){
        //printf("RECVFROM ERROR = %d", sock_errno());
        close(server_socket);
        exit(1);
    }
    else {
        printf("DATA RECEIVED = %s\n", buf);
    }
}


void * thread_listen_and_ping(int parent_socket) {

}
