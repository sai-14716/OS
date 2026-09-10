#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#define USER_FRAMES 12288  // Number of user frames
#define NUM_PAGES 2048     // Virtual memory size in pages
#define ESSENTIAL_PAGES 10 // Number of essential pages (0-9)
#define NFFMIN 1000        // Minimum number of free frames to maintain

// Page Table Entry structure
typedef struct {
    unsigned short int entry;   // Bits 0-13: Frame number, Bit 14: Reference bit, Bit 15: Valid bit
    unsigned short int history; // History for LRU approximation
} PTE;

// Process structure
typedef struct {
    PTE page_table[NUM_PAGES];  // Page table for the process
    int num_searches;           // Number of binary searches to perform
    int *array_size;            // Size of array for binary search
    int *search_keys;           // Search keys
    // Statistics
    int accesses;               // Number of page accesses
    int faults;                 // Number of page faults
    int replacements;           // Number of page replacements
    int attempt_stats[4];       // Statistics for the 4 replacement attempts
} Process;

// Free Frame List Entry
typedef struct {
    int frame;          // Frame number
    int last_owner;     // PID of last owner (-1 if none)
    int last_page;      // Page number of last owner (-1 if none)
} FFEntry;

// Function declarations
void initialize_processes(Process *processes, int n, int m, FILE *fp);
void free_processes(Process *processes, int n);
void initialize_fflist(FFEntry *fflist);
int allocate_frame(FFEntry *fflist, int *nff, int pid, int page);
void release_frames(Process *proc, FFEntry *fflist, int *nff, int pid);
int binary_search(Process *proc, FFEntry *fflist, int *nff, int pid, int key);
void update_page_history(Process *proc);
void print_statistics(Process *processes, int n);

int main() {
    FILE *fp = fopen("search.txt", "r");
    if (!fp) {
        perror("Error opening search.txt");
        return 1;
    }

    int n, m;
    if (fscanf(fp, "%d %d", &n, &m) != 2) {
        fprintf(stderr, "Error reading n and m values\n");
        fclose(fp);
        return 1;
    }

    // Initialize processes
    Process *processes = (Process *)malloc(n * sizeof(Process));
    if (!processes) {
        perror("Memory allocation failed for processes");
        fclose(fp);
        return 1;
    }
    initialize_processes(processes, n, m, fp);
    fclose(fp);

    // Initialize free frame list
    FFEntry *fflist = (FFEntry *)malloc(USER_FRAMES * sizeof(FFEntry));
    if (!fflist) {
        perror("Memory allocation failed for fflist");
        free_processes(processes, n);
        return 1;
    }
    initialize_fflist(fflist);
    int nff = USER_FRAMES;  // Initially all frames are free

    // Allocate essential pages for all processes
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < ESSENTIAL_PAGES; j++) {
            int frame = allocate_frame(fflist, &nff, i, j);
            processes[i].page_table[j].entry = (1 << 15) | frame; // Set valid bit and frame number
            processes[i].page_table[j].history = 0xFFFF;  // Set max history for essential pages
        }
    }

    // Run binary searches for all processes in round-robin fashion
    for (int search = 0; search < m; search++) {
        for (int pid = 0; pid < n; pid++) {
            if (search < processes[pid].num_searches) {
                int key = processes[pid].search_keys[search];
                binary_search(&processes[pid], fflist, &nff, pid, key);
                update_page_history(&processes[pid]);
            }
        }
    }

    // Release all frames and gather statistics
    for (int i = 0; i < n; i++) {
        release_frames(&processes[i], fflist, &nff, i);
    }

    // Print statistics
    print_statistics(processes, n);

    // Clean up
    free(fflist);
    free_processes(processes, n);
    return 0;
}

