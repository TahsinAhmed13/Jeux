#include <string.h>
#include <pthread.h>

#include "player_registry.h"
#include "arraylist.h"
#include "debug.h"

typedef struct player_registry {
    pthread_mutex_t mutex; 
    ARRAYLIST *players; 
} PLAYER_REGISTRY; 

PLAYER_REGISTRY *preg_init() {
    debug("Initialize player registry");
    PLAYER_REGISTRY *preg = (PLAYER_REGISTRY *)calloc(sizeof(PLAYER_REGISTRY), 1); 
    pthread_mutex_init(&preg->mutex, NULL); 
    preg->players = arraylist_create(); 
    return preg; 
}

void preg_fini(PLAYER_REGISTRY *preg) {
    debug("Finalize player registry"); 
    for(int i = 0; i < preg->players->size; ++i) {
        PLAYER *player = (PLAYER *)arraylist_get(preg->players, i); 
        if(player)
            player_unref(player, "because player registry is being finalized"); 
    }
    arraylist_free(preg->players); 
    pthread_mutex_destroy(&preg->mutex); 
    free(preg); 
}

PLAYER *preg_register(PLAYER_REGISTRY *preg, char *name) {
    debug("Register player %s", name); 
    PLAYER *player = NULL; 
    pthread_mutex_lock(&preg->mutex); 
    for(int i = 0; i < preg->players->size; ++i) {
        PLAYER *candp = (PLAYER *)arraylist_get(preg->players, i); ;
        if(!strcmp(name, player_get_name(candp))) {
            player = candp; 
            break; 
        }
    }
    if(!player) {
        debug("Player with that name does not yet exist"); 
        player = player_create(name); 
        arraylist_set(preg->players, arraylist_find(preg->players, NULL),
            player_ref(player, "for reference being retained by player registry")); 
    }
    else {
        debug("Player exists with that name"); 
        player_ref(player, "for new reference to existing player"); 
    }
    pthread_mutex_unlock(&preg->mutex); 
    return player; 
}