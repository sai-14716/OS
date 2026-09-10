#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

// Configuration constants
#define RESOURCE_LIMIT 20
#define THREAD_LIMIT 100

// Operation types
#define OP_FREE 0
#define OP_ACQUIRE 1
#define OP_TERMINATE 2

// Global resource tracking
int resource_count, thread_count;
int free_resources[RESOURCE_LIMIT];
int claimed_maximum[THREAD_LIMIT][RESOURCE_LIMIT];
int current_holdings[THREAD_LIMIT][RESOURCE_LIMIT];
int remaining_needs[THREAD_LIMIT][RESOURCE_LIMIT];
int processed_flag[THREAD_LIMIT];

// Synchronization objects
pthread_mutex_t resource_lock;
pthread_mutex_t output_lock;
pthread_barrier_t startup_sync;
pthread_barrier_t operation_sync;
pthread_barrier_t **thread_ack;
pthread_cond_t *signal_condition;
pthread_mutex_t *condition_lock;

// Shared operation data
int active_thread;
int operation_code;
int resource_vector[RESOURCE_LIMIT];

// Pending operations queue
typedef struct {
    int thread_index;
    int resources[RESOURCE_LIMIT];
} PendingOperation;

PendingOperation *operation_queue;
int queue_length;
int queue_max_size;

// Function declarations
void *coordinator_function(void *arg);
void *worker_function(void *arg);
void setup_data_structures();
void load_configuration();
void handle_operation(int thread_index, int op_type, int *resources);
void process_resource_release(int thread_index, int *resources);
int validate_resource_request(int thread_index, int *resources);
void fulfill_resource_request(int thread_index, int *resources);
void add_to_queue(int thread_index, int *resources);
void attempt_pending_operations();
int safety_check();

int main() {
    pthread_t coordinator, *workers;
    int i;
    
    // Initialize structures and load configuration
    setup_data_structures();
    load_configuration();
    
    // Create synchronization objects
    thread_ack = (pthread_barrier_t **)malloc(thread_count * sizeof(pthread_barrier_t *));
    signal_condition = (pthread_cond_t *)malloc(thread_count * sizeof(pthread_cond_t));
    condition_lock = (pthread_mutex_t *)malloc(thread_count * sizeof(pthread_mutex_t));
    
    for (i = 0; i < thread_count; i++) {
        thread_ack[i] = (pthread_barrier_t *)malloc(sizeof(pthread_barrier_t));
        pthread_barrier_init(thread_ack[i], NULL, 2);
        pthread_cond_init(&signal_condition[i], NULL);
        pthread_mutex_init(&condition_lock[i], NULL);
    }
    
    // Initialize central synchronization objects
    pthread_mutex_init(&resource_lock, NULL);
    pthread_mutex_init(&output_lock, NULL);
    pthread_barrier_init(&startup_sync, NULL, thread_count + 1);
    pthread_barrier_init(&operation_sync, NULL, 2);
    
    // Initialize operation queue
    queue_max_size = thread_count;
    queue_length = 0;
    operation_queue = (PendingOperation *)malloc(queue_max_size * sizeof(PendingOperation));
    
    // Launch worker threads
    workers = (pthread_t *)malloc(thread_count * sizeof(pthread_t));
    for (i = 0; i < thread_count; i++) {
        int *thread_id = (int *)malloc(sizeof(int));
        *thread_id = i;
        pthread_create(&workers[i], NULL, worker_function, thread_id);
    }
    
    // Launch coordinator thread
    pthread_create(&coordinator, NULL, coordinator_function, NULL);
    
    // Wait for completion
    pthread_join(coordinator, NULL);
    for (i = 0; i < thread_count; i++) {
        pthread_join(workers[i], NULL);
    }
    
    // Cleanup resources
    for (i = 0; i < thread_count; i++) {
        pthread_barrier_destroy(thread_ack[i]);
        pthread_cond_destroy(&signal_condition[i]);
        pthread_mutex_destroy(&condition_lock[i]);
        processed_flag[i] = 0;
        free(thread_ack[i]);
    }
    
    pthread_mutex_destroy(&resource_lock);
    pthread_mutex_destroy(&output_lock);
    pthread_barrier_destroy(&startup_sync);
    pthread_barrier_destroy(&operation_sync);
    
    free(thread_ack);
    free(signal_condition);
    free(condition_lock);
    free(operation_queue);
    free(workers);
    
    return 0;
}

