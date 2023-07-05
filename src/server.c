#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "server.h"
#include "protocol.h"
#include "client_registry.h"
#include "player_registry.h"    
#include "jeux_globals.h"
#include "debug.h"

void *jeux_client_service(void *arg) {
    int connfd; 
    CLIENT *client; 
    PLAYER *player; 
    JEUX_PACKET_HEADER header; 
    void *data; 
    size_t datalen; 
    struct timespec time; 

    // Initialize
    pthread_detach(pthread_self()); 
    connfd = *((int *)arg); 
    free(arg); 
    debug("[%d] Starting client service", connfd); 
    client = creg_register(client_registry, connfd); 
    player = NULL; 
    if(!client) {
        debug("[%d] Failed to register client", connfd); 
        close(connfd); 
        pthread_exit(NULL);
    }

    // Main Loop
    while(proto_recv_packet(connfd, &header, &data) != -1) {
        switch(header.type) {
            case JEUX_LOGIN_PKT: 
                debug("[%d] LOGIN packet recieved", connfd); 
                if(!player && data) {
                    char *name = strndup(data, ntohs(header.size)); 
                    debug("[%d] Login '%s'", connfd, name); 
                    player = preg_register(player_registry, name); 
                    if(client_login(client, player) != -1) {
                        client_send_ack(client, NULL, 0); 
                    }
                    else {
                        debug("[%d] Already logged in (player %p [%s])", 
                            connfd, player, player_get_name(player));
                        player_unref(player, "after login attempt"); 
                        player = NULL; 
                        client_send_nack(client);        
                    }
                    free(name); 
                }
                else {
                    debug("[%d] Already logged in (player %p [%s])", 
                        connfd, player, player_get_name(player));
                    client_send_nack(client); 
                }
                break; 
            case JEUX_USERS_PKT: 
                debug("[%d] USERS packet recieved", connfd); 
                if(player && !data) {
                    debug("[%d] Users", connfd); 
                    FILE *stream = open_memstream((char **)&data, &datalen); 
                    PLAYER **plist = creg_all_players(client_registry); 
                    PLAYER **pp = plist;  
                    while(*pp) {
                        fprintf(stream, "%s\t%d\n", player_get_name(*pp), player_get_rating(*pp));  
                        player_unref(*pp, "for player removed from players list"); 
                        pp++; 
                    }  
                    free(plist); 
                    fclose(stream);  
                    client_send_ack(client, data, datalen);
                }
                else {
                    debug("[%d] Login required", connfd); 
                    client_send_nack(client); 
                }
                break;
            case JEUX_INVITE_PKT: 
                debug("[%d] INVITE packet recieved", connfd); 
                if(player && data) {
                    char *name = strndup(data, ntohs(header.size)); 
                    debug("[%d] Invite '%s'", connfd, name); 
                    CLIENT *dest = creg_lookup(client_registry, name);  
                    if(dest) {
                        int id = client_make_invitation(client, dest, header.role%2+1, header.role); 
                        if(id >= 0) {
                            memset(&header, 0, sizeof(JEUX_PACKET_HEADER)); 
                            header.type = JEUX_ACK_PKT; 
                            header.id = id;  
                            clock_gettime(CLOCK_MONOTONIC, &time); 
                            header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
                            header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
                            client_send_packet(client, &header, NULL);  
                        }  
                        else {
                            debug("[%d] Failed to create invitation", connfd); 
                            client_send_nack(client); 
                        }
                        client_unref(dest, "after invitation attempt"); 
                    }
                    else {
                        debug("[%d] No client logged in as '%s'", connfd, name); 
                        client_send_nack(client); 
                    }
                    free(name); 
                }
                else {
                    debug("[%d] Login required", connfd); 
                    client_send_nack(client); 
                }
                break;
            case JEUX_REVOKE_PKT: 
                debug("[%d] REVOKE packet recieved", connfd); 
                if(player && !data) {
                    debug("[%d] Revoke '%hhu'", connfd, header.id); 
                    if(client_revoke_invitation(client, header.id) != -1)
                        client_send_ack(client, NULL, 0); 
                    else
                        client_send_nack(client);  
                }
                else {
                    debug("[%d] Login required", connfd);     
                    client_send_nack(client); 
                }
                break;
            case JEUX_DECLINE_PKT:
                debug("[%d] DECLINE packet recieved", connfd); 
                if(player && !data) {
                    debug("[%d] Decline '%hhu'", connfd, header.id); 
                    if(client_decline_invitation(client, header.id) != -1)
                        client_send_ack(client, NULL, 0); 
                    else
                        client_send_nack(client); 
                }
                else {
                    debug("[%d] Login required", connfd); 
                    client_send_nack(client); 
                }
                break;
            case JEUX_ACCEPT_PKT: 
                debug("[%d] ACCEPT packet recieved", connfd); 
                if(player && !data) {
                    debug("[%d] Accept '%hhu'", connfd, header.id); 
                    if(client_accept_invitation(client, header.id, (char **)&data) != -1) 
                        client_send_ack(client, data, data ? strlen(data) : 0); 
                    else
                        client_send_nack(client); 
                }
                else {
                    debug("[%d] Login required", connfd); 
                    client_send_nack(client); 
                }
                break;
            case JEUX_MOVE_PKT: 
                debug("[%d] MOVE packet recieved", connfd); 
                if(player && data) {
                    char *move = strndup(data, ntohs(header.size)); 
                    debug("[%d] Move '%hhu' (%s)", connfd, header.id, move); 
                    if(client_make_move(client, header.id, move) != -1)
                        client_send_ack(client, NULL, 0); 
                    else
                        client_send_nack(client); 
                    free(move); 
                }
                else {
                    debug("[%d] Login required", connfd); 
                    client_send_nack(client); 
                }
                break;
            case JEUX_RESIGN_PKT:
                debug("[%d] RESIGN packet recieved", connfd); 
                if(player && !data) {
                    debug("[%d] Resign '%hhu'", connfd, header.id); 
                    if(client_resign_game(client, header.id) != -1)
                        client_send_ack(client, NULL, 0); 
                    else
                        client_send_nack(client); 
                }
                else {
                    debug("[%d] Login required", connfd); 
                    client_send_nack(client); 
                }
                break;
        }           
        if(data)
            free(data); 
    }    
    
    // Cleanup
    if(player) {
        player_unref(player, "becuase server thread is discarding reference to logged in player"); 
        debug("[%d] Logging out of client", connfd); 
        client_logout(client); 
    }
    debug("[%d] Ending client service", connfd); 
    creg_unregister(client_registry, client);
    close(connfd); 
    pthread_exit(NULL); 
}