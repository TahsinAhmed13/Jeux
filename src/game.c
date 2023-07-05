#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "game.h"
#include "debug.h"

typedef struct game {
    pthread_mutex_t mutex; 
    size_t refs; 
    GAME_ROLE board[9]; 
    GAME_ROLE turn, winner; 
} GAME; 

typedef struct game_move {
    GAME_ROLE role; 
    int pos; 
} GAME_MOVE; 

const int game_wins[][3] = {
    // horizontal
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8},
    // vertical
    {0, 3, 6},
    {1, 4, 7},
    {2, 5, 8},
    // diagonal
    {0, 4, 8},
    {2, 4, 6},
}; 

GAME *game_create() {
    GAME *game = (GAME *)calloc(sizeof(GAME), 1); 
    pthread_mutex_init(&game->mutex, NULL); 
    game->turn = FIRST_PLAYER_ROLE; 
    return game_ref(game, "for newly created game"); 
}

GAME *game_ref(GAME *game, char *why) {
    pthread_mutex_lock(&game->mutex); 
    debug("Increase reference count on game %p (%lu -> %lu) %s",
        game, game->refs, game->refs+1, why); 
    game->refs++; 
    pthread_mutex_unlock(&game->mutex); 
    return game; 
}

void game_unref(GAME *game, char *why) {
    size_t refs; 
    pthread_mutex_lock(&game->mutex); 
    debug("Decrease reference count on game %p (%lu -> %lu) %s",
        game, game->refs, game->refs-1, why); 
    refs = --game->refs; 
    pthread_mutex_unlock(&game->mutex); 
    if(!refs) {
        debug("Free game %p", game); 
        pthread_mutex_destroy(&game->mutex); 
        free(game); 
    }
}

int game_apply_move(GAME *game, GAME_MOVE *move) {
    pthread_mutex_lock(&game->mutex); 
    if(move->role != game->turn) {
        debug("Specified role (%d) does not match the role (%d) who is to move", move->role, game->turn); 
        pthread_mutex_unlock(&game->mutex); 
        return -1; 
    }
    if(game->board[move->pos-1]) {
        debug("Cannot apply move %s: position is already taken", game_unparse_move(move)); 
        pthread_mutex_unlock(&game->mutex); 
        return -1; 
    }
    debug("Apply move %s on game %p", game_unparse_move(move), game); 
    game->board[move->pos-1] = move->role; 
    game->turn = game->turn%2+1; 
    for(int i = 0; i < sizeof(game_wins)/sizeof(game_wins[0]); ++i) {
        if (game->board[game_wins[i][0]] == move->role && 
            game->board[game_wins[i][0]] == game->board[game_wins[i][1]] &&
            game->board[game_wins[i][1]] == game->board[game_wins[i][2]])
        {
            game->winner = move->role; 
            game->turn = NULL_ROLE;         
            break; 
        }
    }      
    int tie = game->turn; 
    for(int i = 0; i < sizeof(game->board)/sizeof(game->board[0]) && tie; ++i) {
        tie = tie && game->board[i]; 
    }
    if(tie) {
        game->winner = NULL_ROLE; 
        game->turn = NULL_ROLE; 
    }
    pthread_mutex_unlock(&game->mutex); 
    return 0; 
}

int game_resign(GAME *game, GAME_ROLE role) {
    int res = -1; 
    pthread_mutex_lock(&game->mutex); 
    if(game->turn && role) {
        game->winner = role%2+1; 
        game->turn = NULL_ROLE; 
        res = 0;         
    }
    pthread_mutex_unlock(&game->mutex); 
    return res; 
}

char *game_unparse_state(GAME *game) {
    char *state; 
    size_t len; 
    FILE *stream = open_memstream(&state, &len); 
    pthread_mutex_lock(&game->mutex);  
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            int pos = 3*i+j; 
            char ch = ' '; 
            if(game->board[pos])
                ch = game->board[pos] == FIRST_PLAYER_ROLE ? 'X' : 'O'; 
            fprintf(stream, "%c%c", ch, j < 2 ? '|' : '\n'); 
        }
        if(i < 2)
            fprintf(stream, "-----\n"); 
    }
    fprintf(stream, "%c to move", game->turn == FIRST_PLAYER_ROLE ? 'X' : 'O'); 
    pthread_mutex_unlock(&game->mutex); 
    fclose(stream); 
    return state; 
}

int game_is_over(GAME *game) {
    int is_over; 
    pthread_mutex_lock(&game->mutex); 
    is_over = !game->turn; 
    pthread_mutex_unlock(&game->mutex);  
    return is_over; 
}

GAME_ROLE game_get_winner(GAME *game) {
    GAME_ROLE winner; 
    pthread_mutex_lock(&game->mutex);  
    winner = game->winner; 
    pthread_mutex_unlock(&game->mutex);
    return winner; 
}

GAME_MOVE *game_parse_move(GAME *game, GAME_ROLE role, char *str) {
    char *end; 
    int pos = strtol(str, &end, 10); 
    if(!role || *end || !(1 <= pos && pos <= 9))
        return NULL; 
    GAME_MOVE *move = malloc(sizeof(GAME_MOVE)); 
    move->role = role; 
    move->pos = pos;
    return move; 
}

char *game_unparse_move(GAME_MOVE *move) {
    char *str; 
    size_t len; 
    FILE *stream = open_memstream(&str, &len); 
    fprintf(stream, "%d<-%c", move->pos, move->role == FIRST_PLAYER_ROLE ? 'X' : 'O'); 
    fclose(stream); 
    return str; 
}