#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "player.h"
#include "debug.h"

typedef struct player {
    pthread_mutex_t mutex; 
    size_t refs; 
    char *name; 
    int rating; 
} PLAYER; 

PLAYER *player_create(char *name) {
    PLAYER *player = (PLAYER *)calloc(sizeof(PLAYER), 1); 
    pthread_mutex_init(&player->mutex, NULL); 
    player->name = strdup(name); 
    player->rating = PLAYER_INITIAL_RATING;
    return player_ref(player, "for newly created player"); 
}

PLAYER *player_ref(PLAYER *player, char *why) {
    pthread_mutex_lock(&player->mutex); 
    debug("Increase reference count on player %p (%lu -> %lu) %s",
        player, player->refs, player->refs+1, why); 
    player->refs++; 
    pthread_mutex_unlock(&player->mutex); 
    return player; 
}

void player_unref(PLAYER *player, char *why) {
    size_t refs; 
    pthread_mutex_lock(&player->mutex); 
    debug("Decrease reference count on player %p (%lu -> %lu) %s",
        player, player->refs, player->refs-1, why); 
    refs = --player->refs; 
    pthread_mutex_unlock(&player->mutex); 
    if(!refs) {
        debug("Free player %p", player); 
        free(player->name); 
        pthread_mutex_destroy(&player->mutex); 
        free(player); 
    }
}

char *player_get_name(PLAYER *player) {
    char *name; 
    pthread_mutex_lock(&player->mutex); 
    name = player->name; 
    pthread_mutex_unlock(&player->mutex); 
    return name; 
}

int player_get_rating(PLAYER *player) {
    int rating; 
    pthread_mutex_lock(&player->mutex); 
    rating = player->rating; 
    pthread_mutex_unlock(&player->mutex); 
    return rating; 
}

void player_post_result(PLAYER *player1, PLAYER *player2, int result) {
    debug("Post result(%s, %s, %d)", player_get_name(player1), player_get_name(player2), result); 
    if(player1 > player2) {
        PLAYER *tmp = player1; 
        player1 = player2; 
        player2 = tmp;
        if(result)
            result = result%2+1;      
    }
    pthread_mutex_lock(&player1->mutex); 
    pthread_mutex_lock(&player2->mutex); 
    double s1, s2; 
    if(result == 0)
        s1 = s2 = 0.5; 
    else if(result == 1)
        s1 = 1.0, s2 = 0.0; 
    else
        s1 = 0.0, s2 = 1.0;  
    double e1 = 1/(1 + pow(10, (player2->rating-player1->rating)/400.0)); 
    double e2 = 1/(1 + pow(10, (player1->rating-player2->rating)/400.0)); 
    player1->rating += 32*(s1-e1); 
    player2->rating += 32*(s2-e2); 
    pthread_mutex_unlock(&player2->mutex); 
    pthread_mutex_unlock(&player1->mutex); 
}