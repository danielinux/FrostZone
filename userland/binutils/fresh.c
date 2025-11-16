/*
  * Fresh v. 0.2
  * Originally derived from Simple C shell:
  * Copyright (c) 2013 Juan Manuel Reyes
  * Copyright (c) 2015 Daniele Lacamera
  * Copyright (c) 2015 brabo
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <ctype.h>
static pid_t GBSH_PID;
static pid_t GBSH_PGID;
static int GBSH_IS_INTERACTIVE;
static struct termios GBSH_TMODES;
extern char **environ;
static const char empty_value[] = "";

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

struct sigaction act_child;
struct sigaction act_int;

int no_reprint_prmpt;

/**
 * SIGNAL HANDLERS
 */
// signal handler for SIGCHLD */
void signalHandler_child(int p);
// signal handler for SIGINT
void signalHandler_int(int p);
int changeDirectory(char *args[]);

#define LIMIT 16    // max number of tokens for a command
#define MAXLINE 256 // max number of characters from user input
static char currentDirectory[128];
static char lastcmd[128] = "";

#define HISTORY_MAX 8
static char history_entries[HISTORY_MAX][MAXLINE];
static int history_len = 0;
static int history_cursor = 0;
static char history_path[32] = "/var/fresh_history";

#define SHELL_PARAM_MAX LIMIT
static char *shell_params[SHELL_PARAM_MAX];
static int shell_paramc = 0;
static char *shell_param0 = NULL;

struct shell_param_snapshot {
    char *values[SHELL_PARAM_MAX];
    int count;
    char *param0;
};

static void update_pwd_env(void)
{
    char cwd[sizeof(currentDirectory)];

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return;

    setenv("PWD", cwd, 1);
}

int puts_r(struct _reent *r, const char *s)
{
    return strlen(s);
}

static int fresh_exec(char *arg0, char **argv);
static void history_init(void);
static void history_add(const char *line);
static void history_reset_cursor(void);
static void history_load(void);
static char *dup_string(const char *s);
static void shell_params_clear(void);
static void shell_params_assign(char *const *argv, int argc);
static void shell_params_capture(struct shell_param_snapshot *snap);
static void shell_params_restore_snapshot(struct shell_param_snapshot *snap);
static void shell_params_snapshot_clear(struct shell_param_snapshot *snap);
static int builtin_source(char **args, int argc);

static char *dup_string(const char *s)
{
    char *out;
    size_t len;
    if (!s)
        return NULL;
    len = strlen(s) + 1;
    out = malloc(len);
    if (out)
        memcpy(out, s, len);
    return out;
}

static void history_set_path(void)
{
    struct stat st;
    const char *preferred = "/tmp/fresh_history";

    if (stat("/var", &st) == 0 && S_ISDIR(st.st_mode))
        preferred = "/var/fresh_history";

    strncpy(history_path, preferred, sizeof(history_path) - 1);
    history_path[sizeof(history_path) - 1] = '\0';
}

static void history_append_file(const char *line)
{
    int fd;
    size_t len;
    if (!line || line[0] == '\0')
        return;
    fd = open(history_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    len = strlen(line);
    if (len > 0)
        write(fd, line, len);
    write(fd, "\n", 1);
    close(fd);
}

static void history_store_line(const char *line, int persist)
{
    char clean[MAXLINE];
    size_t len;

    if (!line)
        return;

    len = strnlen(line, MAXLINE - 1);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        len--;
    if (len == 0)
        return;
    if (len >= MAXLINE)
        len = MAXLINE - 1;
    memcpy(clean, line, len);
    clean[len] = '\0';

    if (history_len == HISTORY_MAX) {
        memmove(history_entries, history_entries + 1,
                sizeof(history_entries[0]) * (HISTORY_MAX - 1));
        history_len = HISTORY_MAX - 1;
    }
    strncpy(history_entries[history_len], clean, MAXLINE - 1);
    history_entries[history_len][MAXLINE - 1] = '\0';
    history_len++;
done:
    strncpy(lastcmd, clean, sizeof(lastcmd) - 1);
    lastcmd[sizeof(lastcmd) - 1] = '\0';
    if (persist)
        history_append_file(clean);
}

static void history_add(const char *line)
{
    history_store_line(line, 1);
    history_reset_cursor();
}

static void history_load(void)
{
    int fd;
    char buf[128];
    char line[MAXLINE];
    size_t cur = 0;
    ssize_t r;

    fd = open(history_path, O_RDONLY);
    if (fd < 0) {
        if (strcmp(history_path, "/var/fresh_history") == 0) {
            strncpy(history_path, "/tmp/fresh_history",
                    sizeof(history_path) - 1);
            history_path[sizeof(history_path) - 1] = '\0';
            fd = open(history_path, O_RDONLY);
        }
    }
    if (fd < 0)
        return;

    memset(line, 0, sizeof(line));
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t idx = 0; idx < r; idx++) {
            char c = buf[idx];
            if (c == '\n') {
                line[cur] = '\0';
                history_store_line(line, 0);
                cur = 0;
            } else if (cur < (MAXLINE - 1)) {
                line[cur++] = c;
            }
        }
    }
    if (cur > 0) {
        line[cur] = '\0';
        history_store_line(line, 0);
    }
    close(fd);
    history_reset_cursor();
}

