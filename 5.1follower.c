#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#define DEFAULT_NF 1
#define PATH "/"
#define PROJ_ID 65

typedef struct {
    int shmid;
    int *shm_ptr;
    int follower_id;
} FollowerData;

// Function declarations
int get_shared_memory();
void process_follower(FollowerData *fd);
int join_shared_memory(FollowerData *fd);

int main(int argc, char *argv[]) {
    int nf = (argc > 1) ? atoi(argv[1]) : DEFAULT_NF;
    if (nf <= 0) {
        printf("Error: nf should be positive\n");
        return 1;
    }

    // Initialize random seed
    srand(time(NULL));

    // Get shared memory
    int shmid = get_shared_memory();
    if (shmid == -1) return 1;

    // Create followers
    for (int i = 0; i < nf; i++) {
        pid_t pid = fork();
        if (pid == 0) {  // Child process
            FollowerData fd = {shmid, NULL, 0};
            fd.shm_ptr = (int *)shmat(shmid, NULL, 0);
            
            if ((void *)fd.shm_ptr == (void *)-1) {
                perror("shmat error");
                exit(1);
            }

            // Try to join and get follower ID
            fd.follower_id = join_shared_memory(&fd);
            if (fd.follower_id == -1) {
                shmdt(fd.shm_ptr);
                exit(1);
            }

            printf("follower %d joins\n", fd.follower_id);
            process_follower(&fd);
            
            printf("follower %d leaves\n", fd.follower_id);
            shmdt(fd.shm_ptr);
            exit(0);
        }
    }

    // Wait for all children
    for (int i = 0; i < nf; i++) {
        wait(NULL);
    }

    return 0;
}

int get_shared_memory() {
    key_t key = ftok(PATH, PROJ_ID);
    if (key == -1) {
        perror("ftok error");
        return -1;
    }
    
    int shmid = shmget(key, 0, 0666);
    if (shmid == -1) {
        perror("shmget error");
        return -1;
    }
    return shmid;
}

int join_shared_memory(FollowerData *fd) {
    // Check if more followers can join
    if (fd->shm_ptr[1] >= fd->shm_ptr[0]) {
        printf("follower error: %d followers have already joined\n", fd->shm_ptr[0]);
        return -1;
    }
    
    // Join by incrementing joined count
    return ++fd->shm_ptr[1];
}

void process_follower(FollowerData *fd) {
    while (1) {
        // Wait for my turn
        while (fd->shm_ptr[2] != fd->follower_id && 
               fd->shm_ptr[2] != -fd->follower_id) {
            usleep(1000);
        }
        
        // Check if it's time to terminate
        if (fd->shm_ptr[2] == -fd->follower_id) {
            // Set turn for next follower or leader
            if (fd->follower_id == fd->shm_ptr[0]) {
                fd->shm_ptr[2] = 0;  // Last follower, signal leader
            } else {
                fd->shm_ptr[2] = -(fd->follower_id + 1);  // Signal next follower
            }
            break;
        }
        
        // Write random number
        fd->shm_ptr[3 + fd->follower_id] = rand() % 9 + 1;
        
        // Signal next process
        if (fd->follower_id == fd->shm_ptr[0]) {
            fd->shm_ptr[2] = 0;  // Last follower, signal leader
        } else {
            fd->shm_ptr[2] = fd->follower_id + 1;  // Signal next follower
        }
    }
}

