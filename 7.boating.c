#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

// Semaphore implementation
typedef struct {
    int value;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
} semaphore;

// Function prototypes
void P(semaphore *s);
void V(semaphore *s);
void *boat_function(void *arg);
void *visitor_function(void *arg);

// Global variables
int m, n;                          // Number of boats and visitors
semaphore boat = {0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};
semaphore rider = {0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};
pthread_mutex_t bmtx = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t EOS;             // End of session barrier
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex for ordered printing

// Shared arrays for boat-visitor synchronization
bool *BA;                          // Boat availability array
int *BC;                           // Boat's current visitor
int *BT;                           // Boat ride time
pthread_barrier_t *BB;             // Barriers for boat-visitor synchronization
bool *visitor_completed;           // Track completed visitors

// Implementation of P (wait) operation
void P(semaphore *s) {
    pthread_mutex_lock(&s->mtx);
    s->value--;
    if (s->value < 0) {
        pthread_cond_wait(&s->cv, &s->mtx);
    }
    pthread_mutex_unlock(&s->mtx);
}

// Implementation of V (signal) operation
void V(semaphore *s) {
    pthread_mutex_lock(&s->mtx);
    s->value++;
    if (s->value <= 0) {
        pthread_cond_signal(&s->cv);
    }
    pthread_mutex_unlock(&s->mtx);
}

// Boat thread function
void *boat_function(void *arg) {
    int boat_id = *((int *)arg);
    free(arg); // Free the allocated memory for boat_id
    
    pthread_mutex_lock(&print_mutex);
    printf("Boat %d Ready\n", boat_id);
    pthread_mutex_unlock(&print_mutex);
    
    // Initialize boat-specific barrier
    pthread_barrier_init(&BB[boat_id-1], NULL, 2);
    
    // Mark the boat as available initially
    pthread_mutex_lock(&bmtx);
    BA[boat_id-1] = true;
    BC[boat_id-1] = -1;
    pthread_mutex_unlock(&bmtx);
    
    int completed_visitors = 0;
    int total_visitors_served = 0;
    
    while (1) {
        // Signal that a boat is available
        V(&rider);
        
        // Wait for a visitor
        P(&boat);
        
        // Get ready to receive a visitor
        pthread_mutex_lock(&bmtx);
        BA[boat_id-1] = true;
        BC[boat_id-1] = -1;
        pthread_mutex_unlock(&bmtx);
        
        // Wait for a visitor to choose this boat
        pthread_barrier_wait(&BB[boat_id-1]);
        
        // Get the visitor ID and ride time
        pthread_mutex_lock(&bmtx);
        int visitor_id = BC[boat_id-1];
        int ride_time = BT[boat_id-1];
        BA[boat_id-1] = false;  // Mark boat as not available
        pthread_mutex_unlock(&bmtx);
        
        // Start the ride
        pthread_mutex_lock(&print_mutex);
        printf("Boat %d Start of ride for visitor %d\n", boat_id, visitor_id);
        pthread_mutex_unlock(&print_mutex);
        
        // Simulate the ride (scaled down time)
        usleep(ride_time * 100000);  // Convert minutes to microseconds (scaled)
        
        // End the ride
        pthread_mutex_lock(&print_mutex);
        printf("Boat %d End of ride for visitor %d (ride time = %d)\n", boat_id, visitor_id, ride_time);
        pthread_mutex_unlock(&print_mutex);
        
        // Mark the boat as available again
        pthread_mutex_lock(&bmtx);
        BA[boat_id-1] = true;
        pthread_mutex_unlock(&bmtx);
        
        total_visitors_served++;
        
        // Check if all visitors are complete
        int all_completed = 1;
        for (int i = 0; i < n; i++) {
            if (!visitor_completed[i]) {
                all_completed = 0;
                break;
            }
        }
        
        if (all_completed || total_visitors_served >= n) {
            pthread_barrier_wait(&EOS);
            break;
        }
    }
    
    return NULL;
}