static void history_init(void)
{
    history_set_path();
    history_load();
}

static void history_reset_cursor(void)
{
    history_cursor = history_len;
}

static void history_replace_input(char *input, int *len, int *pos,
                                  const char *entry)
{
    const char del = 0x08;
    const char space = 0x20;
    int cur = *len;
    while (cur-- > 0) {
        write(STDOUT_FILENO, &del, 1);
        write(STDOUT_FILENO, &space, 1);
        write(STDOUT_FILENO, &del, 1);
    }
    *len = 0;
    *pos = 0;
    if (!entry)
        return;
    if (entry == input) {
        *len = strnlen(input, MAXLINE - 1);
        *pos = *len;
        write(STDOUT_FILENO, input, *len);
        return;
    }
    strncpy(input, entry, MAXLINE - 1);
    input[MAXLINE - 1] = '\0';
    *len = strlen(input);
    *pos = *len;
    write(STDOUT_FILENO, input, *len);
}

static void shell_params_clear(void)
{
    int i;
    if (shell_param0) {
        free(shell_param0);
        shell_param0 = NULL;
    }
    for (i = 0; i < shell_paramc; i++) {
        if (shell_params[i]) {
            free(shell_params[i]);
            shell_params[i] = NULL;
        }
    }
    shell_paramc = 0;
}

static void shell_params_assign(char *const *argv, int argc)
{
    int i;
    if (!argv || argc <= 0)
        return;
    shell_params_clear();
    shell_param0 = dup_string(argv[0]);
    if (!shell_param0)
        shell_param0 = dup_string("fresh");
    for (i = 1; i < argc && (i - 1) < SHELL_PARAM_MAX; i++) {
        if (argv[i])
            shell_params[i - 1] = dup_string(argv[i]);
        else
            shell_params[i - 1] = dup_string("");
    }
    shell_paramc = (argc - 1);
    if (shell_paramc > SHELL_PARAM_MAX)
        shell_paramc = SHELL_PARAM_MAX;
}

static void shell_params_capture(struct shell_param_snapshot *snap)
{
    int i;
    if (!snap)
        return;
    snap->param0 = shell_param0 ? dup_string(shell_param0) : NULL;
    snap->count = shell_paramc;
    for (i = 0; i < SHELL_PARAM_MAX; i++) {
        if (i < shell_paramc && shell_params[i])
            snap->values[i] = dup_string(shell_params[i]);
        else
            snap->values[i] = NULL;
    }
}

static void shell_params_restore_snapshot(struct shell_param_snapshot *snap)
{
    int i;
    if (!snap)
        return;
    shell_params_clear();
    if (snap->param0) {
        shell_param0 = snap->param0;
        snap->param0 = NULL;
    }
    shell_paramc = snap->count;
    if (shell_paramc > SHELL_PARAM_MAX)
        shell_paramc = SHELL_PARAM_MAX;
    for (i = 0; i < shell_paramc; i++) {
        shell_params[i] = snap->values[i];
        snap->values[i] = NULL;
    }
    for (; i < SHELL_PARAM_MAX; i++) {
        if (snap->values[i]) {
            free(snap->values[i]);
            snap->values[i] = NULL;
        }
    }
    snap->count = 0;
}

static void shell_params_snapshot_clear(struct shell_param_snapshot *snap)
{
    int i;
    if (!snap)
        return;
    if (snap->param0) {
        free(snap->param0);
        snap->param0 = NULL;
    }
    for (i = 0; i < SHELL_PARAM_MAX; i++) {
        if (snap->values[i]) {
            free(snap->values[i]);
            snap->values[i] = NULL;
        }
    }
    snap->count = 0;
}

static int builtin_source(char **args, int argc)
{
    struct shell_param_snapshot snap = {};
    char *newargv[SHELL_PARAM_MAX + 1];
    int count = 0;
    int i;
    int ret;

    if (argc < 2) {
        printf("source: missing file operand\r\n");
        setenv("?", "1", 1);
        return -1;
    }

    shell_params_capture(&snap);
    for (i = 1; i < argc && count < (SHELL_PARAM_MAX + 1); i++) {
        newargv[count++] = args[i];
    }
    if (count > 0)
        shell_params_assign(newargv, count);

    ret = fresh_exec(args[1], &args[1]);
    shell_params_restore_snapshot(&snap);
    shell_params_snapshot_clear(&snap);

    return ret;
}

/**
 * Function used to initialize our shell. We used the approach explained in
 * http://www.gnu.org/software/libc/manual/html_node/Initializing-the-Shell.html
 */