void setup_data_structures() {
    int i, j;
    
    // Zero out all tracking matrices
    for (i = 0; i < THREAD_LIMIT; i++) {
        for (j = 0; j < RESOURCE_LIMIT; j++) {
            claimed_maximum[i][j] = 0;
            current_holdings[i][j] = 0;
            remaining_needs[i][j] = 0;
        }
    }
    
    // Zero out available resources
    for (j = 0; j < RESOURCE_LIMIT; j++) {
        free_resources[j] = 0;
    }
}

void load_configuration() {
    FILE *config;
    int i;
    
    config = fopen("input/system.txt", "r");
    if (config == NULL) {
        perror("Unable to open system configuration file");
        exit(1);
    }
    
    // Read resource and thread counts
    fscanf(config, "%d", &resource_count);
    fscanf(config, "%d", &thread_count);
    
    // Read initial free resource quantities
    for (i = 0; i < resource_count; i++) {
        fscanf(config, "%d", &free_resources[i]);
    }
    
    fclose(config);
}

void *coordinator_function(void *arg) {
    int completed = 0;
    int i;
    
    // Wait for all threads to initialize
    pthread_barrier_wait(&startup_sync);
    
    // Process operations until all threads complete
    while (completed < thread_count) {
        // Wait for an operation request
        pthread_barrier_wait(&operation_sync);
        
        if (operation_code == OP_TERMINATE) {
            // Handle thread termination
            pthread_mutex_lock(&output_lock);
            printf(" Thread %d is granted its last resource request\n", active_thread);
            pthread_mutex_unlock(&output_lock);
            
            // Return all thread resources to pool
            for (i = 0; i < resource_count; i++) {
                free_resources[i] += current_holdings[active_thread][i];
                current_holdings[active_thread][i] = 0;
                remaining_needs[active_thread][i] = 0;
            }
            
            completed++;
            pthread_barrier_wait(thread_ack[active_thread]);
            
            // Try to satisfy waiting threads
            attempt_pending_operations();
        } else {
            // Handle acquisition or release operations
            handle_operation(active_thread, operation_code, resource_vector);
        }
    }
    
    return NULL;
}

void *worker_function(void *arg) {
    int thread_idx = *((int *)arg);
    char filename[50];
    FILE *thread_file;
    int i, j;
    int delay_time, op_type;
    int operation_vector[RESOURCE_LIMIT];
    int has_acquisition = 0;
    
    // Announce thread startup
    pthread_mutex_lock(&output_lock);
    printf(" Thread %d born\n", thread_idx);
    pthread_mutex_unlock(&output_lock);
    
    // Build filename for thread instructions
    sprintf(filename, "input/thread%02d.txt", thread_idx);
    
    // Open thread instruction file
    thread_file = fopen(filename, "r");
    if (thread_file == NULL) {
        printf("Error opening thread file %s", filename);
        exit(1);
    }
    
    // Read maximum resource needs
    for (j = 0; j < resource_count; j++) {
        fscanf(thread_file, "%d", &claimed_maximum[thread_idx][j]);
        remaining_needs[thread_idx][j] = claimed_maximum[thread_idx][j];
    }
    
    // Synchronize with all threads before starting operations
    pthread_barrier_wait(&startup_sync);
    
    // Process operations from file
    while (1) {
        // Read delay and operation type
        fscanf(thread_file, "%d", &delay_time);
        fscanf(thread_file, "%s", filename); // Read R or Q
        
        // Apply delay between operations
        usleep(delay_time * 10000);
        
        if (filename[0] == 'Q') {
            // Request termination
            pthread_mutex_lock(&resource_lock);
            
            // Setup termination request
            active_thread = thread_idx;
            operation_code = OP_TERMINATE;
            
            // Notify coordinator
            pthread_barrier_wait(&operation_sync);
            
            // Wait for acknowledgment
            pthread_barrier_wait(thread_ack[thread_idx]);
            pthread_mutex_unlock(&resource_lock);
            break;
        } else {
            // Read resource vector
            has_acquisition = 0;
            for (j = 0; j < resource_count; j++) {
                fscanf(thread_file, "%d", &operation_vector[j]);
                if (operation_vector[j] > 0) {
                    has_acquisition = 1;
                }
            }
            
            // Determine operation type
            op_type = has_acquisition ? OP_ACQUIRE : OP_FREE;
            
            // Lock resource management
            pthread_mutex_lock(&resource_lock);
            
            // Log operation type
            if (op_type == OP_ACQUIRE) {
                pthread_mutex_lock(&output_lock);
                printf(" Thread %d sends resource request: type = ADDITIONAL\n", thread_idx);
                pthread_mutex_unlock(&output_lock);
            } else {
                pthread_mutex_lock(&output_lock);
                printf(" Thread %d sends resource request: type = RELEASE\n", thread_idx);
                pthread_mutex_unlock(&output_lock);
            }
            
            // Setup operation request
            active_thread = thread_idx;
            operation_code = op_type;
            for (j = 0; j < resource_count; j++) {
                resource_vector[j] = operation_vector[j];
            }
            
            // Notify coordinator
            pthread_mutex_unlock(&resource_lock);
            pthread_barrier_wait(&operation_sync);
            
            // Wait for acknowledgment
            pthread_barrier_wait(thread_ack[thread_idx]);
            
            // For acquisitions, wait until granted
            if (op_type == OP_ACQUIRE) {
                pthread_mutex_lock(&condition_lock[thread_idx]);
                while (processed_flag[thread_idx] == 0)
                    pthread_cond_wait(&signal_condition[thread_idx], &condition_lock[thread_idx]);
                processed_flag[thread_idx] = 0;
                pthread_mutex_unlock(&condition_lock[thread_idx]);
            }
        }
    }
    
    // Cleanup
    fclose(thread_file);
    free(arg);
    return NULL;
}

