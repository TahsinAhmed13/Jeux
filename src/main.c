#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "csapp.h"
#include "debug.h"
#include "protocol.h"
#include "server.h"
#include "client_registry.h"
#include "player_registry.h"
#include "jeux_globals.h"

#ifdef DEBUG
int _debug_packets_ = 1;
#endif

static int listenfd = -1; 

static void terminate(int status);
static void sighup_handler(int signum); 

/*
 * "Jeux" game server.
 *
 * Usage: jeux <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    if(argc != 3 || strcmp(argv[1], "-p")) {
        fprintf(stderr, "Usage: %s -p <port>\n", argv[0]); 
        return EXIT_FAILURE; 
    }
    char *end; 
    if(strtol(argv[2], &end, 10) < 0 || *end) {
        fprintf(stderr, "Invalid port number %s\n", argv[2]);         
        return EXIT_FAILURE; 
    }
    
    // Perform required initializations of the client_registry and
    // player_registry.
    client_registry = creg_init();
    player_registry = preg_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function jeux_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server. 
    struct sigaction sighup_action = {0}; 
    sighup_action.sa_handler = sighup_handler; 
    sigfillset(&sighup_action.sa_mask); 
    if(sigaction(SIGHUP, &sighup_action, NULL) < 0) {
        debug("sigaction: %s", strerror(errno)); 
        terminate(EXIT_FAILURE); 
    }
    struct sigaction sigpipe_action = {0}; 
    sigpipe_action.sa_handler = SIG_IGN; 
    if(sigaction(SIGPIPE, &sigpipe_action, NULL) < 0) {
        debug("sigaction: %s", strerror(errno)); 
        terminate(EXIT_FAILURE); 
    }

    int *connfdp; 
    socklen_t clientlen; 
    struct sockaddr_storage clientaddr;
    pthread_t tid; 
    listenfd = open_listenfd(argv[2]); 
    if(listenfd < 0) {
        debug("open_listenfd: %s", strerror(errno)); 
        terminate(EXIT_FAILURE); 
    }
    debug("Jeux server listening on port %d", atoi(argv[2])); 

    while(1) {
        clientlen = sizeof(clientaddr); 
        connfdp = malloc(sizeof(int)); 
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen); 
        if(*connfdp >= 0) {
            int status = pthread_create(&tid, NULL, jeux_client_service, connfdp); 
            if(status != 0) {
                free(connfdp); 
                debug("pthread_create: %s\n", strerror(status)); 
            }
        }
        else {
            free(connfdp); 
            debug("accept: %s\n", strerror(errno)); 
        }
    }
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    // Close listening socket if open
    if(listenfd >= 0 && fcntl(listenfd, F_GETFD) != -1)
        close(listenfd); 

    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);
    
    debug("%ld: Waiting for service threads to terminate...", pthread_self());
    creg_wait_for_empty(client_registry);
    debug("%ld: All service threads terminated.", pthread_self());

    // Finalize modules.
    creg_fini(client_registry);
    preg_fini(player_registry);

    debug("%ld: Jeux server terminating", pthread_self());
    exit(status);
}

void sighup_handler(int status) {
    terminate(EXIT_SUCCESS); 
}
