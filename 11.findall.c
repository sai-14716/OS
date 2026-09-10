#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#define MAX_PATH 4096
#define MAX_USERS 1000

// Structure to store UID to login ID mapping
typedef struct {
    uid_t uid;
    char loginid[256];
} UserMap;

UserMap users[MAX_USERS];
int user_count = 0;

// Function to load user info from /etc/passwd
void load_user_info() {
    FILE *passwd = fopen("/etc/passwd", "r");
    if (passwd == NULL) {
        perror("Error opening /etc/passwd");
        exit(EXIT_FAILURE);
    }

    char line[1024];
    while (fgets(line, sizeof(line), passwd) && user_count < MAX_USERS) {
        char *username = strtok(line, ":");
        if (username == NULL) continue;
        
        char *x = strtok(NULL, ":");  // Skip password field
        if (x == NULL) continue;
        
        char *uid_str = strtok(NULL, ":");
        if (uid_str == NULL) continue;
        
        uid_t uid = atoi(uid_str);
        
        users[user_count].uid = uid;
        strncpy(users[user_count].loginid, username, sizeof(users[user_count].loginid) - 1);
        users[user_count].loginid[sizeof(users[user_count].loginid) - 1] = '\0';
        
        user_count++;
    }
    
    fclose(passwd);
}

// Function to find user login ID from UID
char* get_login_id(uid_t uid) {
    // First check our cache
    for (int i = 0; i < user_count; i++) {
        if (users[i].uid == uid) {
            return users[i].loginid;
        }
    }
    
    // If not found in cache, try getpwuid
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL) {
        // Add to cache if there's room
        if (user_count < MAX_USERS) {
            users[user_count].uid = uid;
            strncpy(users[user_count].loginid, pw->pw_name, sizeof(users[user_count].loginid) - 1);
            users[user_count].loginid[sizeof(users[user_count].loginid) - 1] = '\0';
            user_count++;
        }
        return pw->pw_name;
    }
    
    // If all else fails
    static char unknown[32];
    snprintf(unknown, sizeof(unknown), "%u", uid);
    return unknown;
}

// Function to check if a file has the specified extension
int has_extension(const char *filename, const char *ext) {
    const char *dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        return 0;
    }
    return strcmp(dot + 1, ext) == 0;
}

// Function to recursively search directory
void search_directory(const char *dir_path, const char *ext, int *file_count) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        fprintf(stderr, "Error opening directory %s: ", dir_path);
        perror("");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        
        struct stat file_stat;
        if (lstat(path, &file_stat) == -1) {
            fprintf(stderr, "Error getting file stats for %s: ", path);
            perror("");
            continue;
        }
        
        if (S_ISREG(file_stat.st_mode) && has_extension(entry->d_name, ext)) {
            // Regular file with matching extension
            (*file_count)++;
            printf("%-3d : %-6s %-8ld %s\n", 
                   *file_count, 
                   get_login_id(file_stat.st_uid), 
                   (long)file_stat.st_size, 
                   path);
        } else if (S_ISDIR(file_stat.st_mode)) {
            // Directory - recurse into it
            search_directory(path, ext, file_count);
        }
    }
    
    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s directory_name extension\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    const char *dir_name = argv[1];
    const char *extension = argv[2];
    
    // Load user information from /etc/passwd
    load_user_info();
    
    // Print header
    printf("NO : OWNER SIZE NAME\n");
    printf("-- ----- ---- ----\n");
    
    int file_count = 0;
    search_directory(dir_name, extension, &file_count);
    
    printf("+++ %d files match the extension %s\n", file_count, extension);
    
    return EXIT_SUCCESS;
}