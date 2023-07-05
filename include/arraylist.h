#ifndef ARRAYLIST_H
#define ARRAYLIST_H

#include <stdlib.h>

typedef struct arraylist {
    size_t size, capacity; 
    void **data; 
} ARRAYLIST; 

extern ARRAYLIST *arraylist_create(); 
extern void *arraylist_get(ARRAYLIST *list, int idx); 
extern void arraylist_set(ARRAYLIST *list, int idx, void *item); 
extern void arraylist_push(ARRAYLIST *list, void *item); 
extern void *arraylist_pop(ARRAYLIST *list); 
extern int arraylist_find(ARRAYLIST *list, void *item); 
extern void arraylist_free(ARRAYLIST *list); 

extern void **arraylist_to_array(ARRAYLIST *list); 

#endif