void shell_init(char *file)
{
    int stdin_fileno, stdout_fileno, stderr_fileno;

    if (file) {
        close(0);
        close(1);
        close(2);
        do {
            stdin_fileno = open(file, O_RDWR, 0);
        } while (stdin_fileno < 0);

        do {
            stdout_fileno = dup(stdin_fileno);
        } while (stdout_fileno < 0);

        do {
            stderr_fileno = dup(stdin_fileno);
        } while (stderr_fileno < 0);
    }

    // See if we are running interactively
    GBSH_PID = getpid();
    // The shell is interactive if STDIN is the terminal
    GBSH_IS_INTERACTIVE = isatty(STDIN_FILENO);

    //    if (!GBSH_IS_INTERACTIVE) {
    //        printf("Warning: this shell is not a TTY.\r\n");
    //    }
}

/**
 * Method used to print the welcome screen of our shell
 */
void welcomeScreen()
{
    printf("\r\n\t============================================\r\n");
    printf("\t          Frosted shell - aka \"Fresh\"         \r\n");
    printf("\t--------------------------------------------\r\n");
    printf("\t             Licensed under GPL             \r\n");
    printf("\t============================================\r\n");
    printf("\r\n\r\n");
}

/**
 * SIGNAL HANDLERS
 */

/**
 * signal handler for SIGCHLD
 */
static int child_pid = 0;
static int child_status = 0;

void signalHandler_child(int p)
{
    /* Wait for all dead processes.
     * We use a non-blocking call (WNOHANG) to be sure this signal handler will
     * not
     * block if a child was cleaned up in another part of the program. */
    child_pid = waitpid(-1, &child_status, WNOHANG);
}

/**
 *	Displays the prompt for the shell
 */
void shellPrompt()
{
    // We print the prompt in the form "<user>@<host> <cwd> >"
    char prompt[256];
    char hostn[] = "frosted";
    char *cwd = getcwd(currentDirectory, 128);
    if (cwd != NULL) {
        snprintf(prompt, 255, "root@%s %s # ", hostn, cwd);
        write(STDOUT_FILENO, prompt, strlen(prompt));
    }
}

/**
 * Signal handler for SIGINT
 */

volatile static int interrupted = 0;
void signalHandler_int(int p)
{
    // shellPrompt();
    interrupted++;
}

/**
 * Method to change directory
 */
int changeDirectory(char *args[])
{
    const char *target = args[1];

    if (target == NULL || target[0] == '\0') {
        target = getenv("HOME");
        if (target == NULL || target[0] == '\0')
            target = "/";
    }

    if (chdir(target) == -1) {
        printf(" %s: no such directory\r\n", target);
        return -1;
    }

    update_pwd_env();
    return 0;
}

/**
* Method for launching a program. It can be run in the background
* or in the foreground
*/

enum x_type {
    X_ERR = -1,
    X_SH = 0,
    X_bFLT,
    X_ELF,
    X_PY,
    X_UNK = 0x99,
};

static enum x_type get_x_type(char *arg)
{
    int fd = open(arg, O_RDONLY);
    char hdr[30] = {};
    if (fd < 0) {
        return X_ERR;
    }
    if (read(fd, hdr, 30) <= 0) {
        close(fd);
        return X_ERR;
    }
    close(fd);

    hdr[29] = '\0';

    if (strncmp(hdr, "bFLT", 4) == 0)
        return X_bFLT;
    if (hdr[0] == '#' && hdr[1] == '!') {
        if (strstr(hdr, "python"))
            return X_PY;
        if (strstr(hdr, "sh"))
            return X_SH;
    }
    if (strncmp(hdr + 1, "ELF", 3) == 0)
        return X_ELF;

    return X_UNK;
}

