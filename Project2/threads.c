#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include "ec440threads.h"


#define THREAD_RUNNING -1
#define THREAD_READY 0
#define THREAD_EXITED 1

#define STACK_SIZE 32767
#define MAX_THREADS 128

#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

#define ERROR_CREATE_THREAD -1
#define ERROR_NO_ACTIVE_THREAD -3


typedef struct Tcb{
    pthread_t id;
    void* rsp; //stack pointer, moves around
    jmp_buf state;
    int status;
} TCB;
//circular linked list
struct TCBNode{
    TCB* currThread;
    struct TCBNode* next;
};

struct TCBNode* current;
struct TCBNode* header;
static int listSize;
static int queueSize;

//return id of current thread
pthread_t pthread_self(void){

    return current->currThread->id;
}

//add tcb to LL after current
void tcbList_add(struct TCBNode* newNode){


    struct TCBNode* temp = current->next;
    current->next = newNode;
    newNode->next = temp;
    listSize++;

}

//add tcb to queue if we have to many
void tcbQueue_add(struct TCBNode* newNode){


    if(header == NULL){
        header = newNode;
        header->next = NULL;
    }else{
        struct TCBNode* temp = header;
        while(temp->next != NULL){
            temp = temp->next;
        }
        temp->next = newNode;
        newNode->next = NULL;
    }
    queueSize++;

}

//remove header of queue add it to LL
void tcbQueue_remove(){

    struct TCBNode* temp = header;
    if(queueSize > 1){
        header = header->next;
    }else{
        header = NULL;
    }
    
    tcbList_add(temp);
    queueSize--;
    

}


//sigaction struct and handler
void schedule(int sig){
    if(current->currThread->status != THREAD_EXITED){
        if(setjmp(current->currThread->state) == 0){
            if(listSize < MAX_THREADS && queueSize > 0){
                tcbQueue_remove();
            }
            current->currThread->status = THREAD_READY;
            current = current->next;
            current->currThread->status = THREAD_RUNNING;

            longjmp(current->currThread->state,1);
        
            
        }
    }else{
        longjmp(current->next->currThread->state,1);
    }

}

//returns whatever setjmp returned. we use this to determine whether we came into pthread_create from the main process or from long jumping back here
int init_subsystem(){


    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sa.sa_handler = schedule;
    sigaction(SIGALRM, &sa, NULL); 

    TCB *tcb = malloc(sizeof(TCB)); //Allocate new TCB
    //thread id is pid for main on Linux
    tcb->id = -1;


    //make current of the list
    struct TCBNode *tcbNode = (struct TCBNode*)malloc(sizeof(struct TCBNode));
    tcbNode->currThread = tcb;
    current = tcbNode;
    
    current->next = current;
    listSize = 1;
    queueSize = 0;
    header = NULL;

    //create the alarm
    ualarm(50000, 50000);

    int firstTime;
    if((firstTime = setjmp(tcb->state)) != 0){
        return firstTime;
    }else{

        tcb->rsp = (void *)tcb->state[0].__jmpbuf[JB_RSP];
        tcb->status = THREAD_RUNNING;
        return firstTime;
    }
    
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg){
    printf("launched %d\n",listSize);
    //flag to see if we're at capcity for concurrent threads
    int addToList = 1;

    if(current == NULL){
        //create head node from main process here
        //if set_jmp = 1, then we exit pthread create
        if(init_subsystem() == 1){
            return 0;
        }
    }else{
        //return error if too many threads already active
        if(listSize > MAX_THREADS){
            addToList = 0;
        }
    }

    
    //create new TCB
    TCB *tcb = malloc(sizeof(TCB)); //Allocate new TCB
    tcb->id = *thread;

    //create new stack
    void* stack = malloc(STACK_SIZE);
    stack += (STACK_SIZE - 1);
    stack -= 8;
    *(long unsigned int*)stack = (long unsigned int)pthread_exit;
    tcb->rsp = stack;

    if(setjmp(tcb->state) != 0){
        return ERROR_CREATE_THREAD;
    }

    tcb->state[0].__jmpbuf[JB_R12] = (long unsigned int)start_routine;
    tcb->state[0].__jmpbuf[JB_R13] = (long unsigned int)arg;
    tcb->state[0].__jmpbuf[JB_RSP] = ptr_mangle((long unsigned int)tcb->rsp);
    tcb->state[0].__jmpbuf[JB_PC] = ptr_mangle((long unsigned int)start_thunk);


    struct TCBNode *tcbNode = (struct TCBNode*)malloc(sizeof(struct TCBNode));
    tcbNode->currThread = tcb;

    tcb->status = THREAD_READY;
    
    if(addToList == 1){
        tcbList_add(tcbNode);
    }else if(addToList == 0){
        tcbQueue_add(tcbNode);
    }
    

    return 0;
}

//free tcb of current thread, then remove it from LL, then set status to exited
void pthread_exit(void *value_ptr){

    if(listSize == 1){
        exit(0);
    }else{

        //find previous entry from current, remove current, and make previous the new current
        struct TCBNode* prev = current;
        while(prev->next != current){
               
            prev = prev->next;
            
        }
        //remove from list
        prev->next = current->next;
        listSize--;

        //free rsp correctly
        current->currThread->rsp += 8;
        current->currThread->rsp -= (STACK_SIZE -1);
        free(current->currThread->rsp);
        current->currThread->status = THREAD_EXITED;


        current = prev;
        
        longjmp(current->currThread->state,1);
    }
    

}