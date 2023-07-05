#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "client_registry.h"
#include "arraylist.h"
#include "debug.h"

typedef struct client_registry {
    pthread_mutex_t mutex; 
    size_t size; 
    CLIENT *clients[MAX_CLIENTS]; 
    sem_t sem; 
    size_t waiting; 
} CLIENT_REGISTRY; 

CLIENT_REGISTRY *creg_init() {
    debug("Initialize client registry"); 
    CLIENT_REGISTRY *cr = (CLIENT_REGISTRY *)calloc(sizeof(CLIENT_REGISTRY), 1);
    pthread_mutex_init(&cr->mutex, NULL); 
    sem_init(&cr->sem, 0, 0); 
    return cr; 
}

void creg_fini(CLIENT_REGISTRY *cr) {
    debug("Finalize client registry"); 
    sem_destroy(&cr->sem); 
    pthread_mutex_destroy(&cr->mutex); 
    free(cr); 
}

CLIENT *creg_register(CLIENT_REGISTRY *cr, int fd) {
    CLIENT *res = NULL; 
    pthread_mutex_lock(&cr->mutex); 
    if(cr->size < MAX_CLIENTS) {
        res = client_create(cr, fd);  
        if(res) {
            for(int i = 0; i < MAX_CLIENTS; ++i) {
                if(cr->clients[i] == NULL) {
                    cr->clients[i] = res; 
                    cr->size++; 
                    debug("Register client fd %d (total connected: %lu)", fd, cr->size);
                    break; 
                }
            }
        }
    }
    pthread_mutex_unlock(&cr->mutex); 
    return res; 
}

int creg_unregister(CLIENT_REGISTRY *cr, CLIENT *client) {
    int res = -1; 
    pthread_mutex_lock(&cr->mutex); 
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(cr->clients[i] == client) {
            cr->clients[i] = NULL; 
            cr->size--; 
            res = 0; 
            debug("Unregister client fd %d (total connected: %lu)", client_get_fd(client), cr->size); 
            client_unref(client, "because client is being unregistered");
            break; 
        }
    }
    if(!cr->size) {
        while(cr->waiting) {
            sem_post(&cr->sem); 
            cr->waiting--; 
        }
    }
    pthread_mutex_unlock(&cr->mutex); 
    return res; 
}

CLIENT *creg_lookup(CLIENT_REGISTRY *cr, char *user) {
    CLIENT *res = NULL; 
    pthread_mutex_lock(&cr->mutex); 
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(cr->clients[i]) {
            PLAYER *player = client_get_player(cr->clients[i]); 
            if(player) {
                char *name = player_get_name(player); 
                if(!strcmp(user, name)) {
                    client_ref(cr->clients[i], "for reference being returned by creg_lookup()"); 
                    res = cr->clients[i]; 
                    break; 
                }
            }
        }
    }
    pthread_mutex_unlock(&cr->mutex);  
    return res; 
}

PLAYER **creg_all_players(CLIENT_REGISTRY *cr) {
    ARRAYLIST *plist = arraylist_create(); 
    pthread_mutex_lock(&cr->mutex);    
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(cr->clients[i]) {
            PLAYER *pp = client_get_player(cr->clients[i]);
            if(pp) {
                arraylist_push(plist, player_ref(pp, "for reference being added to players list"));
            }
        }
    }
    pthread_mutex_unlock(&cr->mutex); 
    PLAYER **players = (PLAYER **)arraylist_to_array(plist); 
    arraylist_free(plist); 
    return players; 
}

void creg_wait_for_empty(CLIENT_REGISTRY *cr) {
    int done = 1; 
    pthread_mutex_lock(&cr->mutex); 
    if(cr->size) {
        cr->waiting++; 
        done = 0; 
    }
    pthread_mutex_unlock(&cr->mutex); 
    if(!done)
        sem_wait(&cr->sem); 
}

void creg_shutdown_all(CLIENT_REGISTRY *cr) {
    pthread_mutex_lock(&cr->mutex); 
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(cr->clients[i]) {
            int fd = client_get_fd(cr->clients[i]); 
            debug("EOF on fd: %d", fd); 
            shutdown(fd, SHUT_RD);     
        }
    }
    pthread_mutex_unlock(&cr->mutex); 
}