#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>

#define HASH_SIZE 128

int listSize;
int initialized;
int page_size;


typedef struct thread_local_storage{
    pthread_t tid;
    unsigned int size; /* size in bytes */
    unsigned int page_num; /* number of pages */
    struct page **pages; /* array of pointers to pages */
} TLS;

struct page {
    uintptr_t address; /* start address of page */
    int ref_count; /* counter for shared pages */
};

struct hash_element{
    pthread_t tid;
    TLS *tls;
    struct hash_element *next;
};

struct hash_element* hash_table[HASH_SIZE];


void tls_handle_page_fault(int sig, siginfo_t *si, void *context){
    uintptr_t p_fault = ((uintptr_t) si->si_addr) & ~(page_size - 1); 

    int i, j;
    for (i = 0; i < listSize; i++){
        for (j = 0; j < hash_table[i]->tls->page_num; j++){
            if (hash_table[i]->tls->pages[j]->address == p_fault) {
                pthread_exit(NULL);
            }
        }
        
    }
    

    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);

}


void tls_init(){
    struct sigaction sigact;

    /* get the size of a page */
    page_size = getpagesize(); 
    
    /* install the signal handler for page faults (SIGSEGV, SIGBUS) */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO; /* use extended signal handling */
    sigact.sa_sigaction = tls_handle_page_fault;
    
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
    initialized = 1;
}


void tls_protect(struct page *p){
    if (mprotect((void *) p->address, page_size, 0)) {
        fprintf(stderr, "tls_protect: could not protect page\n");
        exit(1);
    }
}

void tls_unprotect(struct page *p){
    if (mprotect((void *) p->address, page_size, PROT_READ | PROT_WRITE)) {
        fprintf(stderr, "tls_unprotect: could not unprotect page\n");
        exit(1);
    }
}


int tls_create(unsigned int size){

    if (!initialized){
        tls_init();
    }

    int i;

    for(i = 0; i<listSize; i++){
        if(pthread_self() == hash_table[i]->tid){
           fprintf(stderr, "tls_create: LSA already exists for thread %d\n",(int)pthread_self()); 
           return -1;
        }
    }

    if(size <= 0){
        fprintf(stderr, "tls_create: size <= 0\n");
        return -1;
    }

    TLS* newEntry = (TLS*)calloc(size, sizeof(char));
    newEntry->tid = pthread_self();
    newEntry->size = size;
    newEntry->page_num = ((size - 1)/page_size) + 1;
    newEntry->pages = (struct page**)calloc(newEntry->page_num, sizeof(struct page*));

  
    for(i = 0; i < newEntry->page_num; i++){
        struct page *p = (struct page*)calloc(1,sizeof(struct page));
        p->address = (uintptr_t)mmap(0, page_size, 0, MAP_ANON | MAP_PRIVATE,0, 0);
        p->ref_count = 1;
        newEntry->pages[i] = p;
    }
        
    struct hash_element* newElement = (struct hash_element*)calloc(1, sizeof(struct hash_element));
    newElement->tls = newEntry;
    newElement->next = NULL;
    newElement->tid = pthread_self();

    hash_table[listSize++] = newElement;

    return 0;
}


int tls_write(unsigned int offset, unsigned int length, char *buffer){

    int i;
    int foundIndex = -1;

    for(i = 0; i<listSize; i++){
        if(pthread_self() == hash_table[i]->tid){
           foundIndex = i;
        }
    }
    if (foundIndex == -1){
        fprintf(stderr, "tls_write: no LSA present\n");
        return -1;
    }
    if((offset + length) > hash_table[foundIndex]->tls->size){
        fprintf(stderr, "tls_write: offset + length > size\n");
        return -1;
    }

    TLS* current = hash_table[foundIndex]->tls;

    for (i = 0; i < current->page_num; i++){
        tls_unprotect(current->pages[i]);
    }

    int cnt, idx;
    /* perform the write operation */
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
        struct page *p, *copy;
        unsigned int pn, poff;
        pn = idx / page_size;
        poff = idx % page_size;
        p = current->pages[pn];
        if (p->ref_count > 1) {
            /* this page is shared, create a private copy (COW) */
            copy = (struct page *) calloc(1, sizeof(struct page));
            copy->address = (uintptr_t) mmap(0, page_size, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
            memcpy((void*)copy->address,(void*)p->address,page_size);
            copy->ref_count = 1;
            current->pages[pn] = copy;
            
            /* update original page */
            p->ref_count--;
            tls_protect(p);
            p = copy;
        }
        *((char *)(p->address + poff)) = buffer[cnt];
    }


    for (i = 0; i < current->page_num; i++){
        tls_protect(current->pages[i]);
    }

    return 0;
}


