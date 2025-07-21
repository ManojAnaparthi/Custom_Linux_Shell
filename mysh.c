// Simple custom Linux shell implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

// Structure to store background job info
typedef struct {
    pid_t pid;                // Process ID
    char cmdline[256];        // Command line string
    int running;              // 1 if running, 0 if done
    int stopped;              // 1 if stopped (SIGTSTP), 0 otherwise
} Job;

#define MAX_JOBS 64           // Maximum number of jobs
Job jobs[MAX_JOBS];           // Job table
int job_count = 0;            // Number of jobs

// Add a new background job to the job table
void add_job(pid_t pid, const char *cmdline) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].cmdline, cmdline, 255);
        jobs[job_count].cmdline[255] = '\0';
        jobs[job_count].running = 1;
        jobs[job_count].stopped = 0;
        job_count++;
    }
}

// Update job table: mark finished jobs, print notification, and remove them
void update_jobs() {
    int write_idx = 0;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].running) {
            int status;
            pid_t result = waitpid(jobs[i].pid, &status, WNOHANG | WUNTRACED);
            if (result == jobs[i].pid) {
                if (WIFSTOPPED(status)) {
                    jobs[i].stopped = 1;
                    printf("[job stopped] %s (PID %d)\n", jobs[i].cmdline, jobs[i].pid);
                } else {
                    jobs[i].running = 0;
                    printf("[job done] %s (PID %d)\n", jobs[i].cmdline, jobs[i].pid);
                }
            }
        }
        // Only keep jobs that are still running or stopped
        if (jobs[i].running || jobs[i].stopped) {
            jobs[write_idx++] = jobs[i];
        }
    }
    job_count = write_idx;
}

// Print all current jobs
void print_jobs() {
    update_jobs();
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %s [%s] (PID %d)\n", i+1, jobs[i].cmdline,
            jobs[i].running ? "Running" : (jobs[i].stopped ? "Stopped" : "Done"), jobs[i].pid);
    }
}

#define MAX_ARGS 128          // Max arguments per command
#define MAX_CMDS 16           // Max commands in a pipeline

// Handle I/O redirection (<, >, >>) for a command
void handle_redirection(char **args) {
    for (int i = 0; args[i]; i++) {
        if (strcmp(args[i], "<") == 0 && args[i+1]) {
            int fd = open(args[i+1], O_RDONLY);
            if (fd < 0) { perror("open <"); exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
            args[i] = NULL;
        } else if (strcmp(args[i], ">") == 0 && args[i+1]) {
            int fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open >"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            args[i] = NULL;
        } else if (strcmp(args[i], ">>") == 0 && args[i+1]) {
            int fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror("open >>"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            args[i] = NULL;
        }
    }
}

// Split a command line into arguments
void parse_args(char *line, char **args) {
    int i = 0;
    char *token = strtok(line, " ");
    while (token && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;
}

// Check if a command is a shell built-in
int is_builtin(char *cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "jobs") == 0 ||
           strcmp(cmd, "fg") == 0 || strcmp(cmd, "bg") == 0 || strcmp(cmd, "kill") == 0 || strcmp(cmd, "stp") == 0;
}

// Handle built-in commands: cd, exit, jobs
int handle_builtin(char **args) {
    if (strcmp(args[0], "exit") == 0) {
        return 1; // Signal to exit shell
    } else if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            char *home = getenv("HOME");
            if (home) chdir(home);
            else fprintf(stderr, "cd: HOME not set\n");
        } else {
            if (chdir(args[1]) != 0) perror("cd");
        }
    } else if (strcmp(args[0], "jobs") == 0) {
        print_jobs();
    } else if (strcmp(args[0], "fg") == 0 && args[1]) {
        int jobnum = atoi(args[1]) - 1;
        if (jobnum >= 0 && jobnum < job_count) {
            Job *job = &jobs[jobnum];
            if (job->running) {
                kill(job->pid, SIGCONT);
                printf("[fg] Job %d brought to foreground\n", jobnum+1);
                waitpid(job->pid, NULL, 0);
                job->running = 0;
            } else {
                printf("Job %d is not running\n", jobnum+1);
            }
        } else {
            printf("Invalid job number\n");
        }
    } else if (strcmp(args[0], "bg") == 0 && args[1]) {
        int jobnum = atoi(args[1]) - 1;
        if (jobnum >= 0 && jobnum < job_count) {
            Job *job = &jobs[jobnum];
            if (job->stopped) {
                kill(job->pid, SIGCONT);
                job->running = 1;
                job->stopped = 0;
                printf("[bg] Job %d resumed in background\n", jobnum+1);
            } else {
                printf("Job %d is not stopped\n", jobnum+1);
            }
        } else {
            printf("Invalid job number\n");
        }
    } else if (strcmp(args[0], "kill") == 0 && args[1]) {
        int jobnum = atoi(args[1]) - 1;
        if (jobnum >= 0 && jobnum < job_count) {
            Job *job = &jobs[jobnum];
            if (job->running) {
                kill(job->pid, SIGKILL);
                printf("[kill] Job %d killed\n", jobnum+1);
            } else {
                printf("Job %d is not running\n", jobnum+1);
            }
        } else {
            printf("Invalid job number\n");
        }
    } else if (strcmp(args[0], "stp") == 0 && args[1]) {
        int jobnum = atoi(args[1]) - 1;
        if (jobnum >= 0 && jobnum < job_count) {
            Job *job = &jobs[jobnum];
            if (job->running) {
                kill(job->pid, SIGTSTP);
                job->running = 0;
                job->stopped = 1;
                printf("[stp] Job %d stopped (SIGTSTP sent)\n", jobnum+1);
            } else {
                printf("Job %d is not running\n", jobnum+1);
            }
        } else {
            printf("Invalid job number\n");
        }
    }
    return 0;
}

