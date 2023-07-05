#include "arraylist.h"

#define ARRAYLIST_INITIAL_CAPACITY 4

extern ARRAYLIST *arraylist_create() {
    ARRAYLIST *list = (ARRAYLIST *)malloc(sizeof(ARRAYLIST)); 
    list->size = 0; 
    list->capacity = ARRAYLIST_INITIAL_CAPACITY; 
    list->data = malloc(list->capacity * sizeof(void *)); 
    return list; 
}

extern void *arraylist_get(ARRAYLIST *list, int idx) {
    return idx < list->size ? list->data[idx] : NULL; 
}

extern void arraylist_set(ARRAYLIST *list, int idx, void *item) {
    idx < list->size ? (list->data[idx] = item) : arraylist_push(list, item);  
}

extern void arraylist_push(ARRAYLIST *list, void *item) {
    if(list->size >= list->capacity) {
        list->capacity <<= 1; 
        list->data = realloc(list->data, list->capacity * sizeof(void *)); 
    }
    list->data[list->size++] = item; 
}

extern void *arraylist_pop(ARRAYLIST *list) {
    if(list->size)
        return list->data[list->size--]; 
    return NULL; 
}

extern int arraylist_find(ARRAYLIST *list, void *item) {
    for(int i = 0; i < list->size; ++i) {
        if(arraylist_get(list, i) == item)
            return i; 
    }    
    return list->size; 
}

extern void arraylist_free(ARRAYLIST *list) {
    free(list->data); 
    free(list); 
}

extern void **arraylist_to_array(ARRAYLIST *list) {
    void **arr = (void **)malloc(sizeof(void *) * (list->size+1)); 
    for(int i = 0; i < list->size; ++i) {
        arr[i] = arraylist_get(list, i); 
    }
    arr[list->size] = NULL; 
    return arr; 
}