int tls_read(unsigned int offset, unsigned int length, char *buffer){
    
    int i;
    int foundIndex = -1;

    for(i = 0; i<listSize; i++){
        if(pthread_self() == hash_table[i]->tid){
           foundIndex = i;
        }
    }
    if (foundIndex == -1){
        fprintf(stderr, "tls_read: no LSA present\n");
        return -1;
    }
    if((offset + length) > hash_table[foundIndex]->tls->size){
        fprintf(stderr, "tls_read: offset + length > size\n");
        return -1;
    }

    TLS* current = hash_table[foundIndex]->tls;

    for (i = 0; i < current->page_num; i++){
        tls_unprotect(current->pages[i]);
    }

    int cnt, idx;

    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
        struct page *p;
        unsigned int pn, poff;
        pn = idx / page_size;
        poff = idx % page_size;
        p = current->pages[pn];
        
        buffer[cnt] = *(((char *) p->address) + poff);

    }
    
    for (i = 0; i < current->page_num; i++){
        tls_protect(current->pages[i]);
    }

    return 0;
}


int tls_destroy(){

    int i;
    int foundIndex = -1;

    for(i = 0; i<listSize; i++){
        if(pthread_self() == hash_table[i]->tid){
           foundIndex = i;
        }
    }
    if (foundIndex == -1){
        fprintf(stderr, "tls_destroy: no LSA present\n");
        return -1;
    }

    TLS* current = hash_table[foundIndex]->tls;


    for (i = 0; i < current->page_num; i++){
        if(current->pages[i]->ref_count >1){
            current->pages[i]->ref_count--;
        }else{
            munmap((void *)current->pages[i]->address, page_size);
        }
    }

    free(current->pages);
    current->size = 0;
    current->tid = -1;
    
    hash_table[foundIndex]->tid = -1;

    return 0;
}


int tls_clone(pthread_t tid){

    int i;
    int foundIndex = -1;
    int targetIndex = -1;

    for(i = 0; i<listSize; i++){
        if(pthread_self() == hash_table[i]->tid){
            foundIndex = i;
        }
        if(tid == hash_table[i]->tid){
            targetIndex = i;
        }
    }
    if (targetIndex == -1){
        fprintf(stderr, "tls_clone: no LSA to copy\n");
        return -1;
    }
    if (foundIndex != -1){
        fprintf(stderr, "tls_clone: LSA already present\n");
        return -1;
    }

    TLS* target = hash_table[targetIndex]->tls;
    TLS* newTLS = (TLS*)calloc(target->size,sizeof(char));

    newTLS->tid = pthread_self();
    newTLS->size = target->size;
    newTLS->page_num = target->page_num;
    newTLS->pages = (struct page**)calloc(newTLS->page_num,sizeof(struct page*));

    for(i = 0; i< newTLS->page_num; i++){
        target->pages[i]->ref_count++;
        newTLS->pages[i] = target->pages[i];
    } 


    struct hash_element* newElement = (struct hash_element*)calloc(1, sizeof(struct hash_element));
    newElement->tls = newTLS;
    newElement->next = NULL;
    newElement->tid = pthread_self();

    hash_table[listSize++] = newElement;

    return 0;
}