static int launchProg(char **args, int background)
{
    int err = -1;
    struct stat st;
    char bin_arg0[60] = "/bin/";
    char interpreter[30] = "/bin/fresh";
    enum x_type xt;
    int pid;

    /* Try to look for path */
    if (!strchr(args[0], '/') || (stat(args[0], &st) < 0))
        strcpy(bin_arg0 + 5, args[0]);
    else
        strcpy(bin_arg0, args[0]);

    /* Find in path: executable command */
    if (stat(bin_arg0, &st) < 0) {
        printf("stat: Command not found.\r\n");
        return 1;
    }

    child_pid = 0;
    pid = vfork();

    if (pid == -1) {
        printf("Child process could not be created\r\n");
        return 1;
    }
    if (pid == 0) {
        if (background != 0)
            setsid();

        /* Test file type */
        xt = get_x_type(bin_arg0);
        switch (xt) {
        case X_bFLT:
            // If we launch non-existing commands we end the process
            execvp(bin_arg0, args);
            exit(255);

        case X_PY:
            strcpy(interpreter,"/bin/python");
            /* fall through */
        case X_SH: {
            char *aux[LIMIT] = {NULL};
            int i;
            aux[0] = interpreter;
            aux[1] = bin_arg0;

            i = 1;
            while (args[i] != NULL) {
                aux[i + 1] = args[i++];
            }
            err = execvp(interpreter, aux);
            exit(err);
        }

        case X_ELF:
            printf("Unable to execute ELF: format not (yet) supported.\r\n");
            exit(254);

        case X_UNK:
            printf("Cannot execute: unknown file type.\r\n");
            exit(254);

        case X_ERR:
            printf("Command not found");
            exit(255);
        }
        exit(255); /* Never reached */
    }

    // The following will be executed by the parent

    // If the process is not requested to be in background, we wait for
    // the child to finish.
    if (background == 0) {
        char exit_status_str[16];

        while (child_pid != pid) {
            sleep(120); /* Will be interrupted by sigchld. */
        }
        sprintf(exit_status_str, "%d", child_status);
        setenv("?", exit_status_str, 1);
        return WEXITSTATUS(child_status);
    } else {
        // In order to create a background process, the current process
        // should just skip the call to wait. The SIGCHILD handler
        // signalHandler_child will take care of the returning values
        // of the childs.
        printf("Process created with PID: %d\r\n", pid);
        return 0;
    }
}

/**
* Method used to manage I/O redirection
*/
void fileIO(char *args[], char *inputFile, char *outputFile, int option)
{
    int err = -1;
    int fileDescriptor; // between 0 and 19, describing the output or input file
    int pid;

    if ((pid = vfork()) == -1) {
        printf("Child process could not be created\r\n");
        return;
    }
    if (pid == 0) {
        // Option 0: output redirection
        if (option == 0) {
            uint32_t flags;
            // We open (create) the file truncating it at 0, for write only
            flags = O_CREAT;
            flags |= O_TRUNC | O_WRONLY;
            fileDescriptor = open(outputFile, flags, 0600);
            // We replace de standard output with the appropriate file
            dup2(fileDescriptor, STDOUT_FILENO);
            close(fileDescriptor);
            // Option 1: input and output redirection
        } else if (option == 1) {
            // We open file for read only (it's STDIN)
            fileDescriptor = open(inputFile, O_RDONLY, 0600);
            // We replace de standard input with the appropriate file
            dup2(fileDescriptor, STDIN_FILENO);
            close(fileDescriptor);
            // Same as before for the output file
            fileDescriptor =
                open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
            dup2(fileDescriptor, STDOUT_FILENO);
            close(fileDescriptor);
        }

        // setenv("parent",getcwd(currentDirectory, 128),1);

        execvp(args[0], args);
        exit(1);
    }

    while (child_pid != pid) {
        sleep(60); /* Will be interrupted by sigchld */
    }
}

/**
 * Run a pipeline `cmd1 | cmd2 | ...`
 */
static int pipeHandler(char *args[])
{
    char **commands[LIMIT];
    pid_t pids[LIMIT];
    int cmd_count = 0;
    char **start = args;
    int last_status = 0;
    int i;
    int prev_read = -1;

    if (!args || !args[0])
        return -1;

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            args[i] = NULL;
            if (!start || !*start) {
                printf("fresh: invalid use of pipe\n");
                return -1;
            }
            if (cmd_count >= LIMIT) {
                printf("fresh: too many piped commands\n");
                return -1;
            }
            commands[cmd_count++] = start;
            start = &args[i + 1];
        }
    }
    if (start && *start) {
        if (cmd_count >= LIMIT) {
            printf("fresh: too many piped commands\n");
            return -1;
        }
        commands[cmd_count++] = start;
    }

    if (cmd_count < 2) {
        printf("fresh: pipe requires at least two commands\n");
        return -1;
    }

    for (i = 0; i < cmd_count; i++) {
        int pipefd[2];
        pid_t pid;
        pipefd[0] = -1;
        pipefd[1] = -1;
        if (i < cmd_count - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                if (prev_read != -1)
                    close(prev_read);
                return -1;
            }
        }

        pid = fork();
        if (pid < 0) {
            perror("fork");
            if (pipefd[0] != -1)
                close(pipefd[0]);
            if (pipefd[1] != -1)
                close(pipefd[1]);
            if (prev_read != -1)
                close(prev_read);
            return -1;
        }
        if (pid == 0) {
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
            }
            if (pipefd[1] != -1) {
                dup2(pipefd[1], STDOUT_FILENO);
            }
            if (pipefd[0] != -1)
                close(pipefd[0]);
            if (pipefd[1] != -1)
                close(pipefd[1]);
            if (prev_read != -1)
                close(prev_read);

            execvp(commands[i][0], commands[i]);
            perror(commands[i][0]);
            _exit(127);
        }

        pids[i] = pid;
        if (pipefd[1] != -1)
            close(pipefd[1]);
        if (prev_read != -1)
            close(prev_read);
        prev_read = pipefd[0];
    }

    if (prev_read != -1)
        close(prev_read);

    for (i = 0; i < cmd_count; i++) {
        int status = 0;
        pid_t w;
        do {
            w = waitpid(pids[i], &status, 0);
        } while (w < 0 && errno == EINTR);
        if (w == pids[cmd_count - 1])
            last_status = status;
    }

    if (WIFEXITED(last_status))
        return WEXITSTATUS(last_status);
    if (WIFSIGNALED(last_status))
        return 128 + WTERMSIG(last_status);
    return last_status;
}