// Execute a pipeline of commands (with |)
void execute_pipeline(char *cmdline, int background) {
    char *commands[MAX_CMDS];
    int num_cmds = 0;

    // Split pipeline into individual commands
    char *cmd = strtok(cmdline, "|");
    while (cmd && num_cmds < MAX_CMDS - 1) {
        while (*cmd == ' ') cmd++; // trim leading spaces
        commands[num_cmds++] = cmd;
        cmd = strtok(NULL, "|");
    }

    int pipes[MAX_CMDS - 1][2];
    // Create pipes for inter-process communication
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    // Fork and execute each command in the pipeline
    for (int i = 0; i < num_cmds; i++) {
        char *args[MAX_ARGS];
        parse_args(commands[i], args);

        pid_t pid = fork();
        if (pid == 0) {
            // Set up piping
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < num_cmds - 1) dup2(pipes[i][1], STDOUT_FILENO);

            // Handle I/O redirection for first/last command
            if (i == 0 || i == num_cmds - 1) {
                handle_redirection(args);
            }

            // Close all pipe fds in child
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Safety: only execvp if args[0] is not NULL
            if (args[0] != NULL && strlen(args[0]) > 0) {
                execvp(args[0], args);
                perror("execvp");
            }
            exit(1);
        }
    }

    // Close all pipe fds in parent
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all pipeline children if foreground
    if (!background) {
        for (int i = 0; i < num_cmds; i++) wait(NULL);
    } else {
        printf("[bg] pipeline running in background\n");
    }
}

// Parse and execute a single input line
// Returns 1 if shell should exit, 0 otherwise
int process_input(char *line) {
    int background = 0;

    // Check for trailing & (background job)
    char *amp = strrchr(line, '&');
    if (amp && *(amp + 1) == '\0') {
        background = 1;
        *amp = '\0'; // Remove & from input
    }

    // Remove trailing spaces/newlines
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\n'))
        line[--len] = '\0';

    // If pipeline, execute as pipeline
    if (strchr(line, '|')) {
        execute_pipeline(line, background);
        return 0;
    }

    // Parse arguments for normal command
    char *args[MAX_ARGS];
    parse_args(line, args);
    if (args[0] == NULL) return 0;

    // Handle built-in commands
    if (is_builtin(args[0])) {
        if (handle_builtin(args)) return 1; // signal exit
        return 0;
    }

    // Fork and execute external command
    pid_t pid = fork();
    if (pid == 0) {
        handle_redirection(args);
        execvp(args[0], args);
        perror("execvp");
        exit(1);
    } else {
        if (!background) {
            waitpid(pid, NULL, 0);
        } else {
            add_job(pid, line);
            printf("[bg] PID %d running\n", pid);
        }
    }
    return 0;
}

// SIGCHLD handler: called when child process terminates
void sigchld_handler(int sig) {
    (void)sig;
    update_jobs();
}

// SIGINT handler: called on Ctrl+C
void sigint_handler(int sig) {
    printf("\nType 'exit' to quit.\nmysh> ");
    fflush(stdout);
}

// SIGTSTP handler: called on Ctrl+Z
void sigtstp_handler(int sig) {
    printf("\nNot Stopped (SIGTSTP ignored)\nmysh> ");
    fflush(stdout);
}

// Main shell loop
int main() {
    // Set up signal handlers
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    char *line = NULL;
    size_t len = 0;

    while (1) {
        printf("mysh> ");
        fflush(stdout);

        // Read input line
        ssize_t nread = getline(&line, &len, stdin);
        if (nread == -1) break;

        if (line[nread - 1] == '\n') line[nread - 1] = '\0';
        if (strlen(line) == 0) continue;

        if (process_input(line)) break; // exit if requested
    }

    free(line);
    printf("Exiting shell.\n");
    return 0;
}
