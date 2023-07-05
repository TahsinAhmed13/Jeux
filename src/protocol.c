#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "jeux_globals_ext.h"
#include "debug.h"

const char *JEUX_PACKET_TYPE_NAME[] = {
    "",
    "LOGIN",
    "USERS",
    "INVITE",
    "REVOKE",
    "ACCEPT",
    "DECLINE",
    "MOVE",
    "RESIGN",
    "ACK",
    "NACK",
    "INVITED",
    "REVOKED",
    "ACCEPTED",
    "DECLINED",
    "MOVED",
    "RESIGNED",
    "ENDED",
};

int proto_send_packet(int fd, JEUX_PACKET_HEADER *hdr, void *data) {

#ifdef DEBUG
    fprintf(stderr, "=> %u.%u: type=%s, size=%hu, id=%hhu, role=%hhu",
        ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), 
        JEUX_PACKET_TYPE_NAME[hdr->type], ntohs(hdr->size), hdr->id, hdr->role); 
    if(data) {
        char *cpy = strndup(data, ntohs(hdr->size)); 
        fprintf(stderr, ", payload=[%s]\n", cpy); 
        free(cpy); 
    }
    else
        fprintf(stderr, " (no payload)\n"); 
    fflush(stderr); 
#endif

    // write header to wire  
    uint16_t hdr_size = sizeof(JEUX_PACKET_HEADER); 
    void *hdr_ptr = (void *)hdr;  
    while(hdr_size) {
        ssize_t wbytes = send(fd, hdr_ptr, hdr_size, 0); 
        if(wbytes <= 0)
            return -1;
        hdr_size -= wbytes; 
        hdr_ptr += wbytes; 
    }

    // write payload to wire
    uint16_t size = ntohs(hdr->size);  
    void *ptr = data; 
    while(size) {
        ssize_t wbytes = send(fd, ptr, size, 0);  
        if(wbytes <= 0)
            return -1; 
        size -= wbytes; 
        ptr += wbytes; 
    }
    return 0; 
}

int proto_recv_packet(int fd, JEUX_PACKET_HEADER *hdr, void **payloadp) {
    // initialize to default values
    memset(hdr, 0, sizeof(JEUX_PACKET_HEADER)); 
    *payloadp = NULL; 

    // read header from wire
    uint16_t hdr_size = sizeof(JEUX_PACKET_HEADER); 
    void *hdr_ptr = (void *)hdr; 
    while(hdr_size) {
        ssize_t rbytes = recv(fd, hdr_ptr, hdr_size, 0); 
        if(rbytes <= 0)
            return -1; 
        hdr_size -= rbytes; 
        hdr_ptr += rbytes; 
    }

    // read payload from wire
    uint16_t size = ntohs(hdr->size);   
    *payloadp = size ? malloc(size) : NULL; 
    void *ptr = *payloadp; 
    while(size) {
        ssize_t rbytes = recv(fd, ptr, size, 0); 
        if(rbytes <= 0)
            return -1; 
        size -= rbytes; 
        ptr += rbytes; 
    }

#ifdef DEBUG
    fprintf(stderr, "<= %u.%u: type=%s, size=%hu, id=%hhu, role=%hhu",
        ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), 
        JEUX_PACKET_TYPE_NAME[hdr->type], ntohs(hdr->size), hdr->id, hdr->role); 
    if(*payloadp) {
        char *cpy = strndup(*payloadp, ntohs(hdr->size)); 
        fprintf(stderr, ", payload=[%s]\n", cpy); 
        free(cpy); 
    }
    else
        fprintf(stderr, " (no payload)\n"); 
    fflush(stderr); 
#endif

    return 0; 
}