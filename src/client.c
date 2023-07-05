#include <string.h>
#include <pthread.h>
#include <time.h>

#include "client_registry.h"
#include "jeux_globals_ext.h"
#include "arraylist.h"
#include "debug.h"

typedef struct client {
    pthread_mutex_t mutex; 
    size_t refs; 
    CLIENT_REGISTRY *creg; 
    int fd; 
    PLAYER *player; 
    ARRAYLIST *invitations; 
} CLIENT; 

CLIENT *client_create(CLIENT_REGISTRY *creg, int fd) {
    CLIENT *client = (CLIENT *)calloc(sizeof(CLIENT), 1); 
    pthread_mutexattr_t attr; 
    pthread_mutexattr_init(&attr); 
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); 
    pthread_mutex_init(&client->mutex, &attr); 
    pthread_mutexattr_destroy(&attr); 
    client->creg = creg; 
    client->fd = fd; 
    client->invitations = arraylist_create();  
    return client_ref(client, "for newly created client"); 
}

CLIENT *client_ref(CLIENT *client, char *why) {
    pthread_mutex_lock(&client->mutex); 
    debug("Increase reference count on client %p (%lu -> %lu) %s",
        client, client->refs, client->refs+1, why); 
    client->refs++; 
    pthread_mutex_unlock(&client->mutex); 
    return client; 
}

void client_unref(CLIENT *client, char *why) {
    size_t refs; 
    pthread_mutex_lock(&client->mutex); 
    debug("Decrease reference count on client %p (%lu -> %lu) %s",
        client, client->refs, client->refs-1, why); 
    refs = --client->refs; 
    pthread_mutex_unlock(&client->mutex); 
    if(!refs) {
        debug("Free client %p", client); 
        client_logout(client);  
        arraylist_free(client->invitations); 
        pthread_mutex_destroy(&client->mutex); 
        free(client); 
    }
}

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; 

int client_login(CLIENT *client, PLAYER *player) {
    int res = -1; 
    pthread_mutex_lock(&log_mutex); 
    PLAYER **plist = creg_all_players(client->creg); 
    int logged = 0; 
    PLAYER **pp = plist; 
    while(*pp) {
        logged = logged || *pp == player; 
        player_unref(*pp, "for player removed from players list");      
        pp++; 
    }
    free(plist); 
    pthread_mutex_lock(&client->mutex); 
    if(!client->player && !logged) {
        client->player = player_ref(player, "for reference being retained by client"); 
        res = 0; 
    }
    pthread_mutex_unlock(&client->mutex); 
    pthread_mutex_unlock(&log_mutex); 
    return res; 
}

int client_logout(CLIENT *client) {
    int res = -1; 
    pthread_mutex_lock(&log_mutex); 
    pthread_mutex_lock(&client->mutex); 
    if(client->player) {
        debug("Log out client %p", client); 
        for(int i = 0; i < client->invitations->size; ++i) {
            INVITATION *inv = arraylist_get(client->invitations, i); 
            if(inv) {
                if(inv_get_game(inv))
                    client_resign_game(client, i); 
                else if(inv_get_source(inv) == client)
                    client_revoke_invitation(client, i); 
                else
                    client_decline_invitation(client, i); 
            } 
        }
        player_unref(client->player, "because client is being logged out"); 
        client->player = NULL; 
        res = 0; 
    }
    pthread_mutex_unlock(&client->mutex); 
    pthread_mutex_unlock(&log_mutex); 
    return res; 
}

PLAYER *client_get_player(CLIENT *client) {
    PLAYER *player; 
    pthread_mutex_lock(&client->mutex); 
    player = client->player; 
    pthread_mutex_unlock(&client->mutex); 
    return player; 
}

int client_get_fd(CLIENT *client) {
    int fd; 
    pthread_mutex_lock(&client->mutex);  
    fd = client->fd; 
    pthread_mutex_unlock(&client->mutex); 
    return fd; 
}

int client_send_packet(CLIENT *client, JEUX_PACKET_HEADER *pkt, void *data) {
    int res;  
    pthread_mutex_lock(&client->mutex); 
    debug("Send packet (clientfd=%d, type=%s) for client %p",
        client->fd, JEUX_PACKET_TYPE_NAME[pkt->type], client); 
    res = proto_send_packet(client->fd, pkt, data); 
    pthread_mutex_unlock(&client->mutex); 
    return res; 
}

