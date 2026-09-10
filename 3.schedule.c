#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MAX_BURSTS 50  // Maximum number of bursts per process
#define MAX_PROCESSES 1000 // Maximum number of processes
#define INFINITY 1000000000 // For FCFS simulation

// Process states
typedef enum {
    NEW,
    READY,
    RUNNING,
    WAITING,
    TERMINATED
} ProcessState;

// Structure to store process information
typedef struct {
    int id;
    int arrival_time;
    int bursts[MAX_BURSTS];  // Alternate CPU and IO bursts
    int num_bursts;
    ProcessState state;
    int current_burst;       // Index of current burst
    int remaining_time;      // Remaining time in current burst
    int wait_time;          // Total time spent waiting
    int turnaround_time;    // Total time from arrival to completion
    int total_runtime;      // Sum of all CPU and IO bursts
} Process;

// Structure for FIFO queue
typedef struct {
    int data[MAX_PROCESSES];
    int front, rear;
    int size;
} Queue;

// Structure for event queue (min-heap)
typedef struct {
    int process_idx;
    int time;
    int event_type;  // 0: arrival, 1: CPU end, 2: IO end, 3: timeout
} Event;

typedef struct {
    Event data[MAX_PROCESSES * MAX_BURSTS];
    int size;
} Heap;

// Global variables
Process processes[MAX_PROCESSES];
int num_processes;
Queue ready_queue;
Heap event_heap;
int current_time;
int cpu_idle_time;
int cpu_busy_time;

// Queue operations
void init_queue(Queue *q) {
    q->front = q->rear = -1;
    q->size = 0;
}

int is_empty(Queue *q) {
    return q->size == 0;
}

void enqueue(Queue *q, int value) {
    if (q->size == 0) {
        q->front = q->rear = 0;
    } else {
        q->rear = (q->rear + 1) % MAX_PROCESSES;
    }
    q->data[q->rear] = value;
    q->size++;
}

int dequeue(Queue *q) {
    int value = q->data[q->front];
    q->size--;
    if (q->size == 0) {
        q->front = q->rear = -1;
    } else {
        q->front = (q->front + 1) % MAX_PROCESSES;
    }
    return value;
}

int front(Queue *q) {
    return q->data[q->front];
}

// Heap operations
void init_heap(Heap *h) {
    h->size = 0;
}

void swap_events(Event *a, Event *b) {
    Event temp = *a;
    *a = *b;
    *b = temp;
}

void heapify_up(Heap *h, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (h->data[parent].time > h->data[idx].time ||
            (h->data[parent].time == h->data[idx].time && 
             h->data[parent].event_type > h->data[idx].event_type) ||
            (h->data[parent].time == h->data[idx].time && 
             h->data[parent].event_type == h->data[idx].event_type &&
             processes[h->data[parent].process_idx].id > processes[h->data[idx].process_idx].id)) {
            swap_events(&h->data[parent], &h->data[idx]);
            idx = parent;
        } else {
            break;
        }
    }
}

void heapify_down(Heap *h, int idx) {
    while (1) {
        int smallest = idx;
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;

        if (left < h->size && (h->data[left].time < h->data[smallest].time ||
            (h->data[left].time == h->data[smallest].time && 
             h->data[left].event_type < h->data[smallest].event_type) ||
            (h->data[left].time == h->data[smallest].time && 
             h->data[left].event_type == h->data[smallest].event_type &&
             processes[h->data[left].process_idx].id < processes[h->data[smallest].process_idx].id))) {
            smallest = left;
        }

        if (right < h->size && (h->data[right].time < h->data[smallest].time ||
            (h->data[right].time == h->data[smallest].time && 
             h->data[right].event_type < h->data[smallest].event_type) ||
            (h->data[right].time == h->data[smallest].time && 
             h->data[right].event_type == h->data[smallest].event_type &&
             processes[h->data[right].process_idx].id < processes[h->data[smallest].process_idx].id))) {
            smallest = right;
        }

        if (smallest != idx) {
            swap_events(&h->data[idx], &h->data[smallest]);
            idx = smallest;
        } else {
            break;
        }
    }
}

void insert_event(Heap *h, int process_idx, int time, int event_type) {
    Event e = {process_idx, time, event_type};
    h->data[h->size] = e;
    heapify_up(h, h->size);
    h->size++;
}

Event extract_min(Heap *h) {
    Event min = h->data[0];
    h->size--;
    if (h->size > 0) {
        h->data[0] = h->data[h->size];
        heapify_down(h, 0);
    }
    return min;
}

// Function to read input file
void read_input() {
    FILE *fp = fopen("proc.txt", "r");
    if (!fp) {
        printf("Error opening input file\n");
        exit(1);
    }

    fscanf(fp, "%d", &num_processes);
    
    for (int i = 0; i < num_processes; i++) {
        Process *p = &processes[i];
        int j = 0;
        
        // Read process ID and arrival time
        fscanf(fp, "%d %d", &p->id, &p->arrival_time);
        
        // Read bursts until -1 is encountered
        while (1) {
            int burst;
            fscanf(fp, "%d", &burst);
            p->bursts[j] = burst;
            j++;
            
            // Check if we've reached the -1 marker
            if (burst == -1) {
                p->num_bursts = j - 1;  // Subtract 1 to not count the -1
                break;
            }
            
            // Safety check to prevent buffer overflow
            if (j >= MAX_BURSTS) {
                printf("Error: Too many bursts for process %d\n", p->id);
                exit(1);
            }
        }
        
        p->state = NEW;
        p->current_burst = 0;
        p->remaining_time = p->bursts[0];
        p->wait_time = 0;
        p->turnaround_time = 0;
        
        // Calculate total runtime
        p->total_runtime = 0;
        for (int k = 0; k < p->num_bursts; k++) {
            if (p->bursts[k] != -1) {
                p->total_runtime += p->bursts[k];
            }
        }
    }
    
    fclose(fp);
}