void handle_operation(int thread_idx, int op_type, int *resources) {
    int i;
    int local_resources[RESOURCE_LIMIT];
    
    // Copy resources to local array
    for (i = 0; i < resource_count; i++) {
        local_resources[i] = resources[i];
    }
    
    // Process by operation type
    if (op_type == OP_FREE) {
        // Release resources
        process_resource_release(thread_idx, local_resources);
        
        pthread_mutex_lock(&output_lock);
        printf(" Thread %d is done with its resource release request\n", thread_idx);
        pthread_mutex_unlock(&output_lock);
        
        pthread_barrier_wait(thread_ack[active_thread]);
        
        // Try to grant pending requests
        attempt_pending_operations();
    } else if (op_type == OP_ACQUIRE) {
        // Handle any releases first (negative values)
        for (i = 0; i < resource_count; i++) {
            if (local_resources[i] < 0) {
                free_resources[i] += -local_resources[i];
                current_holdings[thread_idx][i] += local_resources[i]; // Add negative value
                remaining_needs[thread_idx][i] -= local_resources[i]; // Subtract negative value
                local_resources[i] = 0;
            }
        }
        
        // Queue the acquisition request
        pthread_mutex_lock(&output_lock);
        printf("Master thread stores resource request of thread %d\n", thread_idx);
        pthread_mutex_unlock(&output_lock);
        add_to_queue(thread_idx, local_resources);
        
        pthread_barrier_wait(thread_ack[active_thread]);
        
        // Show waiting threads
        pthread_mutex_lock(&output_lock);
        printf(" Waiting threads: ");
        for (i = 0; i < queue_length; i++) {
            printf("%d ", operation_queue[i].thread_index);
        }
        printf("\n");
        pthread_mutex_unlock(&output_lock);
        
        // Try to fulfill pending requests
        attempt_pending_operations();
    }
}

void process_resource_release(int thread_idx, int *resources) {
    int i;
    
    // Return resources to free pool
    for (i = 0; i < resource_count; i++) {
        if (resources[i] < 0) {
            free_resources[i] += -resources[i];
            current_holdings[thread_idx][i] += resources[i]; // Add negative value
            remaining_needs[thread_idx][i] -= resources[i]; // Subtract negative value
        }
    }
}