int client_send_ack(CLIENT *client, void *data, size_t datalen) {
    JEUX_PACKET_HEADER header = {0}; 
    struct timespec time; 
    header.type = JEUX_ACK_PKT;
    header.size = htons((uint16_t)datalen); 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    return client_send_packet(client, &header, data); 
}

int client_send_nack(CLIENT *client) {
    JEUX_PACKET_HEADER header = {0}; 
    struct timespec time; 
    header.type = JEUX_NACK_PKT; 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    return client_send_packet(client, &header, NULL); 
}

int client_add_invitation(CLIENT *client, INVITATION *inv) {
    int id = -1; 
    int role = 0; 
    if(client == inv_get_source(inv))
        role = 1; 
    else if(client == inv_get_target(inv))
        role = 2; 
    pthread_mutex_lock(&client->mutex); 
    if(client->player && role) {
        debug("[%d] Add invitation as %s", client->fd, role == 1 ? "source" : "target"); 
        id = arraylist_find(client->invitations, NULL);
        arraylist_set(client->invitations, id, (void *)inv_ref(inv, 
            "for invitation being added to client's list")); 
    }    
    else {
        debug("[%d] Failed to add invitation", client->fd); 
    }
    pthread_mutex_unlock(&client->mutex); 
    return id; 
}

int client_remove_invitation(CLIENT *client, INVITATION *inv) {
    int id; 
    pthread_mutex_lock(&client->mutex); 
    id = arraylist_find(client->invitations, inv); 
    if(id < client->invitations->size) {
        debug("[%d] Remove invitation %p", client->fd, inv);
        inv_unref(arraylist_get(client->invitations, id),
            "for invitation being removed from client's list"); 
        arraylist_set(client->invitations, id, NULL);  
    }
    else {
        debug("[%d] Failed to remove invitation %p", client->fd, inv); 
    }
    pthread_mutex_unlock(&client->mutex); 
    return id; 
}

int client_make_invitation(CLIENT *source, CLIENT *target,
                GAME_ROLE source_role, GAME_ROLE target_role) {
    debug("[%d] Make an invitation", client_get_fd(source));         
    INVITATION *inv = inv_create(source, target, source_role, target_role); 
    if(!inv) {
        debug("[%d] Failed to create invitation", client_get_fd(source)); 
        return -1; 
    }
    int source_id = client_add_invitation(source, inv); 
    int target_id = client_add_invitation(target, inv); 
    inv_unref(inv, "becuase pointer to invitation is being discarded"); 
    if(source_id == -1 || target_id == -1)
        return -1; 

    JEUX_PACKET_HEADER header = {0}; 
    char *data; 
    struct timespec time; 
    header.type = JEUX_INVITED_PKT; 
    header.id = (uint8_t)target_id; 
    header.role = (uint8_t)target_role;
    data = player_get_name(client_get_player(source)); 
    header.size = htons((uint16_t)strlen(data)); 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    client_send_packet(target, &header, data); 
    return source_id; 
}

