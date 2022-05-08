#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "ec440threads.h"


#define THREAD_RUNNING -1
#define THREAD_READY 0
#define THREAD_EXITED 1
#define THREAD_BLOCKED 2

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
    int isMain;
    pthread_t blockee;//thread that is blocked by this thread
    int isBlocking;
    void* retValue;
} TCB;
//circular linked list
struct TCBNode{
    TCB* currThread;
    struct TCBNode* next;
    struct TCBNode* semNext;
};


typedef struct semaphore{
  unsigned value;
  struct TCBNode* queue;
  int queueSize;
  int isInit;
  int pshared;

} semaphore_info;


int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
void pthread_exit(void *value_ptr);
pthread_t pthread_self(void);
int pthread_join(pthread_t thread, void **value_ptr);
int init_subsystem();
void lock();
void unlock();
int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_destroy(sem_t *sem);

long int id;


struct TCBNode* current;
struct TCBNode* header;
static int listSize;
static int queueSize;

sigset_t new_set;
sigset_t old_set;
static int isLocked;

void lock(){
    sigemptyset(&new_set);
    sigaddset(&new_set, SIGALRM);
    if(sigprocmask(SIG_BLOCK, &new_set, &old_set) == 0){
        isLocked = 1;
    }
    
}

void unlock(){
    if(isLocked == 1){
        if(sigprocmask(SIG_SETMASK, &old_set, NULL) == 0)
            isLocked = 0;
    }
}

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

    if(setjmp(current->currThread->state) == 0){

        lock();
        //if there is room in the scheduler and there is something in the queue
        if(listSize < MAX_THREADS && queueSize > 0){
            tcbQueue_remove();
        }

        if(current->currThread->status == THREAD_RUNNING){
            current->currThread->status = THREAD_READY;
        }

        current = current->next;
        while(current->currThread->status != THREAD_READY){
            current = current->next;
        }
        
        current->currThread->status = THREAD_RUNNING;

        unlock();

        longjmp(current->currThread->state,1);
    
        
    }
    

}