int validate_resource_request(int thread_idx, int *resources) {
    int i;
    
    // Check if sufficient resources are available
    for (i = 0; i < resource_count; i++) {
        if (resources[i] > free_resources[i]) {
            return 0;
        }
    }
    
    // Deadlock avoidance (safety check) if enabled
#ifdef _DLAVOID
    int temp_free[RESOURCE_LIMIT];
    int temp_holdings[THREAD_LIMIT][RESOURCE_LIMIT];
    int temp_needs[THREAD_LIMIT][RESOURCE_LIMIT];
    int j;
    
    // Create temporary state after allocation
    for (i = 0; i < resource_count; i++) {
        temp_free[i] = free_resources[i] - resources[i];
    }
    
    for (i = 0; i < thread_count; i++) {
        for (j = 0; j < resource_count; j++) {
            temp_holdings[i][j] = current_holdings[i][j];
            temp_needs[i][j] = remaining_needs[i][j];
        }
    }
    
    // Apply the resource request
    for (j = 0; j < resource_count; j++) {
        temp_holdings[thread_idx][j] += resources[j];
        temp_needs[thread_idx][j] -= resources[j];
    }
    
    // Safety algorithm
    int available_work[RESOURCE_LIMIT];
    int completed[THREAD_LIMIT];
    int progress_made;
    
    // Initialize tracking arrays
    for (i = 0; i < resource_count; i++) {
        available_work[i] = temp_free[i];
    }
    
    for (i = 0; i < thread_count; i++) {
        completed[i] = 0;
    }
    
    // Try to find a sequence of completions
    progress_made = 1;
    while (progress_made) {
        progress_made = 0;
        for (i = 0; i < thread_count; i++) {
            if (!completed[i]) {
                int can_finish = 1;
                for (j = 0; j < resource_count; j++) {
                    if (temp_needs[i][j] > available_work[j]) {
                        can_finish = 0;
                        break;
                    }
                }
                
                if (can_finish) {
                    // Thread can finish with available resources
                    for (j = 0; j < resource_count; j++) {
                        available_work[j] += temp_holdings[i][j];
                    }
                    completed[i] = 1;
                    progress_made = 1;
                }
            }
        }
    }
    
    // Check if all threads can finish
    for (i = 0; i < thread_count; i++) {
        if (!completed[i]) {
            return 0;
        }
    }
#endif

    return 1;
}

void fulfill_resource_request(int thread_idx, int *resources) {
    int i;
    
    // Allocate resources to thread
    for (i = 0; i < resource_count; i++) {
        free_resources[i] -= resources[i];
        current_holdings[thread_idx][i] += resources[i];
        remaining_needs[thread_idx][i] -= resources[i];
    }
    
    // Signal the thread that resources are available
    pthread_mutex_lock(&condition_lock[thread_idx]);
    processed_flag[thread_idx] = 1;
    pthread_cond_signal(&signal_condition[thread_idx]);
    pthread_mutex_unlock(&condition_lock[thread_idx]);
    
    pthread_mutex_lock(&output_lock);
    printf("Master thread grants resource request for thread %d\n", thread_idx);
    pthread_mutex_unlock(&output_lock);
}

void add_to_queue(int thread_idx, int *resources) {
    int i;
    
    // Expand queue if needed
    if (queue_length == queue_max_size) {
        queue_max_size *= 2;
        operation_queue = (PendingOperation *)realloc(operation_queue, queue_max_size * sizeof(PendingOperation));
    }
    
    // Add request to queue
    operation_queue[queue_length].thread_index = thread_idx;
    for (i = 0; i < resource_count; i++) {
        operation_queue[queue_length].resources[i] = resources[i];
    }
    queue_length++;
}

void attempt_pending_operations() {
    int i, j, k;
    
    pthread_mutex_lock(&output_lock);
    printf("Master thread tries to grant pending requests\n");
    pthread_mutex_unlock(&output_lock);
    
    // Try to fulfill all pending requests
    i = 0;
    while (i < queue_length) {
        if (validate_resource_request(operation_queue[i].thread_index, operation_queue[i].resources)) {
            // Grant resources
            fulfill_resource_request(operation_queue[i].thread_index, operation_queue[i].resources);
            
            // Remove fulfilled request from queue
            for (j = i; j < queue_length - 1; j++) {
                operation_queue[j].thread_index = operation_queue[j + 1].thread_index;
                for (k = 0; k < resource_count; k++) {
                    operation_queue[j].resources[k] = operation_queue[j + 1].resources[k];
                }
            }
            queue_length--;
            
            // Start over to check all requests again
            i = 0;
        } else {
            pthread_mutex_lock(&output_lock);
            printf("+++ Insufficient resources to grant request of thread %d\n", operation_queue[i].thread_index);
            pthread_mutex_unlock(&output_lock);
            i++;
        }
    }
    
    // Display waiting threads
    pthread_mutex_lock(&output_lock);
    printf(" Waiting threads: ");
    for (i = 0; i < queue_length; i++) {
        printf("%d ", operation_queue[i].thread_index);
    }
    printf("\n");
    pthread_mutex_unlock(&output_lock);
}