// Schedule next process on CPU
void schedule_process(int quantum) {
    if (is_empty(&ready_queue)) {
        return;
    }

    int proc_idx = front(&ready_queue);
    Process *p = &processes[proc_idx];
    dequeue(&ready_queue);
    
    p->state = RUNNING;
    int run_time = quantum;
    if (p->remaining_time < quantum) {
        run_time = p->remaining_time;
    }

    #ifdef VERBOSE
    printf("%d : Process %d is scheduled to run for time %d\n", current_time, p->id, run_time);
    #endif

    // Schedule next event (CPU burst end or timeout)
    if (run_time == p->remaining_time) {
        insert_event(&event_heap, proc_idx, current_time + run_time, 1); // CPU burst end
    } else {
        insert_event(&event_heap, proc_idx, current_time + run_time, 3); // Timeout
    }
}

// Main simulation function
void simulate(int quantum, const char *filename) {
    FILE *output_file = fopen(filename, "a+");
    if (!output_file) {
        printf("Error opening output file: %s\n", filename);
        exit(1);
    }

    // Initialize data structures
    init_queue(&ready_queue);
    init_heap(&event_heap);
    current_time = 0;
    cpu_idle_time = 0;
    cpu_busy_time = 0;

    // Print scheduling type header
    if (quantum == INFINITY) {
        fprintf(output_file, "**** FCFS Scheduling ****\n");
    } else {
        fprintf(output_file, "**** RR Scheduling with q = %d ****\n", quantum);
    }

    // Reset process states
    for (int i = 0; i < num_processes; i++) {
        processes[i].state = NEW;
        processes[i].current_burst = 0;
        processes[i].remaining_time = processes[i].bursts[0];
        processes[i].wait_time = 0;
        processes[i].turnaround_time = 0;
        insert_event(&event_heap, i, processes[i].arrival_time, 0);
    }

    #ifdef VERBOSE
    fprintf(output_file, "%d : Starting\n", current_time);
    #endif

    // Main event loop
    while (event_heap.size > 0) {
        Event e = extract_min(&event_heap);
        int prev_time = current_time;
        current_time = e.time;
        Process *p = &processes[e.process_idx];

        if (!is_empty(&ready_queue)) {
            cpu_busy_time += current_time - prev_time;
        } else {
            cpu_idle_time += current_time - prev_time;
        }

        switch (e.event_type) {
            case 0: // Process arrival
                #ifdef VERBOSE
                fprintf(output_file, "%d : Process %d joins ready queue upon arrival\n", 
                        current_time, p->id);
                #endif
                p->state = READY;
                enqueue(&ready_queue, e.process_idx);
                break;

            case 1: // CPU burst end
                p->current_burst++;
                if (p->current_burst >= p->num_bursts) {
                    p->state = TERMINATED;
                    p->turnaround_time = current_time - p->arrival_time;
                    fprintf(output_file, "%d : Process %d exits. Turnaround time = %d (%d%%), Wait time = %d\n",
                            current_time, p->id, p->turnaround_time,
                            (p->turnaround_time * 100) / p->total_runtime, p->wait_time);
                } else {
                    p->state = WAITING;
                    p->remaining_time = p->bursts[p->current_burst + 1];
                    insert_event(&event_heap, e.process_idx,
                               current_time + p->bursts[p->current_burst], 2);
                }
                #ifdef VERBOSE
                if (is_empty(&ready_queue)) 
                    fprintf(output_file, "%d : CPU goes idle\n", current_time);
                #endif
                break;

            case 2: // IO burst end
                p->current_burst++;
                p->remaining_time = p->bursts[p->current_burst];
                #ifdef VERBOSE
                fprintf(output_file, "%d : Process %d joins ready queue after IO completion\n", 
                        current_time, p->id);
                #endif
                p->state = READY;
                enqueue(&ready_queue, e.process_idx);
                break;

            case 3: // Quantum timeout
                p->remaining_time -= quantum;
                #ifdef VERBOSE
                fprintf(output_file, "%d : Process %d joins ready queue after timeout\n", 
                        current_time, p->id);
                #endif
                p->state = READY;
                enqueue(&ready_queue, e.process_idx);
                break;
        }

        // Schedule next process if CPU is free
        if (!is_empty(&ready_queue)) {
            for (int i = 0; i < num_processes; i++) {
                if (processes[i].state == READY) {
                    processes[i].wait_time += current_time - prev_time;
                }
            }
            if (e.event_type == 1 || e.event_type == 3 ||e.event_type == 2|| 
                (e.event_type == 0 && p->state == READY)) {
                schedule_process(quantum);
            }
        }
    }

    // Print summary statistics
    double avg_wait_time = 0;
    for (int i = 0; i < num_processes; i++) {
        avg_wait_time += processes[i].wait_time;
    }
    avg_wait_time /= num_processes;

    fprintf(output_file, "Average wait time = %.2f\n", avg_wait_time);
    fprintf(output_file, "Total turnaround time = %d\n", current_time);
    fprintf(output_file, "CPU idle time = %d\n", cpu_idle_time);
    fprintf(output_file, "CPU utilization = %.2f%%\n", (100.0 * cpu_busy_time) / current_time);

    fclose(output_file);
}


int main() {
    read_input();

    printf("**** FCFS Scheduling ****\n");
    simulate(INFINITY,"output.txt");
    printf("\n**** RR Scheduling with q = 10 ****\n");
    simulate(10,"output.txt");
    printf("\n**** RR Scheduling with q = 5 ****\n");
    simulate(5,"output.txt");

    return 0;
}