void initialize_processes(Process *processes, int n, int m, FILE *fp) {
    for (int i = 0; i < n; i++) {
        processes[i].num_searches = m;
        processes[i].array_size = (int *)malloc(sizeof(int));
        processes[i].search_keys = (int *)malloc(m * sizeof(int));
        
        // Read array size and search keys
        if (fscanf(fp, "%d", processes[i].array_size) != 1) {
            fprintf(stderr, "Error reading array size for process %d\n", i);
            exit(1);
        }
        
        for (int j = 0; j < m; j++) {
            if (fscanf(fp, "%d", &processes[i].search_keys[j]) != 1) {
                fprintf(stderr, "Error reading search key %d for process %d\n", j, i);
                exit(1);
            }
        }
        
        // Initialize page table
        for (int j = 0; j < NUM_PAGES; j++) {
            processes[i].page_table[j].entry = 0;  // Invalid initially
            processes[i].page_table[j].history = 0;
        }
        
        // Initialize statistics
        processes[i].accesses = 0;
        processes[i].faults = 0;
        processes[i].replacements = 0;
        for (int j = 0; j < 4; j++) {
            processes[i].attempt_stats[j] = 0;
        }
    }
}

void free_processes(Process *processes, int n) {
    for (int i = 0; i < n; i++) {
        free(processes[i].array_size);
        free(processes[i].search_keys);
    }
    free(processes);
}

void initialize_fflist(FFEntry *fflist) {
    for (int i = 0; i < USER_FRAMES; i++) {
        fflist[i].frame = i;
        fflist[i].last_owner = -1;
        fflist[i].last_page = -1;
    }
}

int allocate_frame(FFEntry *fflist, int *nff, int pid, int page) {
    if (*nff <= 0) {
        fprintf(stderr, "No free frames available\n");
        exit(1);
    }
    
    int frame_index = 0;
    int frame = fflist[frame_index].frame;
    
    // Move the rest of the frames up
    for (int i = frame_index; i < *nff - 1; i++) {
        fflist[i] = fflist[i + 1];
    }
    
    (*nff)--;
    return frame;
}

void release_frames(Process *proc, FFEntry *fflist, int *nff, int pid) {
    for (int page = 0; page < NUM_PAGES; page++) {
        if (proc->page_table[page].entry & (1 << 15)) {  // If valid
            int frame = proc->page_table[page].entry & 0x3FFF;  // Extract frame number
            
            // Add to free frame list
            fflist[*nff].frame = frame;
            fflist[*nff].last_owner = -1;  // Clear ownership
            fflist[*nff].last_page = -1;
            (*nff)++;
            
            // Invalidate page table entry
            proc->page_table[page].entry &= ~(1 << 15);
        }
    }
}

int find_victim_page(Process *proc) {
    unsigned short int min_history = 0xFFFF;
    int victim = -1;
    
    for (int page = ESSENTIAL_PAGES; page < NUM_PAGES; page++) {
        if ((proc->page_table[page].entry & (1 << 15)) && // Valid
            (proc->page_table[page].history < min_history)) {
            min_history = proc->page_table[page].history;
            victim = page;
        }
    }
    
    return victim;
}

