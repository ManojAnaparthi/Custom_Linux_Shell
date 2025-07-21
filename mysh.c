#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

typedef struct {
    pid_t pid;
    char cmdline[256];
    int running;
} Job;

#define MAX_JOBS 64
Job jobs[MAX_JOBS];
int job_count = 0;

void add_job(pid_t pid, const char *cmdline) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].cmdline, cmdline, 255);
        jobs[job_count].cmdline[255] = '\0';
        jobs[job_count].running = 1;
        job_count++;
    }
}

void update_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].running) {
            int status;
            pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
            if (result == jobs[i].pid) {
                jobs[i].running = 0;
            }
        }
    }
}

void print_jobs() {
    update_jobs();
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %s [%s] (PID %d)\n", i+1, jobs[i].cmdline, jobs[i].running ? "Running" : "Done", jobs[i].pid);
    }
}

#define MAX_ARGS 128
#define MAX_CMDS 16

// Find redirection operators and set up files
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

void parse_args(char *line, char **args) {
    int i = 0;
    char *token = strtok(line, " ");
    while (token && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;
}

int is_builtin(char *cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "jobs") == 0;
}

int handle_builtin(char **args) {
    if (strcmp(args[0], "exit") == 0) {
        return 1;
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
    }
    return 0;
}

void execute_pipeline(char *cmdline, int background) {
    char *commands[MAX_CMDS];
    int num_cmds = 0;

    char *cmd = strtok(cmdline, "|");
    while (cmd && num_cmds < MAX_CMDS - 1) {
        while (*cmd == ' ') cmd++; // trim leading spaces
        commands[num_cmds++] = cmd;
        cmd = strtok(NULL, "|");
    }

    int pipes[MAX_CMDS - 1][2];
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        char *args[MAX_ARGS];
        parse_args(commands[i], args);

        pid_t pid = fork();
        if (pid == 0) {
            // Set up piping
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < num_cmds - 1) dup2(pipes[i][1], STDOUT_FILENO);

            // Handle I/O redirection only for first and last command
            if (i == 0 || i == num_cmds - 1) {
                handle_redirection(args);
            }

            // Close all pipe fds
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(args[0], args);
            perror("execvp");
            exit(1);
        }
    }

    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (!background) {
        for (int i = 0; i < num_cmds; i++) wait(NULL);
    } else {
        printf("[bg] pipeline running in background\n");
    }
}

void process_input(char *line) {
    int background = 0;

    // Check for trailing &
    char *amp = strrchr(line, '&');
    if (amp && *(amp + 1) == '\0') {
        background = 1;
        *amp = '\0'; // truncate the & from input
    }

    // Remove newline or trailing space
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\n'))
        line[--len] = '\0';

    // If it’s a pipeline
    if (strchr(line, '|')) {
        execute_pipeline(line, background);
        return;
    }

    // Normal command
    char *args[MAX_ARGS];
    parse_args(line, args);
    if (args[0] == NULL) return;

    if (is_builtin(args[0])) {
        if (handle_builtin(args)) exit(0);
        return;
    }

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
}

void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void sigint_handler(int sig) {
    printf("\nType 'exit' to quit.\nmysh> ");
    fflush(stdout);
}

void sigtstp_handler(int sig) {
    printf("\nStopped (SIGTSTP ignored)\nmysh> ");
    fflush(stdout);
}

int main() {
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    char *line = NULL;
    size_t len = 0;

    while (1) {
        printf("mysh> ");
        fflush(stdout);

        ssize_t nread = getline(&line, &len, stdin);
        if (nread == -1) break;

        if (line[nread - 1] == '\n') line[nread - 1] = '\0';
        if (strlen(line) == 0) continue;

        process_input(line);
    }

    free(line);
    printf("Exiting shell.\n");
    return 0;
}
