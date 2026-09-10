
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define DEFAULT_N 10
#define MAX_N 100
#define HASH_SIZE 1000
#define PATH "/"
#define PROJ_ID 65

typedef struct {
    int shmid;
    int *shm_ptr;
    int n;
    int *hash_table;
    int hash_count;
} SharedMemory;

// Function declarations
int create_shared_memory(int n);
void initialize_shared_memory(SharedMemory *sm);
void wait_for_followers(SharedMemory *sm);
bool check_sum_in_hash(SharedMemory *sm, int sum);
void add_sum_to_hash(SharedMemory *sm, int sum);
void process_leader_turn(SharedMemory *sm);
void cleanup_shared_memory(SharedMemory *sm);

int main(int argc, char *argv[]) {
    SharedMemory sm = {0};
    
    // Parse command line arguments
    sm.n = (argc > 1) ? atoi(argv[1]) : DEFAULT_N;
    if (sm.n > MAX_N || sm.n <= 0) {
        printf("Error: n should be between 1 and %d\n", MAX_N);
        return 1;
    }

    // Initialize random seed
    srand(time(NULL));

    // Create and initialize shared memory
    sm.shmid = create_shared_memory(sm.n);
    if (sm.shmid == -1) return 1;
    
    // Initialize hash table
    sm.hash_table = (int *)calloc(HASH_SIZE, sizeof(int));
    sm.hash_count = 0;

    initialize_shared_memory(&sm);
    printf("Wait for the moment.\n");
    
    // Main process loop
    wait_for_followers(&sm);
    
    while (1) {
        process_leader_turn(&sm);
        if (sm.shm_ptr[2] == -1) break;  // Termination condition
        
        // Wait for my turn (0)
        while (sm.shm_ptr[2] != 0) {
            usleep(1000);  // Small delay to reduce CPU usage
        }
    }

    cleanup_shared_memory(&sm);
    return 0;
}

int create_shared_memory(int n) {
    key_t key = ftok(PATH, PROJ_ID);
    if (key == -1) {
        perror("ftok error");
        return -1;
    }
    
    int shmid = shmget(key, (n + 4) * sizeof(int), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid == -1) {
        perror("shmget error");
        return -1;
    }
    return shmid;
}

void initialize_shared_memory(SharedMemory *sm) {
    sm->shm_ptr = (int *)shmat(sm->shmid, NULL, 0);
    if ((void *)sm->shm_ptr == (void *)-1) {
        perror("shmat error");
        exit(1);
    }
    
    // Initialize shared memory
    sm->shm_ptr[0] = sm->n;  // Number of followers
    sm->shm_ptr[1] = 0;      // Number of joined followers
    sm->shm_ptr[2] = 0;      // Turn indicator
}

void wait_for_followers(SharedMemory *sm) {
    while (sm->shm_ptr[1] < sm->n) {
        usleep(1000);
    }
}

bool check_sum_in_hash(SharedMemory *sm, int sum) {
    int hash_index = sum % HASH_SIZE;
    while (sm->hash_table[hash_index] != 0) {
        if (sm->hash_table[hash_index] == sum) return true;
        hash_index = (hash_index + 1) % HASH_SIZE;
    }
    return false;
}

void add_sum_to_hash(SharedMemory *sm, int sum) {
    int hash_index = sum % HASH_SIZE;
    while (sm->hash_table[hash_index] != 0) {
        hash_index = (hash_index + 1) % HASH_SIZE;
    }
    sm->hash_table[hash_index] = sum;
    sm->hash_count++;
}

void process_leader_turn(SharedMemory *sm) {
    // Write random number
    sm->shm_ptr[3] = rand() % 99 + 1;
    
    // Calculate and display sum
    int sum = sm->shm_ptr[3];
    printf("%d", sm->shm_ptr[3]);
    
    for (int i = 1; i <= sm->n; i++) {
        sum += sm->shm_ptr[3 + i];
        printf(" + %d", sm->shm_ptr[3 + i]);
    }
    printf(" = %d\n", sum);
    
    // Check if sum is duplicate
    if (check_sum_in_hash(sm, sum)) {
        sm->shm_ptr[2] = -1;  // Signal termination
    } else {
        add_sum_to_hash(sm, sum);
        sm->shm_ptr[2] = 1;   // Signal F1's turn
    }
}

void cleanup_shared_memory(SharedMemory *sm) {
    shmdt(sm->shm_ptr);
    shmctl(sm->shmid, IPC_RMID, NULL);
    free(sm->hash_table);
}