int client_revoke_invitation(CLIENT *client, int id) {
    debug("[%d] Revoke invitation %d", client_get_fd(client), id); 
    pthread_mutex_lock(&client->mutex); 
    INVITATION *inv = arraylist_get(client->invitations, id); 
    if(inv) {
        inv_ref(inv, "for pointer to invitation copied from source client's list"); 
    }
    pthread_mutex_unlock(&client->mutex); 
    if(!inv) {
        debug("[%d] Invalid invitation id (%d)", client_get_fd(client), id); 
        return -1; 
    }

    if(inv_get_source(inv) != client) {
        debug("[%d] Only the source (%p) can revoke invitation %d",
            client_get_fd(client), inv_get_source(inv), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    CLIENT *target = inv_get_target(inv); 
    int target_id; 
    if (inv_close(inv, NULL_ROLE) || 
        client_remove_invitation(client, inv) == -1 || 
        (target_id = client_remove_invitation(target, inv)) == -1) 
    {
        debug("[%d] Invitation %d can no longer be revoked", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    inv_unref(inv, "because pointer to invitation is now being discarded"); 

    JEUX_PACKET_HEADER header = {0}; 
    struct timespec time; 
    header.type = JEUX_REVOKED_PKT; 
    header.id = (uint8_t)target_id; 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    client_send_packet(target, &header, NULL); 
    return 0; 
}

int client_decline_invitation(CLIENT *client, int id) {
    debug("[%d] Decline invitation %d", client_get_fd(client), id); 
    pthread_mutex_lock(&client->mutex); 
    INVITATION *inv = arraylist_get(client->invitations, id); 
    if(inv) {
        inv_ref(inv, "for pointer to invitation copied from target client's list"); 
    }
    pthread_mutex_unlock(&client->mutex); 
    if(!inv) {
        debug("[%d] Invalid invitation id (%d)", client_get_fd(client), id); 
        return -1; 
    }

    if(inv_get_target(inv) != client) {
        debug("[%d] Only the target (%p) can decline invitation %d",
            client_get_fd(client), inv_get_target(inv), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    CLIENT *source = inv_get_source(inv); 
    int source_id; 
    if (inv_close(inv, NULL_ROLE) ||
        (source_id = client_remove_invitation(source, inv)) == -1 ||
        client_remove_invitation(client, inv) == -1) 
    {
        debug("[%d] Invitation %d can no longer be declined", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    inv_unref(inv, "because pointer to invitation is now being discarded"); 
 
    JEUX_PACKET_HEADER header = {0}; 
    struct timespec time; 
    header.type = JEUX_DECLINED_PKT; 
    header.id = (uint8_t)source_id; 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    client_send_packet(source, &header, NULL); 
    return 0; 
}

int client_accept_invitation(CLIENT *client, int id, char **strp) {
    debug("[%d] Accept invitation %d", client_get_fd(client), id); 
    pthread_mutex_lock(&client->mutex); 
    INVITATION *inv = arraylist_get(client->invitations, id);
    if(inv) {
        inv_ref(inv, "for pointer to invitation copied from target client's list"); 
    }
    pthread_mutex_unlock(&client->mutex); 
    if(!inv) {
        debug("[%d] Invalid invitation id (%d)", client_get_fd(client), id); 
        return -1; 
    }
    
    if(inv_get_target(inv) != client) {
        debug("[%d] Only the target (%p) can accept invitation %d",
            client_get_fd(client), inv_get_target(inv), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    if(inv_accept(inv)) {
        debug("[%d] Invitation %d can no longer be accepted", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1;
    }
    
    CLIENT *source = inv_get_source(inv); 
    JEUX_PACKET_HEADER header = {0}; 
    char *data = NULL; 
    struct timespec time; 
    header.type = JEUX_ACCEPTED_PKT; 
    pthread_mutex_lock(&source->mutex); 
    header.id = (uint8_t)arraylist_find(source->invitations, inv); 
    pthread_mutex_unlock(&source->mutex); 
    if(inv_get_target_role(inv) == FIRST_PLAYER_ROLE) {
        *strp = game_unparse_state(inv_get_game(inv));  
        header.size = 0; 
    }
    else {
        data = game_unparse_state(inv_get_game(inv)); 
        header.size = htons((uint16_t)strlen(data)); 
    }
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    client_send_packet(source, &header, data); 
    if(data)
        free(data);  
    inv_unref(inv, "because pointer to invitation is now being discarded"); 
    return 0; 
}

static int client_send_end(CLIENT *client, int id, GAME_ROLE winner) {
    JEUX_PACKET_HEADER header = {0}; 
    struct timespec time; 
    header.type = JEUX_ENDED_PKT; 
    header.id = (uint8_t)id; 
    header.role = winner; 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    return client_send_packet(client, &header, NULL); 
}

int client_resign_game(CLIENT *client, int id) {
    debug("[%d] Resign game %d", client_get_fd(client), id); 
    pthread_mutex_lock(&client->mutex); 
    INVITATION *inv = arraylist_get(client->invitations, id);
    if(inv) {
        inv_ref(inv, "for pointer to invitation copied from client's list"); 
    }
    pthread_mutex_unlock(&client->mutex); 
    if(!inv) {
        debug("[%d] Invalid invitation id (%d)", client_get_fd(client), id); 
        return -1; 
    }
    
    CLIENT *opp = NULL; 
    GAME_ROLE role = NULL_ROLE; 
    if(inv_get_source(inv) == client) {
        opp = inv_get_target(inv); 
        role = inv_get_source_role(inv);  
    }
    else if(inv_get_target(inv) == client) {
        opp = inv_get_source(inv); 
        role = inv_get_target_role(inv);
    }
    else {
        debug("[%d] Only a participant can resign game %d", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    } 
    int opp_id; 
    if (!inv_get_game(inv) || inv_close(inv, role) ||
        client_remove_invitation(client, inv) == -1 ||
        (opp_id = client_remove_invitation(opp, inv)) == -1) 
    { 
        debug("[%d] Invitation %d cannot be resigned", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }    

    JEUX_PACKET_HEADER header = {0}; 
    struct timespec time;     
    header.type = JEUX_RESIGNED_PKT;  
    header.id = (uint8_t)opp_id; 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    client_send_packet(opp, &header, NULL); 
    client_send_end(client, id, role%2+1); 
    client_send_end(opp, opp_id, role%2+1); 
    player_post_result(client_get_player(client), client_get_player(opp), 2); 
    inv_unref(inv, "because pointer to invitation is now being discarded"); 
    return 0; 
}

int client_make_move(CLIENT *client, int id, char *move) {
    debug("[%d] Make move '%s' in game %d", client_get_fd(client), move, id); 
    pthread_mutex_lock(&client->mutex); 
    INVITATION *inv = arraylist_get(client->invitations, id);
    if(inv) {
        inv_ref(inv, "for pointer to invitation copied from client's list"); 
    }
    pthread_mutex_unlock(&client->mutex); 
    if(!inv) {
        debug("[%d] Invalid invitation id (%d)", client_get_fd(client), id); 
        return -1; 
    }

    CLIENT *opp = NULL; 
    GAME_ROLE role = NULL_ROLE; 
    if(inv_get_source(inv) == client) {
        opp = inv_get_target(inv); 
        role = inv_get_source_role(inv);  
    }
    else if(inv_get_target(inv) == client) {
        opp = inv_get_source(inv); 
        role = inv_get_target_role(inv);
    }
    else {
        debug("[%d] Only a participant can make a move on game %d", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    GAME *game = inv_get_game(inv); 
    if(!game) {
        debug("[%d] No game in progress for invitation %d", client_get_fd(client), id); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    GAME_MOVE *gmove = game_parse_move(game, role, move); 
    if(!gmove) {
        debug("[%d] Cannot parse move", client_get_fd(client)); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    if(game_apply_move(game, gmove)) {
        debug("[%d] Illegal move", client_get_fd(client)); 
        inv_unref(inv, "because pointer to invitation is now being discarded"); 
        return -1; 
    }
    free(gmove); 

    JEUX_PACKET_HEADER header = {0}; 
    char *data;
    struct timespec time;  
    header.type = JEUX_MOVED_PKT; 
    pthread_mutex_lock(&opp->mutex); 
    header.id = (uint8_t)arraylist_find(opp->invitations, inv);  
    pthread_mutex_unlock(&opp->mutex); 
    data = game_unparse_state(game); 
    header.size = htons((uint16_t)strlen(data)); 
    clock_gettime(CLOCK_MONOTONIC, &time); 
    header.timestamp_sec = htonl((uint32_t)time.tv_sec); 
    header.timestamp_nsec = htonl((uint32_t)time.tv_nsec); 
    client_send_packet(opp, &header, data); 
    free(data); 

    if(game_is_over(game)) {
        GAME_ROLE winner = game_get_winner(game); 
        int opp_id; 
        if (inv_close(inv, winner%2+1) || 
            client_remove_invitation(client, inv) == -1 || 
            (opp_id = client_remove_invitation(opp, inv)) == -1) 
        {
            inv_unref(inv, "because pointer to invitation is now being discarded"); 
            return -1; 
        }
        int result = 0; 
        if(winner == role)
            result = 1; 
        else if(winner != NULL_ROLE)
            result = 2; 
        client_send_end(client, id, winner); 
        client_send_end(opp, opp_id, winner); 
        player_post_result(client_get_player(client), client_get_player(opp), result); 
    }
    inv_unref(inv, "because pointer to invitation is now being discarded"); 
    return 0; 
}