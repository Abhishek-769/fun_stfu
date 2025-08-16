#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // For fork(), execvp(), chdir(), getcwd()
#include <sys/wait.h>   // For waitpid()
#include <sys/types.h>  // For pid_t
#include <fcntl.h>      // For open() and file control options

// --- Function Prototypes ---

// Main loop of the shell
void shell_loop(void);

// Read a line of input from the user
char *read_line(void);

// Parse the line into arguments (tokens)
char **parse_line(char *line);

// Execute the parsed command
int execute_command(char **args);

// --- Built-in Shell Command Prototypes ---
int shell_cd(char **args);
int shell_pwd(char **args);
int shell_exit(char **args);

// Array of built-in command names
char *builtin_str[] = {
    "cd",
    "pwd",
    "exit"
};

// Array of corresponding built-in function pointers
int (*builtin_func[]) (char **) = {
    &shell_cd,
    &shell_pwd,
    &shell_exit
};

// Get the number of built-in commands
int num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}


// --- Main Function ---

int main(int argc, char **argv) {
    // Run the command loop.
    shell_loop();

    // Perform any shutdown/cleanup.
    return EXIT_SUCCESS;
}


// --- Function Implementations ---

/**
 * @brief The main loop of the shell. It continuously prompts the user,
 *        reads a command, parses it, and executes it.
 */
void shell_loop(void) {
    char *line;
    char **args;
    int status;

    do {
        printf("minishell> ");
        line = read_line();
        args = parse_line(line);
        status = execute_command(args);

        // Free the memory allocated by read_line and parse_line
        free(line);
        free(args);
    } while (status);
}

#define RL_BUFSIZE 1024
/**
 * @brief Reads a line of input from stdin.
 * @return The line from stdin as a heap-allocated string.
 */
