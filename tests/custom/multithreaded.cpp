#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

// Thread function to be executed by each thread
void *print_message(void *threadid) {
    long tid = (long)threadid;  // Cast the thread ID to long
    printf("Thread %ld: Hello, World!\n", tid);
    pthread_exit(NULL);  // Exit the thread
}

int main(int argc, char **argv) {
    
    int num_threads = atoi(argv[1]);
    
    pthread_t threads[num_threads];  // Array to hold thread IDs
    int rc;
    long t;

    // Create threads
    for (t = 0; t < num_threads; t++) {
        printf("Creating thread %ld\n", t);
        rc = pthread_create(&threads[t], NULL, print_message, (void *)t);

        if (rc) {
            printf("Error: Unable to create thread %ld, return code: %d\n", t, rc);
            exit(-1);
        }
    }

    // Wait for all threads to complete
    for (t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    printf("All threads completed.\n");
    return 0;
}