// Visitor thread function
void *visitor_function(void *arg) {
    int visitor_id = *((int *)arg);
    free(arg); // Free the allocated memory for visitor_id
    
    // Decide random visit time (30-120 minutes) and ride time (15-60 minutes)
    int visit_time = 30 + rand() % 91;  // 30 to 120 minutes
    int ride_time = 15 + rand() % 46;   // 15 to 60 minutes
    
    pthread_mutex_lock(&print_mutex);
    printf("Visitor %d Starts sightseeing for %d minutes\n", visitor_id, visit_time);
    pthread_mutex_unlock(&print_mutex);
    
    // Simulate visit to other attractions
    usleep(visit_time * 100000);  // Convert minutes to microseconds (scaled)
    
    pthread_mutex_lock(&print_mutex);
    printf("Visitor %d Ready to ride a boat (ride time = %d)\n", visitor_id, ride_time);
    pthread_mutex_unlock(&print_mutex);
    
    // Signal that a visitor is ready
    V(&boat);
    
    // Wait for a boat
    P(&rider);
    
    // Find an available boat
    int boat_id = -1;
    
    while (boat_id == -1) {
        pthread_mutex_lock(&bmtx);
        
        // Search for an available boat
        for (int i = 0; i < m; i++) {
            if (BA[i] && BC[i] == -1) {
                boat_id = i + 1;
                BC[i] = visitor_id;
                BT[i] = ride_time;
                break;
            }
        }
        
        pthread_mutex_unlock(&bmtx);
        
        // If no boat found, short sleep to prevent busy waiting
        if (boat_id == -1) {
            usleep(1000);  // Sleep for 1ms before trying again
        }
    }
    
    pthread_mutex_lock(&print_mutex);
    printf("Visitor %d Finds boat %d\n", visitor_id, boat_id);
    pthread_mutex_unlock(&print_mutex);
    
    // Wait for the boat to start the ride
    pthread_barrier_wait(&BB[boat_id-1]);
    
    // Ride is now handled by the boat thread
    
    // After the ride, mark visitor as completed
    visitor_completed[visitor_id-1] = true;
    
    // Visitor leaves after the ride
    pthread_mutex_lock(&print_mutex);
    printf("Visitor %d Leaving\n", visitor_id);
    pthread_mutex_unlock(&print_mutex);
    
    return NULL;
}

int main(int argc, char *argv[]) {
    // Check command line arguments
    if (argc != 3) {
        printf("Usage: %s <number of boats> <number of visitors>\n", argv[0]);
        return 1;
    }
    
    // Parse command line arguments
    m = atoi(argv[1]);
    n = atoi(argv[2]);
    
    // Validate input
    if (m < 5 || m > 10 || n < 20 || n > 100) {
        printf("Invalid input: 5 <= m <= 10 and 20 <= n <= 100\n");
        return 1;
    }
    
    // Seed random number generator
    srand(time(NULL));
    
    // Initialize the end of session barrier
    pthread_barrier_init(&EOS, NULL, 2);
    
    // Allocate memory for shared arrays
    BA = (bool *)malloc(m * sizeof(bool));
    BC = (int *)malloc(m * sizeof(int));
    BT = (int *)malloc(m * sizeof(int));
    BB = (pthread_barrier_t *)malloc(m * sizeof(pthread_barrier_t));
    visitor_completed = (bool *)calloc(n, sizeof(bool));
    
    // Initialize shared arrays
    for (int i = 0; i < m; i++) {
        BA[i] = false;
        BC[i] = -1;
        BT[i] = 0;
    }
    
    // Create threads
    pthread_t *boat_threads = (pthread_t *)malloc(m * sizeof(pthread_t));
    pthread_t *visitor_threads = (pthread_t *)malloc(n * sizeof(pthread_t));
    
    // Create boat threads
    for (int i = 0; i < m; i++) {
        int *boat_id = (int *)malloc(sizeof(int));
        *boat_id = i + 1;
        pthread_create(&boat_threads[i], NULL, boat_function, (void *)boat_id);
        // Small delay to ensure ordered initialization
        usleep(10000);
    }
    
    // Small delay before starting visitor threads
    usleep(50000);
    
    // Create visitor threads
    for (int i = 0; i < n; i++) {
        int *visitor_id = (int *)malloc(sizeof(int));
        *visitor_id = i + 1;
        pthread_create(&visitor_threads[i], NULL, visitor_function, (void *)visitor_id);
        // Small delay to maintain order
        usleep(10000);
    }
    
    // Wait for all visitor threads to complete
    for (int i = 0; i < n; i++) {
        pthread_join(visitor_threads[i], NULL);
    }
    
    // Wait for the end of session
    pthread_barrier_wait(&EOS);
    
    // Wait for all boat threads to complete
    for (int i = 0; i < m; i++) {
        pthread_join(boat_threads[i], NULL);
    }
    
    // Clean up resources
    for (int i = 0; i < m; i++) {
        pthread_barrier_destroy(&BB[i]);
    }
    pthread_barrier_destroy(&EOS);
    
    free(BA);
    free(BC);
    free(BT);
    free(BB);
    free(visitor_completed);
    free(boat_threads);
    free(visitor_threads);
    
    return 0;
}