//returns whatever setjmp returned. we use this to determine whether we came into pthread_create from the main process or from long jumping back here
int init_subsystem(){

    id = 0;

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sa.sa_handler = schedule;
    sigaction(SIGALRM, &sa, NULL); 

    //add these to the blockable set
    
    isLocked = 0;

    TCB *tcb = malloc(sizeof(TCB)); //Allocate new TCB
    //thread id is pid for main on Linux
    tcb->id = id++;
    tcb->isMain = 1;
    tcb->isBlocking = 0;
    tcb->blockee = 0;


    //make current of the list
    struct TCBNode *tcbNode = (struct TCBNode*)malloc(sizeof(struct TCBNode));
    tcbNode->currThread = tcb;
    current = tcbNode;
    
    current->next = current;
    current->semNext = NULL;
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


//free tcb of current thread, then remove it from LL, then set status to exited
void pthread_exit(void *value_ptr){

    if(listSize == 1){
        exit(0);
    }else{

        lock();
        
        
        current->currThread->status = THREAD_EXITED;
        current->currThread->retValue = value_ptr;

        if(current->currThread->isBlocking == 1){
            struct TCBNode* blockedThread = current;
            while(blockedThread->currThread->id != current->currThread->blockee){
                blockedThread = blockedThread->next;    
            }
            blockedThread->currThread->status = THREAD_READY;
            //printf("\n%d unblocked\n", (int)blockedThread->currThread->status);
        }
        
        
        listSize--;
        
        unlock();

        schedule(1);
        
        //longjmp(current->currThread->state,1);
    }
    __builtin_unreachable();
    
}

void pthread_exit_wrapper()
{
    unsigned long int res;
    asm("movq %%rax, %0\n":"=r"(res));
    pthread_exit((void *) res);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg){

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

    lock();
    //create new TCB
    TCB *tcb = malloc(sizeof(TCB)); //Allocate new TCB
    *thread = id++;
    tcb->id = *thread;
    tcb->isMain = 0;
    tcb->isBlocking = 0;
    tcb->blockee = 0;

    //create new stack
    void* stack = malloc(STACK_SIZE);
    stack += (STACK_SIZE - 1);
    stack -= 8;
    *(long unsigned int*)stack = (long unsigned int)pthread_exit_wrapper;
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
    tcbNode->semNext = NULL;

    tcb->status = THREAD_READY;
    
    
    if(addToList == 1){
        tcbList_add(tcbNode);
    }else if(addToList == 0){
        tcbQueue_add(tcbNode);
    }
    unlock();

    return 0;
}


int pthread_join(pthread_t thread, void **value_ptr){
    lock();

    struct TCBNode* waitThread = current;
    while(waitThread->currThread->id != thread){
        waitThread = waitThread->next;
    }

    if(waitThread->currThread->isBlocking == 1){
        return -1;
    }
    
    if(waitThread->currThread->status != THREAD_EXITED){
        waitThread->currThread->blockee = current->currThread->id;
        waitThread->currThread->isBlocking = 1;
        current->currThread->status = THREAD_BLOCKED;
    }
    

    //printf("join was ran\n");
    unlock();
    schedule(1);
    while(waitThread->currThread->status != THREAD_EXITED){
        //pause thread
    }

    waitThread->currThread->isBlocking = 0;
    if(value_ptr != NULL){
        *value_ptr = waitThread->currThread->retValue;    
    }
    
    //free up resources stuff
    //find previous entry from current, remove current, and make previous the new current
    struct TCBNode* prev = waitThread;
    while(prev->next != waitThread){
            
        prev = prev->next;
        
    }
    lock();
    //remove from list
    prev->next = waitThread->next;
    

    //free rsp correctly for non main threads
    if(waitThread->currThread->isMain == 0){
        waitThread->currThread->rsp += 8;
        waitThread->currThread->rsp -= (STACK_SIZE -1);
        free(waitThread->currThread->rsp);
    }
    unlock();

    return 0;
}

int sem_init(sem_t *sem, int pshared, unsigned value){
    semaphore_info *newSem = (semaphore_info*)malloc(sizeof(semaphore_info));
    newSem->value = value;
    newSem->queue = NULL;
    newSem->queueSize = 0;
    newSem->isInit = 1;
    newSem->pshared = pshared;

    sem->__align = (long int)newSem;
    //printf ("%ld pointer in init\n", sem->__align);

    return 0;
}

int sem_wait(sem_t *sem){
    lock();
    semaphore_info *currSem = (semaphore_info*)sem->__align;
    //printf("%ld pointer in wait\n", (long int)currSem);
    if(currSem->value == 0){
        
        current->currThread->status = THREAD_BLOCKED;
        if(currSem->queueSize == 0){
            currSem->queue = current;
            currSem->queueSize++;                                                                                                                                                                                                                                                               
        }else{
            struct TCBNode* queueEnd = currSem->queue;
            int i;
            for(i = 1; i<currSem->queueSize; i++){
                queueEnd = queueEnd->semNext;
            }
            queueEnd->semNext = current;
            currSem->queueSize++;
        }
        unlock();
        schedule(0);
        while(currSem->value == 0){
            //thread blocked
        }
        lock();
        currSem->value--;
        unlock();
    }else{
        
        currSem->value--;
        unlock();
    }
    
    return 0;

}

int sem_post(sem_t *sem){
    lock();
    semaphore_info *currSem = (semaphore_info*)sem->__align;
    currSem->value++;
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       

    if(currSem->value > 0){
        if(currSem->queueSize > 0){
            
            //struct TCBNode* queueThread = currSem->queue;
            currSem->queue->currThread->status = THREAD_READY;
            currSem->queue = currSem->queue->semNext;    
            currSem->queueSize--;
            
            /*if(setjmp(current->currThread->state) == 0){

                lock();
                current->currThread->status = THREAD_READY;
                queueThread->currThread->status = THREAD_RUNNING;
                unlock();

                longjmp(queueThread->currThread->state,1);
    
        
            }*/
        }
    }

    unlock();

    return 0;
}

int sem_destroy(sem_t *sem){
    semaphore_info *currSem = (semaphore_info*)sem->__align;

    currSem->queue = NULL;
    free((semaphore_info*)sem->__align);

    return 0;
}