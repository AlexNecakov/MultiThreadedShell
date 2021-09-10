
#include<stdio.h>
#include<pthread.h>
#include<semaphore.h>
#include<stdlib.h>

#define THREAD_CNT 4

sem_t mutex;
sem_t full;
sem_t empty;
int data;
int* retVal;


// waste some time
void *count(void *arg) {
        unsigned long int c = (unsigned long int)arg;
        int i;
        for (i = 0; i < c; i++) {
                if ((i % 1000) == 0) {
//                            printf("tid: 0x%x Just counted to %d of %ld\n", \(unsigned int)pthread_self(), i, c);
                }
        }
    return arg;
}


void *producer(void* arg){
    int produced;
    printf("\nProducer created");
    printf("\nProducer id is %ld\n",pthread_self());
    for(produced=0;produced<30;produced++){
        sem_wait(&empty);
        sem_wait(&mutex);
        data=produced;
        sem_post(&mutex);
        sem_post(&full);
        printf("\nProducer: %d",data);
    }

    return arg;
}

void *consumer(void* arg){
    int consumed, total=0;
    int *thread = (int*)arg;
    printf("\nConsumer created, Thread number: %d\n", *thread);
    printf("\nConsumer id is %ld\n",pthread_self());
    for(consumed=0;consumed<10;consumed++){
        sem_wait(&full);
        sem_wait(&mutex);
        total=total+data;
        printf("\nThread: %d, Consumed: %d", *thread, data);
        sem_post(&mutex);
        sem_post(&empty);
    }
    printf("\nThe total of 10 iterations for thread %d is %d\n",*thread, total);

    *(int*)arg = total;
    return arg;
}


int main(int argc, char **argv) {
    pthread_t threads[THREAD_CNT];
    printf("%ld thread id\n",(long int)threads[0]);
    printf("%ld thread id\n",(long int)threads[1]);
    printf("%ld thread id\n",(long int)threads[2]);
    printf("%ld thread id\n",(long int)threads[3]);
    //int i;

    sem_init(&mutex, 0, 1);
    sem_init(&full, 0, 0);
    sem_init(&empty, 0, 1);
    int x=1,y=2, z=3;
    int *a = &x;
    int *b = &y;
    int *c = &z;
    int t=1,u=2, v=3;
    int *d = &t;
    int *e = &u;
    int *f = &v;
    //retVal = &q;

    pthread_create(&threads[0], NULL, producer, NULL);
    pthread_create(&threads[1], NULL, consumer, (void*)a);
    pthread_create(&threads[2], NULL, consumer, (void*)b);
    pthread_create(&threads[3], NULL, consumer, (void*)c);
    

    //create THRAD_CNT threads
    /*
        for(i = 0; i<THREAD_CNT; i++) {
                pthread_create(&threads[i], NULL, count, (void *)((i+1)*cnt));
        }
    */


    //join all threads ... not important for proj2
    //int* ret = &g;
        //for(i = 0; i<THREAD_CNT; i++) {
        //      pthread_join(threads[i], (void**)&ret);
      //        printf("\n %d returned\n",*ret);

    //    }
    
    pthread_join(threads[1], (void**)&d);
    printf("\n %d returned\n",*d);
    
    pthread_join(threads[3], (void**)&f);
    printf("\n %d returned\n",*f);
    
    pthread_join(threads[2], (void**)&e);
    printf("\n %d returned\n",*e);
  
    
    // But we have to make sure that main does not return before
    // the threads are done... so count some more...
    count((void *)(100000000*(THREAD_CNT + 1)));
    return 0;
}