char *read_line(void) {
    int bufsize = RL_BUFSIZE;
    int position = 0;
    char *buffer = malloc(sizeof(char) * bufsize);
    int c;

    if (!buffer) {
        fprintf(stderr, "minishell: allocation error\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Read a character
        c = getchar();

        // If we hit EOF or newline, replace it with a null terminator and return.
        if (c == EOF || c == '\n') {
            buffer[position] = '\0';
            return buffer;
        } else {
            buffer[position] = c;
        }
        position++;

        // If we have exceeded the buffer, reallocate.
        if (position >= bufsize) {
            bufsize += RL_BUFSIZE;
            buffer = realloc(buffer, bufsize);
            if (!buffer) {
                fprintf(stderr, "minishell: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}

#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n\a"
/**
 * @brief Parses a line into a sequence of tokens (arguments).
 * @param line The line to parse.
 * @return A null-terminated array of tokens.
 */
char **parse_line(char *line) {
    int bufsize = TOK_BUFSIZE;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "minishell: allocation error\n");
        exit(EXIT_FAILURE);
    }

    // strtok() splits the string by the given delimiters
    token = strtok(line, TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "minishell: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOK_DELIM);
    }
    // The array of tokens must be NULL-terminated for execvp
    tokens[position] = NULL;
    return tokens;
}

/**
 * @brief Launches a program in a new process.
 * @param args Null-terminated list of arguments (including command).
 * @return Always returns 1, to continue execution.
 */
int launch_proc(char **args) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        // --- Child process ---
        // execvp searches the PATH for the command and replaces the child process image
        if (execvp(args[0], args) == -1) {
            perror("minishell"); // This will print an error like "minishell: ls: No such file or directory"
        }
        // execvp only returns on error, so we must exit the child
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // --- Error forking ---
        perror("minishell");
    } else {
        // --- Parent process ---
        // Wait for the child process to complete
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1; // Signal to continue the shell loop
}


/**
 * @brief Handles I/O redirection (<, >) and pipes (|).
 * @param args The command and its arguments.
 * @return Status code (1 to continue, 0 to terminate).
 */
int execute_command(char **args) {
    int i;
    int pipe_index = -1;
    int in_redirect_index = -1;
    int out_redirect_index = -1;

    if (args[0] == NULL) {
        // An empty command was entered.
        return 1;
    }

    // Check for built-in commands first
    for (i = 0; i < num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    // Scan for pipe and redirection symbols
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_index = i;
            break; // Found a pipe, stop scanning for more pipes (this shell handles one pipe)
        }
        if (strcmp(args[i], "<") == 0) {
            in_redirect_index = i;
        }
        if (strcmp(args[i], ">") == 0) {
            out_redirect_index = i;
        }
    }

    // --- Pipe Handling ---
    if (pipe_index != -1) {
        args[pipe_index] = NULL; // Split the command into two parts at the pipe
        char **cmd1 = args;
        char **cmd2 = &args[pipe_index + 1];

        int pipefd[2];
        pid_t p1, p2;

        if (pipe(pipefd) < 0) {
            perror("minishell: pipe error");
            return 1;
        }

        p1 = fork();
        if (p1 < 0) {
            perror("minishell: fork error");
            return 1;
        }

        if (p1 == 0) { // Child 1 (executes cmd1)
            // It only needs to write to the pipe
            close(pipefd[0]); // Close the read end
            dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to the pipe's write end
            close(pipefd[1]); // Close the original write end

            if (execvp(cmd1[0], cmd1) < 0) {
                perror("minishell: command not found");
                exit(EXIT_FAILURE);
            }
        }

        p2 = fork();
        if (p2 < 0) {
            perror("minishell: fork error");
            return 1;
        }

        if (p2 == 0) { // Child 2 (executes cmd2)
            // It only needs to read from the pipe
            close(pipefd[1]); // Close the write end
            dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to the pipe's read end
            close(pipefd[0]); // Close the original read end

            if (execvp(cmd2[0], cmd2) < 0) {
                perror("minishell: command not found");
                exit(EXIT_FAILURE);
            }
        }

        // Parent process
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(p1, NULL, 0);
        waitpid(p2, NULL, 0);
        return 1;
    }

    // --- I/O Redirection Handling ---
    if (in_redirect_index != -1 || out_redirect_index != -1) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("minishell: fork error");
            return 1;
        }
        if (pid == 0) { // Child process
            if (in_redirect_index != -1) {
                int in_fd = open(args[in_redirect_index + 1], O_RDONLY);
                if (in_fd < 0) {
                    perror("minishell: could not open input file");
                    exit(EXIT_FAILURE);
                }
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
                args[in_redirect_index] = NULL; // Remove redirection from args
            }
            if (out_redirect_index != -1) {
                // Open file for writing, create if doesn't exist, truncate if it does.
                // Permissions 0644: owner can read/write, group/others can only read.
                int out_fd = open(args[out_redirect_index + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                 if (out_fd < 0) {
                    perror("minishell: could not open output file");
                    exit(EXIT_FAILURE);
                }
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
                args[out_redirect_index] = NULL; // Remove redirection from args
            }
            if (execvp(args[0], args) == -1) {
                perror("minishell");
                exit(EXIT_FAILURE);
            }
        } else { // Parent process
            waitpid(pid, NULL, 0);
        }
        return 1;
    }


    // If no pipes or redirection, launch a simple process
    return launch_proc(args);
}


// --- Built-in Command Implementations ---

/**
   @brief Built-in command: change directory.
   @param args List of args. args[0] is "cd". args[1] is the directory.
   @return 1 on success, to continue executing.
 */
int shell_cd(char **args) {
    if (args[1] == NULL) {
        // If no argument, go to HOME directory
        const char *home_dir = getenv("HOME");
        if (home_dir == NULL) {
             fprintf(stderr, "minishell: cd: HOME not set\n");
        } else {
            if (chdir(home_dir) != 0) {
                perror("minishell");
            }
        }
    } else {
        if (chdir(args[1]) != 0) {
            perror("minishell");
        }
    }
    return 1;
}

/**
   @brief Built-in command: print working directory.
   @param args List of args. Not examined.
   @return 1, to continue executing.
 */
int shell_pwd(char **args) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("minishell: pwd error");
    }
    return 1;
}


/**
   @brief Built-in command: exit.
   @param args List of args. Not examined.
   @return 0, to terminate execution.
 */
int shell_exit(char **args) {
    return 0; // Returning 0 terminates the main loop
}