/**
* Method used to handle the commands entered via the standard input
*/
int commandHandler(char *args[], int argc)
{
    int i = 0;
    int j = 0;

    int fileDescriptor;
    int standardOut;

    int aux = -1;
    int background = 0;

    char *args_aux[LIMIT] = {NULL};
    char *orig_args[LIMIT] = {NULL};

    for (i = 0; i < argc && i < LIMIT; i++)
        orig_args[i] = args[i];
    i = 0;

    // We look for the special characters and separate the command itself
    // in a new array for the arguments
    while (j < argc) {
        if ((strcmp(args[j], ">") == 0) || (strcmp(args[j], "<") == 0) ||
            (strcmp(args[j], "&") == 0)) {
            break;
        }
        if (!strncmp(args[j], "$", 1)) {
            const char *name = args[j] + 1;
            const char *value = NULL;
            char tmp[16];

            if (name[0] == '\0') {
                value = "$";
            } else if (name[0] == '$' && name[1] == '\0') {
                snprintf(tmp, sizeof(tmp), "%d", getpid());
                value = tmp;
            } else if (name[0] == '?' && name[1] == '\0') {
                const char *env = getenv("?");
                value = env ? env : "0";
            } else if (name[0] == '#' && name[1] == '\0') {
                if (shell_paramc > 0)
                    snprintf(tmp, sizeof(tmp), "%d", shell_paramc);
                else
                    snprintf(tmp, sizeof(tmp), "%d", argc - 1);
                value = tmp;
            } else if (isdigit((unsigned char)name[0])) {
                char *endptr = NULL;
                long idx = strtol(name, &endptr, 10);
                if (endptr && *endptr == '\0') {
                    if ((shell_paramc > 0) || shell_param0) {
                        if (idx == 0)
                            value = shell_param0 ? shell_param0 : "fresh";
                        else if (idx > 0 && idx <= shell_paramc &&
                                 shell_params[idx - 1])
                            value = shell_params[idx - 1];
                        else
                            value = empty_value;
                    } else if (idx >= 0 && idx < argc && orig_args[idx]) {
                        value = orig_args[idx];
                    } else {
                        value = empty_value;
                    }
                } else {
                    value = empty_value;
                }
            } else if (isalpha((unsigned char)name[0]) || name[0] == '_') {
                const char *env = getenv(name);
                value = env ? env : empty_value;
            } else {
                value = empty_value;
            }

            if (!value)
                value = empty_value;

            if (*value == '\0') {
                args[j] = (char *)empty_value;
            } else {
                char *ptr = malloc(strlen(value) + 1);
                if (ptr) {
                    strcpy(ptr, value);
                    args[j] = ptr;
                }
            }
        }
        args_aux[j] = args[j];
        j++;
    }

    /* Add variables to our environment */
    if (argc == 1) {
        char *command = args[0];
        char *token = strchr(command, '=');
        if (token) {
            size_t keylen = token - command;
            char *key = malloc((keylen + 1) * sizeof (char));
            strncpy(key, command, keylen);
            *(key + keylen) = 0x0;
            token++;
            if (setenv(key, token, 1)) {
                return -errno;
            }
            return 0;
        }
    }

    // 'exit' command quits the shell
    if (strcmp(args[0], "exit") == 0)
        exit(0);
    // 'pwd' command prints the current directory
    else if (strcmp(args[0], "pwd") == 0) {
        if (args[j] != NULL) {
            // If we want file output
            if ((strcmp(args[j], ">") == 0) && (args[j + 1] != NULL)) {
                fileDescriptor =
                    open(args[j + 1], O_CREAT | O_TRUNC | O_WRONLY, 0600);
                // We replace de standard output with the appropriate file
                standardOut =
                    dup(STDOUT_FILENO); // first we make a copy of stdout
                                        // because we'll want it back
                dup2(fileDescriptor, STDOUT_FILENO);
                close(fileDescriptor);
                printf("%s\r\n", getcwd(currentDirectory, 128));
                dup2(standardOut, STDOUT_FILENO);
            }
        } else {
            printf("%s\r\n", getcwd(currentDirectory, 128));
        }
    }
    // 'cd' command to change directory
    else if (strcmp(args[0], "cd") == 0) {
        if (changeDirectory(args) < 0) {
            setenv("?", "1", 1);
        }
    }

    // 'setenv' command to set environment variables
    else if (strcmp(args[0], "setenv") == 0) {
        setenv(args[1], args[2], 1);
    } else if (strcmp(args[0], "export") == 0) {
        int rv = 0;
        if (argc < 2) {
            printf("export: missing arguments\r\n");
            rv = -1;
        } else {
            for (i = 1; i < argc && rv == 0; i++) {
                char *eq = strchr(args[i], '=');
                const char *key = args[i];
                const char *val = NULL;
                if (eq) {
                    *eq = '\0';
                    val = eq + 1;
                } else {
                    if (i + 1 >= argc) {
                        printf("export: missing value for %s\r\n", args[i]);
                        rv = -1;
                        break;
                    }
                    val = args[++i];
                }
                if (setenv(key, val, 1) != 0)
                    rv = -errno;
                if (eq)
                    *eq = '=';
            }
        }
        setenv("?", rv == 0 ? "0" : "1", 1);
        return rv;
    } else if (strcmp(args[0], "getenv") == 0) {
        if (argc > 1) {
            char *value;
            value = getenv(args[1]);
            if (value)
                printf("%s\r\n", value);
            else
                printf("getenv: variable '%s' is not set\r\n", args[1]);
        } else {
            extern char **environ;
            for (char **env = environ; env && *env; env++)
                printf("%s\r\n", *env);
        }
    }
    // 'unsetenv' command to undefine environment variables
    else if (strcmp(args[0], "unsetenv") == 0 || strcmp(args[0], "unset") == 0) {
        if (argc < 2) {
            printf("unset: missing variable name\r\n");
            setenv("?", "1", 1);
            return -1;
        }
        for (i = 1; i < argc; i++)
            unsetenv(args[i]);
        setenv("?", "0", 1);
    } else if (strcmp(args[0], "source") == 0 || strcmp(args[0], ".") == 0) {
        int src_ret = builtin_source(args, argc);
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", src_ret);
        setenv("?", buf, 1);
        return src_ret;
    } else if (strcmp(args[0], "shift") == 0) {
        long shift_count = 1;
        char *endptr = NULL;
        if (argc > 1) {
            shift_count = strtol(args[1], &endptr, 10);
            if ((endptr && *endptr != '\0') || shift_count < 0) {
                printf("shift: invalid count\r\n");
                setenv("?", "1", 1);
                return -1;
            }
        }
        if (shift_count > shell_paramc) {
            printf("shift: not enough positional parameters\r\n");
            setenv("?", "1", 1);
            return -1;
        }
        for (i = 0; i < shift_count; i++) {
            if (shell_params[i]) {
                free(shell_params[i]);
                shell_params[i] = NULL;
            }
        }
        for (i = shift_count; i < shell_paramc; i++)
            shell_params[i - shift_count] = shell_params[i];
        for (i = shell_paramc - shift_count; i < shell_paramc; i++) {
            if (i >= 0)
                shell_params[i] = NULL;
        }
        shell_paramc -= shift_count;
        setenv("?", "0", 1);
        return 0;
    } else {
        // If none of the preceding commands were used, we invoke the
        // specified program. We have to detect if I/O redirection,
        // piped execution or background execution were solicited
        while (args[i] != NULL && background == 0) {
            // If background execution was solicited (last argument '&')
            // we exit the loop
            if (strcmp(args[i], "&") == 0) {
                background = 1;
                // If '|' is detected, piping was solicited, and we call
                // the appropriate method that will handle the different
                // executions
            } else if (strcmp(args[i], "|") == 0) {
                int pipe_status = pipeHandler(args);
                char buf[12];
                snprintf(buf, sizeof(buf), "%d", pipe_status);
                setenv("?", buf, 1);
                return pipe_status;
                // If '<' is detected, we have Input and Output redirection.
                // First we check if the structure given is the correct one,
                // and if that is the case we call the appropriate method
            } else if (strcmp(args[i], "<") == 0) {
                aux = i + 1;
                if (args[aux] == NULL || args[aux + 1] == NULL ||
                    args[aux + 2] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                } else {
                    if (strcmp(args[aux + 1], ">") != 0) {
                        printf("Usage: Expected '>' and found %s\r\n",
                               args[aux + 1]);
                        return -2;
                    }
                }
                fileIO(args_aux, args[i + 1], args[i + 3], 1);
                return 1;
            }
            // If '>' is detected, we have output redirection.
            // First we check if the structure given is the correct one,
            // and if that is the case we call the appropriate method
            else if (strcmp(args[i], ">") == 0) {
                if (args[i + 1] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                }
                fileIO(args_aux, NULL, args[i + 1], 0);
                return 1;
            }
            i++;
        }
        // We launch the program with our method, indicating if we
        // want background execution or not
        return launchProg(args_aux, background);
    }
}

