#include <stdlib.h>
#include <pthread.h>

#include "client_registry.h"
#include "debug.h"

typedef struct invitation {
    pthread_mutex_t mutex; 
    size_t refs; 
    CLIENT *source, *target; 
    GAME_ROLE source_role, target_role; 
    GAME *game; 
    INVITATION_STATE state; 
} INVITATION; 

INVITATION *inv_create(CLIENT *source, CLIENT *target, 
                GAME_ROLE source_role, GAME_ROLE target_role) {
    if(source == target) {
        debug("Source and target cannot be same client"); 
        return NULL; 
    }
    INVITATION *inv = (INVITATION *)calloc(sizeof(INVITATION), 1); 
    pthread_mutex_init(&inv->mutex, NULL); 
    inv->source = client_ref(source, "as source of new invitation"); 
    inv->target = client_ref(target, "as target of new invitation"); 
    inv->source_role = source_role; 
    inv->target_role = target_role; 
    inv->state = INV_OPEN_STATE; 
    return inv_ref(inv, "for newly created invitation"); 
}

INVITATION *inv_ref(INVITATION *inv, char *why) {
    pthread_mutex_lock(&inv->mutex); 
    debug("Increase reference count on invitation %p (%lu -> %lu) %s",
        inv, inv->refs, inv->refs+1, why); 
    inv->refs++; 
    pthread_mutex_unlock(&inv->mutex); 
    return inv; 
}

void inv_unref(INVITATION *inv, char *why) {
    int refs; 
    pthread_mutex_lock(&inv->mutex); 
    debug("Decrease reference count on invitation %p (%lu -> %lu) %s",
        inv, inv->refs, inv->refs-1, why); 
    refs = --inv->refs; 
    pthread_mutex_unlock(&inv->mutex); 
    if(!refs) {
        debug("Free invitation %p", inv); 
        client_unref(inv->source, "because invitation is being freed"); 
        client_unref(inv->target, "becuase invitation is being freed"); 
        if(inv->game)
            game_unref(inv->game, "because invitation is being freed"); 
        pthread_mutex_destroy(&inv->mutex); 
        free(inv); 
    }
}

CLIENT *inv_get_source(INVITATION *inv) {
    CLIENT *source; 
    pthread_mutex_lock(&inv->mutex); 
    source = inv->source; 
    pthread_mutex_unlock(&inv->mutex); 
    return source; 
}

CLIENT *inv_get_target(INVITATION *inv) {
    CLIENT *target; 
    pthread_mutex_lock(&inv->mutex); 
    target = inv->target; 
    pthread_mutex_unlock(&inv->mutex); 
    return target; 
}

GAME_ROLE inv_get_source_role(INVITATION *inv) {
    GAME_ROLE source_role; 
    pthread_mutex_lock(&inv->mutex);  
    source_role = inv->source_role; 
    pthread_mutex_unlock(&inv->mutex); 
    return source_role; 
}

GAME_ROLE inv_get_target_role(INVITATION *inv) {
    GAME_ROLE target_role; 
    pthread_mutex_lock(&inv->mutex); 
    target_role = inv->target_role; 
    pthread_mutex_unlock(&inv->mutex);  
    return target_role; 
}

GAME *inv_get_game(INVITATION *inv) {
    GAME *game; 
    pthread_mutex_lock(&inv->mutex); 
    game = inv->game; 
    pthread_mutex_unlock(&inv->mutex); 
    return game; 
}

int inv_accept(INVITATION *inv) {
    int res = -1; 
    pthread_mutex_lock(&inv->mutex); 
    if(inv->state == INV_OPEN_STATE) {
        inv->state = INV_ACCEPTED_STATE; 
        inv->game = game_create();  
        res = 0; 
    }
    pthread_mutex_unlock(&inv->mutex); 
    return res; 
}

int inv_close(INVITATION *inv, GAME_ROLE role) {
    int res = -1; 
    pthread_mutex_lock(&inv->mutex); 
    if(inv->state == INV_OPEN_STATE) {
        res = 0; 
    }
    else if(inv->state == INV_ACCEPTED_STATE && role) {
        game_resign(inv->game, role); 
        res = 0; 
    }
    if(!res) {
        inv->state = INV_CLOSED_STATE; 
    }
    pthread_mutex_unlock(&inv->mutex); 
    return res; 
}