int handle_page_fault(Process *proc, FFEntry *fflist, int *nff, int pid, int page) {
    proc->faults++;
    
    // If enough free frames, allocate directly
    if (*nff > NFFMIN) {
        int frame = allocate_frame(fflist, nff, pid, page);
        proc->page_table[page].entry = (1 << 15) | frame;  // Set valid bit and frame number
        proc->page_table[page].history = 0xFFFF;  // Set to maximum (most recently used)
        proc->page_table[page].entry |= (1 << 14);  // Set reference bit
        return 0;  // No replacement needed
    }
    
    // Need page replacement
    proc->replacements++;
    
    // Find victim page
    int victim_page = find_victim_page(proc);
    if (victim_page == -1) {
        fprintf(stderr, "No victim page found for replacement\n");
        exit(1);
    }
    
    int victim_frame = proc->page_table[victim_page].entry & 0x3FFF;
    
    // Attempt 1: Check if the page was replaced earlier
    for (int i = 0; i < *nff; i++) {
        if (fflist[i].last_owner == pid && fflist[i].last_page == page) {
            // Found a frame that previously held this page
            int frame = fflist[i].frame;
            
            // Remove from free list
            for (int j = i; j < *nff - 1; j++) {
                fflist[j] = fflist[j + 1];
            }
            (*nff)--;
            
            // Update page table
            proc->page_table[page].entry = (1 << 15) | frame;  // Set valid bit and frame number
            proc->page_table[page].history = 0xFFFF;  // Set to maximum (most recently used)
            proc->page_table[page].entry |= (1 << 14);  // Set reference bit
            
            // Invalidate the victim page
            proc->page_table[victim_page].entry &= ~(1 << 15);
            
            // Add victim frame to free list
            fflist[*nff].frame = victim_frame;
            fflist[*nff].last_owner = pid;
            fflist[*nff].last_page = victim_page;
            (*nff)++;
            
            proc->attempt_stats[0]++;
            return 1;  // Replacement done with Attempt 1
        }
    }
    
    // Attempt 2: Look for a frame with no owner
    for (int i = 0; i < *nff; i++) {
        if (fflist[i].last_owner == -1) {
            int frame = fflist[i].frame;
            
            // Remove from free list
            for (int j = i; j < *nff - 1; j++) {
                fflist[j] = fflist[j + 1];
            }
            (*nff)--;
            
            // Update page table
            proc->page_table[page].entry = (1 << 15) | frame;  // Set valid bit and frame number
            proc->page_table[page].history = 0xFFFF;  // Set to maximum (most recently used)
            proc->page_table[page].entry |= (1 << 14);  // Set reference bit
            
            // Invalidate the victim page
            proc->page_table[victim_page].entry &= ~(1 << 15);
            
            // Add victim frame to free list
            fflist[*nff].frame = victim_frame;
            fflist[*nff].last_owner = pid;
            fflist[*nff].last_page = victim_page;
            (*nff)++;
            
            proc->attempt_stats[1]++;
            return 2;  // Replacement done with Attempt 2
        }
    }
    
    // Attempt 3: Look for a frame whose last owner was this process
    for (int i = 0; i < *nff; i++) {
        if (fflist[i].last_owner == pid) {
            int frame = fflist[i].frame;
            
            // Remove from free list
            for (int j = i; j < *nff - 1; j++) {
                fflist[j] = fflist[j + 1];
            }
            (*nff)--;
            
            // Update page table
            proc->page_table[page].entry = (1 << 15) | frame;  // Set valid bit and frame number
            proc->page_table[page].history = 0xFFFF;  // Set to maximum (most recently used)
            proc->page_table[page].entry |= (1 << 14);  // Set reference bit
            
            // Invalidate the victim page
            proc->page_table[victim_page].entry &= ~(1 << 15);
            
            // Add victim frame to free list
            fflist[*nff].frame = victim_frame;
            fflist[*nff].last_owner = pid;
            fflist[*nff].last_page = victim_page;
            (*nff)++;
            
            proc->attempt_stats[2]++;
            return 3;  // Replacement done with Attempt 3
        }
    }
    
    // Attempt 4: Pick a random free frame
    if (*nff > 0) {
        int index = rand() % *nff;
        int frame = fflist[index].frame;
        
        // Remove from free list
        for (int j = index; j < *nff - 1; j++) {
            fflist[j] = fflist[j + 1];
        }
        (*nff)--;
        
        // Update page table
        proc->page_table[page].entry = (1 << 15) | frame;  // Set valid bit and frame number
        proc->page_table[page].history = 0xFFFF;  // Set to maximum (most recently used)
        proc->page_table[page].entry |= (1 << 14);  // Set reference bit
        
        // Invalidate the victim page
        proc->page_table[victim_page].entry &= ~(1 << 15);
        
        // Add victim frame to free list
        fflist[*nff].frame = victim_frame;
        fflist[*nff].last_owner = pid;
        fflist[*nff].last_page = victim_page;
        (*nff)++;
        
        proc->attempt_stats[3]++;
        return 4;  // Replacement done with Attempt 4
    }
    
    fprintf(stderr, "Page replacement failed for process %d, page %d\n", pid, page);
    exit(1);
}