void pointer_shift(int *a, int s, int n)
{
    int i;
    for (i = n; i > s - 1; i--) {
        *(a + i + 1) = *(a + i);
    }
}

char *readline_tty(char *input, int size)
{
    while (2 > 1) {
        int len = 0, pos = 0;
        int out = STDOUT_FILENO;
        char got[5];
        int i, j;
        history_reset_cursor();

        while (len < size) {
            const char del = 0x08;
            const char space = 0x20;
            int ret = read(STDIN_FILENO, got, 4);
            if (ret < 0) {
                printf("read: %s\r\n", strerror(errno));
                shellPrompt();
                continue;
            }

            /* arrows */
            if ((ret == 3) && (got[0] == 0x1b)) {
                char dir = got[2];
                if (dir == 'A') {
                    if (history_len == 0)
                        continue;
                    if (history_cursor > 0)
                        history_cursor--;
                    history_replace_input(input, &len, &pos,
                                          history_entries[history_cursor]);
                    continue;
                } else if (dir == 'B') {
                    if (history_len == 0)
                        continue;
                    if (history_cursor < history_len)
                        history_cursor++;
                    if (history_cursor == history_len)
                        history_replace_input(input, &len, &pos, NULL);
                    else
                        history_replace_input(input, &len, &pos,
                                              history_entries[history_cursor]);
                    continue;
                } else if (dir == 'C') {
                    if (pos < len) {
                        printf("%c", input[pos++]);
                        fflush(stdout);
                    }
                } else if (dir == 'D') {
                    write(STDOUT_FILENO, &del, 1);
                    pos--;
                    continue;
                }
            }

            if (ret > 3) {
                if ((got[0] == 0x1B) && (got[2] == 0x33) && (got[3] == 0x7E)) {
                    if (pos < len) {
                        // write(STDOUT_FILENO, &del, 1);
                        // printf( " ");
                        // write(STDOUT_FILENO, &del, 1);
                        pos--;
                        len--;
                        if (pos < len) {
                            for (i = pos + 1; i < len; i++) {
                                input[i] = input[i + 1];
                                write(STDOUT_FILENO, &input[i], 1);
                            }
                            write(STDOUT_FILENO, &space, 1);
                            i = len - pos;
                            while (i > 0) {
                                write(STDOUT_FILENO, &del, 1);
                                i--;
                            }

                        } else {
                            input[pos] = 0x00;
                            pos--;
                            len--;
                        }

                        continue;
                    }
                }
                continue;
            }
            if ((ret > 0) && (got[0] >= 0x20) && (got[0] <= 0x7e)) {
                for (i = 0; i < ret; i++) {
                    /* Echo to terminal */
                    if (got[i] >= 0x20 && got[i] <= 0x7e)
                        write(STDOUT_FILENO, &got[i], 1);
                    if (pos < len) {
                        for (j = len + 1; j > pos; j--) {
                            input[j] = input[j - 1];
                        }
                        input[pos] = got[i];
                        for (j = pos + 1; j < len + 1; j++) {
                            write(STDOUT_FILENO, &input[j], 1);
                        }
                        write(STDOUT_FILENO, &input[i], 1);
                        j = len - pos + 1;
                        while (j > 0) {
                            write(STDOUT_FILENO, &del, 1);
                            j--;
                        }
                    } else {
                        input[pos] = got[i];
                    }

                    len++;
                    pos++;
                }
            }

            if ((got[0] == 0x0C)) { /* CTRL-L */
                printf("\033[2J\033[H");
                fflush(stdout);
                shellPrompt();
                for (i = 0; i < len; i++) {
                    write(STDOUT_FILENO, &input[i], 1);
                }
            }

            if ((got[0] == 0x0D) || (got[0] == 0x0A)) {
                input[len] = 0x0A;
                input[len + 1] = '\0';
                printf("\r\n");
                fflush(stdout);
                if (len > 0) {
                    char hist[MAXLINE];
                    if (len >= MAXLINE)
                        len = MAXLINE - 1;
                    memcpy(hist, input, len);
                    hist[len] = '\0';
                    history_add(hist);
                }
                return input; /* CR (\r\n) */
            }

            if ((got[0] == 0x4)) {
                printf("\r\n");
                fflush(stdout);
                len = 0;
                pos = 0;
                break;
            }

            /* tab */
            if ((got[0] == 0x09)) {
                input[len] = 0;
                printf("\r\n");
                printf("Built-in commands: \r\n");
                printf("\t cd getenv pwd setenv");
                printf("\r\n");
                shellPrompt();
                printf("%s", input);
                fflush(stdout);
                continue;
            }

            /* backspace */
            if ((got[0] == 0x7F) || (got[0] == 0x08)) {
                if (pos > 0) {
                    write(STDOUT_FILENO, &del, 1);
                    write(STDOUT_FILENO, &space, 1);
                    write(STDOUT_FILENO, &del, 1);
                    pos--;
                    len--;
                    if (pos < len) {
                        for (i = pos; i < len; i++) {
                            input[i] = input[i + 1];
                            write(STDOUT_FILENO, &input[i], 1);
                        }
                        write(STDOUT_FILENO, &space, 1);
                        i = len - pos + 1;
                        while (i > 0) {
                            write(STDOUT_FILENO, &del, 1);
                            i--;
                        }

                    } else {
                        input[pos] = 0x00;
                    }

                    continue;
                }
            }
        }
        printf("\r\n");
        fflush(stdout);
        if (len < 0)
            return NULL;

        input[len + 1] = '\0';
    }
    return input;
}

char *readline_notty(char *input, int len)
{
    int ret = read(STDIN_FILENO, input, len - 1);
    if (ret > 0) {
        input[ret - 1] = 0x0A;
        input[ret] = 0x00;
        return input;
    }
    return NULL;
}

static char *readline(char *input, int len)
{
    if (GBSH_IS_INTERACTIVE)
        return readline_tty(input, len);
    else
        return readline_notty(input, len);
}

static int parseLine(char *line)
{
    char *tokens[LIMIT] = {};
    char *end = NULL;
    char *saveptr;
    int numTokens;

    /* Find inline comments */
    end = strchr(line, '#');
    if (end)
        *end = (char)0;

    /* Find special symbols */
    if ((tokens[0] = strtok_r(line, " \r\n\t", &saveptr)) == NULL)
        return 0;

    if (tokens[0][0] == '#')
        return 0;

    for (numTokens = 1; numTokens < LIMIT; numTokens++) {
        tokens[numTokens] = strtok_r(NULL, " \r\n\t", &saveptr);
        if (tokens[numTokens] == NULL)
            break;

        /* Look for comment '#' */
        end = strchr(tokens[numTokens], '#');
        if (end) {
            *end = (char)0;
            if (strlen(tokens[numTokens]) == 0)
                numTokens--;
            break;
        }
    }
    if (numTokens > 0)
        return commandHandler(tokens, numTokens);
    else
        return 0;
}