int binary_search(Process *proc, FFEntry *fflist, int *nff, int pid, int key) {
    int size = *(proc->array_size);
    int low = 0, high = size - 1;
    
    while (low <= high) {
        int mid = (low + high) / 2;
        
        // Calculate page number for A[mid]
        int page = ESSENTIAL_PAGES + mid / (4096 / sizeof(int));
        
        // Access page
        proc->accesses++;
        
        // Check if page is valid
        if (!(proc->page_table[page].entry & (1 << 15))) {
            // Page fault
            handle_page_fault(proc, fflist, nff, pid, page);
        }
        
        // Set reference bit
        proc->page_table[page].entry |= (1 << 14);
        
        // Simulate comparison with A[mid]
        int value = mid;  // We don't have actual array values, so use mid as a proxy
        
        if (value == key) {
            return mid;  // Found
        }
        
        if (value < key) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    
    return -1;  // Not found
}

void update_page_history(Process *proc) {
    for (int page = 0; page < NUM_PAGES; page++) {
        if (proc->page_table[page].entry & (1 << 15)) {  // If page is valid
            // Get reference bit
            int ref_bit = (proc->page_table[page].entry >> 14) & 1;
            
            // Update history (right shift and insert reference bit at MSB)
            proc->page_table[page].history = (proc->page_table[page].history >> 1) | (ref_bit << 15);
            
            // Reset reference bit
            proc->page_table[page].entry &= ~(1 << 14);
            
            // Ensure essential pages always have maximum history
            if (page < ESSENTIAL_PAGES) {
                proc->page_table[page].history = 0xFFFF;
            }
        }
    }
}

void print_statistics(Process *processes, int n) {
    int total_accesses = 0, total_faults = 0, total_replacements = 0;
    int total_attempts[4] = {0, 0, 0, 0};
    
    printf("+++ Page access summary\n");
    printf("PID Accesses Faults Replacements Attempts\n");
    
    for (int i = 0; i < n; i++) {
        Process *proc = &processes[i];
        
        printf("%-3d %-8d %-3d (%.2f%%) %-3d (%.2f%%) %-3d + %-3d + %-3d + %-3d (%.2f%% + %.2f%% + %.2f%% + %.2f%%)\n",
               i,
               proc->accesses,
               proc->faults,
               (proc->faults * 100.0) / proc->accesses,
               proc->replacements,
               (proc->replacements * 100.0) / proc->accesses,
               proc->attempt_stats[0],
               proc->attempt_stats[1],
               proc->attempt_stats[2],
               proc->attempt_stats[3],
               (proc->replacements > 0) ? (proc->attempt_stats[0] * 100.0) / proc->replacements : 0.0,
               (proc->replacements > 0) ? (proc->attempt_stats[1] * 100.0) / proc->replacements : 0.0,
               (proc->replacements > 0) ? (proc->attempt_stats[2] * 100.0) / proc->replacements : 0.0,
               (proc->replacements > 0) ? (proc->attempt_stats[3] * 100.0) / proc->replacements : 0.0);
        
        total_accesses += proc->accesses;
        total_faults += proc->faults;
        total_replacements += proc->replacements;
        for (int j = 0; j < 4; j++) {
            total_attempts[j] += proc->attempt_stats[j];
        }
    }
    
    // Print total statistics
    printf("Total %-8d %-3d (%.2f%%) %-3d (%.2f%%) %-3d + %-3d + %-3d + %-3d (%.2f%% + %.2f%% + %.2f%% + %.2f%%)\n",
           total_accesses,
           total_faults,
           (total_faults * 100.0) / total_accesses,
           total_replacements,
           (total_replacements * 100.0) / total_accesses,
           total_attempts[0],
           total_attempts[1],
           total_attempts[2],
           total_attempts[3],
           (total_replacements > 0) ? (total_attempts[0] * 100.0) / total_replacements : 0.0,
           (total_replacements > 0) ? (total_attempts[1] * 100.0) / total_replacements : 0.0,
           (total_replacements > 0) ? (total_attempts[2] * 100.0) / total_replacements : 0.0,
           (total_replacements > 0) ? (total_attempts[3] * 100.0) / total_replacements : 0.0);
}