/* Fresh exec */

static int fresh_exec(char *arg0, char **argv)
{
    struct stat st;
    char *script_mem;
    int fd;
    int r, count = 0;
    char *line, *eol;

    if (stat(arg0, &st) < 0) {
        fprintf(stderr, "Error: cannot stat script %s\n", arg0);
        return 254;
    }
    script_mem = malloc(st.st_size + 1);
    if (script_mem == NULL) {
        fprintf(stderr, "Error: cannot allocate memory for script %s\n", arg0);
        return 9;
    }

    fd = open(arg0, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot read script %s\n", arg0);
        free(script_mem);
        return 254;
    }

    while ((r = read(fd, script_mem + count, st.st_size - count)) > 0) {
        count += r;
    }
    if (count != st.st_size) {
        fprintf(stderr, "Error while reading script %s\n", arg0);
        free(script_mem);
        return 254;
    }
    script_mem[count - 1] = '\0';

    line = script_mem;
    r = 0;
    while (line) {
        eol = strchr(line, '\n');
        if (eol)
            *(eol++) = '\0';
        printf(" + %s\r\n", line);
        r = parseLine(line);
        line = eol;
    }
    free(script_mem);
    return r;
}

/* Main */
#ifndef APP_FRESH_MODULE
int main(int argc, char *argv[])
#else
int icebox_fresh(int argc, char *argv[])
#endif
{
    char line[MAXLINE]; // buffer for the user input
    int ret;
    struct sigaction sigint_a = {};
    struct sigaction sigcld_a = {};
    int idx = 1;
    sigint_a.sa_handler = signalHandler_int;
    sigcld_a.sa_handler = signalHandler_child;
    no_reprint_prmpt = 0; // to prevent the printing of the shell
                          // after certain methods

    sigaction(SIGINT, &sigint_a, NULL);
    sigaction(SIGCHLD, &sigcld_a, NULL);

    /* Check for "-t" arg */
    if ((argc > 2) && (strcmp(argv[1], "-t") == 0)) {
        shell_init(argv[2]);
        idx = 3;
    } else
        shell_init(NULL);

    if (argc > 0)
        shell_params_assign(argv, 1);

    update_pwd_env();
    history_init();

    /* Execute script */
    if (argc > idx) {
        shell_params_assign(&argv[idx], argc - idx);
        ret = fresh_exec(argv[idx], &argv[idx]);
        return ret;
    }

    welcomeScreen();
    fprintf(stdout, "Current pid = %d\r\n", getpid());

    while (1) {
        if (no_reprint_prmpt == 0)
            shellPrompt();
        no_reprint_prmpt = 0;

        // We empty the line buffer
        memset(line, '\0', MAXLINE);

        // We wait for user input
        /* fgets(line, MAXLINE, stdin); */
        while (readline(line, MAXLINE) == NULL)
            ;

        ret = parseLine(line);
    }
    exit(0);
}
