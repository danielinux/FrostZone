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

int no_reprint_prmpt;

static void signalHandler_child(int p);
static void signalHandler_int(int p);
int changeDirectory(char *args[]);

#define LIMIT 16    // max number of tokens for a command
#define MAXLINE 256 // max number of characters from user input
static char currentDirectory[128];

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
    if (fd < 0 && strcmp(history_path, "/var/fresh_history") == 0) {
        fd = open(history_path, O_RDONLY | O_CREAT, 0600);
        if (fd < 0) {
            strncpy(history_path, "/tmp/fresh_history",
                    sizeof(history_path) - 1);
            history_path[sizeof(history_path) - 1] = '\0';
            fd = open(history_path, O_RDONLY | O_CREAT, 0600);
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
    /* Disable history loading while debugging early shell startup. */
    /* history_load(); */
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

static int fresh_tty_trace_enabled = 0;
static int fresh_prompt_trace_done = 0;

/**
 * Function used to initialize our shell. We used the approach explained in
 * http://www.gnu.org/software/libc/manual/html_node/Initializing-the-Shell.html
 */
void shell_init(char *file)
{
    int stdin_fileno, stdout_fileno, stderr_fileno;

    if (file) {
        int retries;
        if (strcmp(file, "/dev/ttyS0") == 0) {
            fresh_tty_trace_enabled = 1;
        }
        close(0);
        close(1);
        close(2);
        for (retries = 0; retries < 100; retries++) {
            stdin_fileno = open(file, O_RDWR, 0);
            if (stdin_fileno >= 0)
                break;
        }
        if (stdin_fileno < 0)
            _exit(1);

        stdout_fileno = dup(stdin_fileno);
        if (stdout_fileno < 0)
            _exit(1);

        stderr_fileno = dup(stdin_fileno);
        if (stderr_fileno < 0)
            _exit(1);
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
static void welcomeScreen(void)
{
    printf("\r\n\t============================================\r\n");
    printf("\t          Frosted shell - aka \"Fresh\"         \r\n");
    printf("\t--------------------------------------------\r\n");
    printf("\t             Licensed under GPL             \r\n");
    printf("\t============================================\r\n");
    printf("\r\n\r\n");
}

/* SIGCHLD: drain every reapable zombie. WNOHANG-loop matches POSIX
 * shells — a single SIGCHLD can cover multiple exits when the kernel
 * coalesces them, which our previous one-shot reap leaked.
 *
 * Frosted's toolchain doesn't define sig_atomic_t; volatile int is
 * the closest equivalent and matches the previous declarations. */
static volatile int child_pid = 0;
static volatile int child_status = 0;

static void signalHandler_child(int p)
{
    int st;
    pid_t r;
    (void)p;
    while ((r = waitpid(-1, &st, WNOHANG)) > 0) {
        child_pid = r;
        child_status = st;
    }
}

static void shellPrompt(void)
{
    char prompt[256];
    char hostn[] = "frosted";
    char *cwd = getcwd(currentDirectory, 128);
    if (cwd != NULL) {
        snprintf(prompt, 255, "root@%s %s # ", hostn, cwd);
        write(STDOUT_FILENO, prompt, strlen(prompt));
    }
}

/* SIGINT during prompt: caught so the kernel doesn't terminate the
 * shell. Body intentionally empty — the read loop notices the
 * interruption via EINTR and reprints the prompt. */
static void signalHandler_int(int p)
{
    (void)p;
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

/* Resolve command name to full path in /bin/.
 * Returns 0 on success, -1 if not found. Result written to buf. */
static int resolve_cmd(const char *cmd, char *buf, int bufsz)
{
    struct stat st;
    if (!strchr(cmd, '/') || (stat(cmd, &st) < 0)) {
        strncpy(buf, "/bin/", bufsz - 1);
        strncpy(buf + 5, cmd, bufsz - 6);
        buf[bufsz - 1] = '\0';
    } else {
        strncpy(buf, cmd, bufsz - 1);
        buf[bufsz - 1] = '\0';
    }
    if (stat(buf, &st) < 0)
        return -1;
    return 0;
}

static int launchProg(char **args, int background)
{
    int err = -1;
    struct stat st;
    char bin_arg0[60];
    char interpreter[30] = "/bin/fresh";
    enum x_type xt;
    int pid;

    if (resolve_cmd(args[0], bin_arg0, sizeof(bin_arg0)) < 0) {
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
* Set up I/O redirections in the current process (parent).
* Returns 0 on success, -1 on failure.
* Saves original fds in saved_in/saved_out/saved_err for later restore.
*/
static int redir_setup(char *inputFile, char *outputFile, int option,
                       int *saved_in, int *saved_out, int *saved_err)
{
    int fd;
    *saved_in = *saved_out = *saved_err = -1;

    if (option == 0) {
        /* > stdout */
        fd = open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0) { perror(outputFile); return -1; }
        *saved_out = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    } else if (option == 1) {
        /* < stdin + > stdout */
        fd = open(inputFile, O_RDONLY, 0600);
        if (fd < 0) { perror(inputFile); return -1; }
        *saved_in = dup(STDIN_FILENO);
        dup2(fd, STDIN_FILENO);
        close(fd);
        fd = open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0) {
            perror(outputFile);
            dup2(*saved_in, STDIN_FILENO);
            close(*saved_in);
            *saved_in = -1;
            return -1;
        }
        *saved_out = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    } else if (option == 2) {
        /* >> append */
        fd = open(outputFile, O_CREAT | O_APPEND | O_WRONLY, 0600);
        if (fd < 0) { perror(outputFile); return -1; }
        *saved_out = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    } else if (option == 3) {
        /* 2> stderr */
        fd = open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0) { perror(outputFile); return -1; }
        *saved_err = dup(STDERR_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    return 0;
}

static void redir_restore(int saved_in, int saved_out, int saved_err)
{
    if (saved_in >= 0) {
        dup2(saved_in, STDIN_FILENO);
        close(saved_in);
    }
    if (saved_out >= 0) {
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    if (saved_err >= 0) {
        dup2(saved_err, STDERR_FILENO);
        close(saved_err);
    }
}

/**
 * Run a pipeline `cmd1 | cmd2 | ...`
 *
 * Standard pattern: create every pipe up front, fork each stage with its
 * stdin/stdout bound to the adjacent pipes, close *all* pipe fds in the
 * parent, then wait for each child. Doing it this way avoids the
 * fork-serially-then-wait deadlock the previous version hit whenever a
 * stage's output did not fit into the 64-byte pipe buffer (PIPE_BUFSIZE
 * in frosted/pipe.c).
 */
static int pipeHandler(char *args[])
{
    char **commands[LIMIT];
    pid_t pids[LIMIT];
    int pipes[LIMIT][2];
    int cmd_count = 0;
    char **start = args;
    int last_status = 0;
    int i, j;
    int saved_stdin, saved_stdout;

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

    /* Create every pipe before forking any stage. */
    for (i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            for (j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return -1;
        }
    }

    saved_stdin  = dup(STDIN_FILENO);
    saved_stdout = dup(STDOUT_FILENO);

    for (i = 0; i < cmd_count; i++) {
        char resolved[60];
        pid_t pid;
        int in_fd, out_fd;

        if (resolve_cmd(commands[i][0], resolved, sizeof(resolved)) < 0) {
            printf("fresh: %s: command not found\r\n", commands[i][0]);
            pids[i] = -1;
            continue;
        }

        in_fd  = (i == 0)               ? saved_stdin  : pipes[i - 1][0];
        out_fd = (i == cmd_count - 1)   ? saved_stdout : pipes[i][1];

        /* Bind this stage's stdin/stdout in the parent before vfork; the
         * child inherits them. Restore the parent's terminal fds after
         * vfork returns so subsequent stages (and the prompt) are sane. */
        dup2(in_fd,  STDIN_FILENO);
        dup2(out_fd, STDOUT_FILENO);

        child_pid = 0;
        pid = vfork();
        if (pid < 0) {
            perror("vfork");
            pids[i] = -1;
            dup2(saved_stdin,  STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            continue;
        }
        if (pid == 0) {
            /* Child: exec the target. No fd cleanup needed in the child
             * because Frosted close-on-exec closes everything except the
             * 3 inherited standard fds. */
            execvp(resolved, commands[i]);
            _exit(127);
        }
        pids[i] = pid;

        dup2(saved_stdin,  STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
    }

    /* Parent must close every pipe fd so readers see EOF once upstream
     * producers exit. Without this the last stage blocks forever waiting
     * for data that will never arrive. */
    for (i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    close(saved_stdin);
    close(saved_stdout);

    /* Reap every stage. The handler's SIGCHLD-based child_pid is noisy
     * across multiple concurrent children, so use waitpid directly. */
    for (i = 0; i < cmd_count; i++) {
        int status;
        if (pids[i] < 0)
            continue;
        while (waitpid(pids[i], &status, 0) < 0) {
            /* retry on interruption */
        }
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

    /* P2: variable / command substitution has already happened in the
     * expander phase (expand_tokens); commandHandler only walks for
     * operators. */
    while (j < argc) {
        if ((strcmp(args[j], ">") == 0) || (strcmp(args[j], ">>") == 0) ||
            (strcmp(args[j], "2>") == 0) || (strcmp(args[j], "<") == 0) ||
            (strcmp(args[j], "&") == 0)) {
            break;
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
            int rc;
            if (!key)
                return -ENOMEM;
            strncpy(key, command, keylen);
            *(key + keylen) = 0x0;
            token++;
            rc = setenv(key, token, 1);
            free(key);
            if (rc) {
                return -errno;
            }
            return 0;
        }
    }

    // 'exit' command quits the shell
    if (strcmp(args[0], "exit") == 0)
        _exit(0);
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
        if (argc < 3) {
            printf("usage: setenv NAME VALUE\r\n");
            setenv("?", "1", 1);
        } else {
            setenv(args[1], args[2], 1);
        }
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
            if (value) {
                printf("%s\r\n", value);
                setenv("?", "0", 1);
            } else {
                printf("getenv: variable '%s' is not set\r\n", args[1]);
                setenv("?", "1", 1);
            }
        } else {
            extern char **environ;
            for (char **env = environ; env && *env; env++)
                printf("%s\r\n", *env);
            setenv("?", "0", 1);
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
                args_aux[i] = NULL;
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
                int sv_in, sv_out, sv_err, rv;
                aux = i + 1;
                if (args[aux] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                }
                if (args[aux + 1] == NULL) {
                    if (redir_setup(args[i + 1], NULL, 1, &sv_in, &sv_out, &sv_err) < 0)
                        return -1;
                    rv = launchProg(args_aux, 0);
                    redir_restore(sv_in, sv_out, sv_err);
                    return rv;
                }
                if (strcmp(args[aux + 1], ">") != 0) {
                    printf("Usage: Expected '>' and found %s\r\n",
                           args[aux + 1]);
                    return -2;
                }
                if (args[aux + 2] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                }
                if (redir_setup(args[i + 1], args[i + 3], 1, &sv_in, &sv_out, &sv_err) < 0)
                    return -1;
                rv = launchProg(args_aux, 0);
                redir_restore(sv_in, sv_out, sv_err);
                return rv;
            }
            // '>>' append redirection
            else if (strcmp(args[i], ">>") == 0) {
                int sv_in, sv_out, sv_err, rv;
                if (args[i + 1] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                }
                if (redir_setup(NULL, args[i + 1], 2, &sv_in, &sv_out, &sv_err) < 0)
                    return -1;
                rv = launchProg(args_aux, 0);
                redir_restore(sv_in, sv_out, sv_err);
                return rv;
            }
            // '2>' stderr redirection
            else if (strcmp(args[i], "2>") == 0) {
                int sv_in, sv_out, sv_err, rv;
                if (args[i + 1] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                }
                if (redir_setup(NULL, args[i + 1], 3, &sv_in, &sv_out, &sv_err) < 0)
                    return -1;
                rv = launchProg(args_aux, 0);
                redir_restore(sv_in, sv_out, sv_err);
                return rv;
            }
            // '>' output redirection
            else if (strcmp(args[i], ">") == 0) {
                int sv_in, sv_out, sv_err, rv;
                if (args[i + 1] == NULL) {
                    printf("Not enough input arguments\r\n");
                    return -1;
                }
                if (redir_setup(NULL, args[i + 1], 0, &sv_in, &sv_out, &sv_err) < 0)
                    return -1;
                rv = launchProg(args_aux, 0);
                redir_restore(sv_in, sv_out, sv_err);
                return rv;
            }
            i++;
        }
        // We launch the program with our method, indicating if we
        // want background execution or not
        return launchProg(args_aux, background);
    }
}

char *readline_tty(char *input, int size)
{
    for (;;) {
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
                    if (pos > 0) {
                        write(STDOUT_FILENO, &del, 1);
                        pos--;
                    }
                    continue;
                }
            }

            if (ret > 3) {
                if ((got[0] == 0x1B) && (got[2] == 0x33) && (got[3] == 0x7E)) {
                    /* Delete key: remove char at cursor position */
                    if (pos < len) {
                        len--;
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
                        input[len] = '\0';
                        continue;
                    }
                }
                continue;
            }
            if ((ret > 0) && (got[0] >= 0x20) && (got[0] <= 0x7e)) {
                for (i = 0; i < ret; i++) {
                    if (len >= size - 2)
                        break;
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

/*
 * Lexer (P1 of the fresh refactor)
 *
 * Five-stage pipeline: read -> lex -> parse -> expand -> execute.
 * This is the lex stage. Output is a NULL-terminated argv-style array
 * of word strings, plus parallel type and quote-flag arrays that later
 * stages will consume.
 *
 * - Static storage: word bytes live in lex_buf, pointers in lex_words.
 *   No malloc on the hot path.
 * - Quote rules: '...' literal, "..." with \"$`\\ escapable; outside
 *   quotes \\<c> escapes one char.
 * - Operators recognised: | || & && ; < > >> 2> >& <& ( ).  Each is
 *   emitted with its lexeme so the legacy commandHandler keeps working
 *   via strcmp; later phases can switch to type-based dispatch.
 * - Comment '#' starts only at a word boundary outside quotes (the old
 *   strchr-based stripping clobbered '#' inside quoted strings).
 *
 * Caveat: a *quoted* operator-lookalike (e.g. echo ">") is emitted as
 * a T_WORD with lexeme ">" — same string as a real T_GT operator. The
 * legacy commandHandler can't tell them apart; P3 will switch to a
 * type-aware executor and remove the ambiguity.
 */

#define LEX_BUF_BYTES (MAXLINE + 64)
#define LEX_MAX_TOKS  64

enum tok_type {
    T_WORD = 0,
    T_PIPE,        /* |  */
    T_OR,          /* || */
    T_AMP,         /* &  */
    T_AND,         /* && */
    T_SEMI,        /* ;  */
    T_LT,          /* <  */
    T_GT,          /* >  */
    T_DGT,         /* >> */
    T_ERR_GT,      /* 2> */
    T_GT_AMP,      /* >& */
    T_LT_AMP,      /* <& */
    T_LP,          /* (  */
    T_RP,          /* )  */
};

#define LEX_QUOTED_SINGLE 0x1
#define LEX_QUOTED_DOUBLE 0x2

static char     lex_buf[LEX_BUF_BYTES];
static char    *lex_words[LEX_MAX_TOKS + 1];   /* +1 for NULL terminator */
static uint8_t  lex_types[LEX_MAX_TOKS];
static uint8_t  lex_quoted[LEX_MAX_TOKS];
static int      lex_count;

static int lex_emit_op(const char *lexeme, enum tok_type type,
                       char **bp, char *be)
{
    size_t need = strlen(lexeme) + 1;
    if (lex_count >= LEX_MAX_TOKS)
        return -1;
    if ((size_t)(be - *bp) < need)
        return -1;
    lex_words[lex_count] = *bp;
    memcpy(*bp, lexeme, need);
    *bp += need;
    lex_types[lex_count] = (uint8_t)type;
    lex_quoted[lex_count] = 0;
    lex_count++;
    return 0;
}

static int lex_emit_word(char *start, int quoted_mask)
{
    if (lex_count >= LEX_MAX_TOKS)
        return -1;
    lex_words[lex_count] = start;
    lex_types[lex_count] = (uint8_t)T_WORD;
    lex_quoted[lex_count] = (uint8_t)quoted_mask;
    lex_count++;
    return 0;
}

/*
 * Lex a single logical input line. Returns the number of tokens on
 * success (>=0), or -1 on overflow / unterminated quote.
 */
static int lex_line(const char *line)
{
    char *bp = lex_buf;
    char *be = lex_buf + sizeof(lex_buf);
    const char *p = line;
    char *word_start = NULL;
    int word_quoted = 0;

    lex_count = 0;
    lex_words[0] = NULL;
    if (!line)
        return 0;

    while (*p) {
        char c = *p;

        /* Command substitution: $(...) and `...` are slurped into the
         * current word verbatim. The expander phase will run them.
         * Crude quote-aware paren counter for $(...). */
        if (c == '$' && p[1] == '(') {
            int depth = 1;
            if (!word_start) {
                if (bp >= be) return -1;
                word_start = bp;
                word_quoted = 0;
            }
            if (bp >= be - 2) return -1;
            *bp++ = '$';
            *bp++ = '(';
            p += 2;
            while (*p && depth > 0) {
                char ch = *p;
                if (ch == '\'') {
                    if (bp >= be - 1) return -1; *bp++ = ch; p++;
                    while (*p && *p != '\'') {
                        if (bp >= be - 1) return -1; *bp++ = *p++;
                    }
                    if (!*p) return -1;
                    if (bp >= be - 1) return -1; *bp++ = *p++;
                    continue;
                }
                if (ch == '"') {
                    if (bp >= be - 1) return -1; *bp++ = ch; p++;
                    while (*p && *p != '"') {
                        if (*p == '\\' && p[1]) {
                            if (bp >= be - 2) return -1;
                            *bp++ = '\\'; *bp++ = p[1]; p += 2; continue;
                        }
                        if (bp >= be - 1) return -1; *bp++ = *p++;
                    }
                    if (!*p) return -1;
                    if (bp >= be - 1) return -1; *bp++ = *p++;
                    continue;
                }
                if (ch == '(') depth++;
                else if (ch == ')') {
                    depth--;
                    if (depth == 0) {
                        if (bp >= be - 1) return -1;
                        *bp++ = ')'; p++;
                        break;
                    }
                }
                if (bp >= be - 1) return -1;
                *bp++ = *p++;
            }
            if (depth != 0)
                return -1;
            continue;
        }
        if (c == '`') {
            if (!word_start) {
                if (bp >= be) return -1;
                word_start = bp;
                word_quoted = 0;
            }
            if (bp >= be - 1) return -1;
            *bp++ = '`'; p++;
            while (*p && *p != '`') {
                if (*p == '\\' && p[1]) {
                    if (bp >= be - 2) return -1;
                    *bp++ = '\\'; *bp++ = p[1]; p += 2; continue;
                }
                if (bp >= be - 1) return -1;
                *bp++ = *p++;
            }
            if (!*p) return -1;
            if (bp >= be - 1) return -1;
            *bp++ = '`'; p++;
            continue;
        }

        if (!word_start) {
            /* Between tokens: skip whitespace, recognise operators */
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                p++;
                continue;
            }
            if (c == '#')
                break;   /* unquoted '#' starts a comment */

            /* Two-char operators (longest match first). */
            if (c == '|' && p[1] == '|') {
                if (lex_emit_op("||", T_OR, &bp, be) < 0) return -1;
                p += 2; continue;
            }
            if (c == '&' && p[1] == '&') {
                if (lex_emit_op("&&", T_AND, &bp, be) < 0) return -1;
                p += 2; continue;
            }
            if (c == '>' && p[1] == '>') {
                if (lex_emit_op(">>", T_DGT, &bp, be) < 0) return -1;
                p += 2; continue;
            }
            if (c == '>' && p[1] == '&') {
                if (lex_emit_op(">&", T_GT_AMP, &bp, be) < 0) return -1;
                p += 2; continue;
            }
            if (c == '<' && p[1] == '&') {
                if (lex_emit_op("<&", T_LT_AMP, &bp, be) < 0) return -1;
                p += 2; continue;
            }
            if (c == '2' && p[1] == '>') {
                if (lex_emit_op("2>", T_ERR_GT, &bp, be) < 0) return -1;
                p += 2; continue;
            }

            /* One-char operators. */
            if (c == '|') { if (lex_emit_op("|", T_PIPE, &bp, be) < 0) return -1; p++; continue; }
            if (c == '&') { if (lex_emit_op("&", T_AMP,  &bp, be) < 0) return -1; p++; continue; }
            if (c == ';') { if (lex_emit_op(";", T_SEMI, &bp, be) < 0) return -1; p++; continue; }
            if (c == '<') { if (lex_emit_op("<", T_LT,   &bp, be) < 0) return -1; p++; continue; }
            if (c == '>') { if (lex_emit_op(">", T_GT,   &bp, be) < 0) return -1; p++; continue; }
            if (c == '(') { if (lex_emit_op("(", T_LP,   &bp, be) < 0) return -1; p++; continue; }
            if (c == ')') { if (lex_emit_op(")", T_RP,   &bp, be) < 0) return -1; p++; continue; }

            /* Otherwise: start a word here. */
            if (bp >= be)
                return -1;
            word_start = bp;
            word_quoted = 0;
        }

        /* Inside a word */
        c = *p;
        if (c == '\'') {
            /* Single quote: literal until closing ' */
            word_quoted |= LEX_QUOTED_SINGLE;
            p++;
            while (*p && *p != '\'') {
                if (bp >= be - 1) return -1;
                *bp++ = *p++;
            }
            if (!*p)
                return -1;   /* unterminated */
            p++;
            continue;
        }
        if (c == '"') {
            word_quoted |= LEX_QUOTED_DOUBLE;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    char nxt = p[1];
                    if (nxt == '"' || nxt == '\\' || nxt == '$' || nxt == '`') {
                        if (bp >= be - 1) return -1;
                        *bp++ = nxt;
                        p += 2;
                        continue;
                    }
                    /* otherwise the backslash stays literal */
                    if (bp >= be - 1) return -1;
                    *bp++ = '\\';
                    p++;
                    continue;
                }
                if (bp >= be - 1) return -1;
                *bp++ = *p++;
            }
            if (!*p)
                return -1;
            p++;
            continue;
        }
        if (c == '\\' && p[1]) {
            /* Outside quotes: \ escapes the next char (incl. newline). */
            if (bp >= be - 1) return -1;
            *bp++ = p[1];
            p += 2;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (bp >= be) return -1;
            *bp++ = '\0';
            if (lex_emit_word(word_start, word_quoted) < 0) return -1;
            word_start = NULL;
            word_quoted = 0;
            p++;
            continue;
        }
        /* Operator chars also end the current word; re-process them next loop. */
        if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
            c == '(' || c == ')') {
            if (bp >= be) return -1;
            *bp++ = '\0';
            if (lex_emit_word(word_start, word_quoted) < 0) return -1;
            word_start = NULL;
            word_quoted = 0;
            continue;
        }
        if (c == '#' && !word_quoted) {
            if (bp >= be) return -1;
            *bp++ = '\0';
            if (lex_emit_word(word_start, word_quoted) < 0) return -1;
            word_start = NULL;
            break;
        }
        if (bp >= be - 1) return -1;
        *bp++ = c;
        p++;
    }

    if (word_start) {
        if (bp >= be) return -1;
        *bp++ = '\0';
        if (lex_emit_word(word_start, word_quoted) < 0) return -1;
    }
    lex_words[lex_count] = NULL;
    return lex_count;
}

/*
 * Expander (P2 of the fresh refactor)
 *
 * Runs after the lexer, before commandHandler. Per word:
 * - Replaces $VAR / ${VAR} / $? / $$ / $# / $0..$9.
 * - Runs $(...) and `...` substitutions in a sub-fresh and substitutes
 *   the captured stdout (with trailing newlines stripped).
 * - Word-splits substituted output on whitespace when the input word
 *   was unquoted; emits as a single word (possibly empty) when quoted.
 *
 * Operator tokens (T_PIPE, T_GT, ...) pass through untouched.
 */

#define EXP_BUF_BYTES   (LEX_BUF_BYTES * 4)
#define EXP_MAX_TOKS    LEX_MAX_TOKS
#define CMDSUB_BUF      512

static char     exp_buf[EXP_BUF_BYTES];
static char    *exp_words[EXP_MAX_TOKS + 1];
static uint8_t  exp_types[EXP_MAX_TOKS];
static int      exp_count;

static const char *resolve_var(const char *name, int argc)
{
    static char scratch[16];
    (void)argc;

    if (name[0] == '\0')
        return "$";
    if (name[0] == '$' && name[1] == '\0') {
        snprintf(scratch, sizeof(scratch), "%d", getpid());
        return scratch;
    }
    if (name[0] == '?' && name[1] == '\0') {
        const char *e = getenv("?");
        return e ? e : "0";
    }
    if (name[0] == '#' && name[1] == '\0') {
        snprintf(scratch, sizeof(scratch), "%d", shell_paramc);
        return scratch;
    }
    if (isdigit((unsigned char)name[0])) {
        char *endptr = NULL;
        long idx = strtol(name, &endptr, 10);
        if (endptr && *endptr != '\0')
            return empty_value;
        if (idx == 0)
            return shell_param0 ? shell_param0 : "fresh";
        if (idx > 0 && idx <= shell_paramc && shell_params[idx - 1])
            return shell_params[idx - 1];
        return empty_value;
    }
    if (isalpha((unsigned char)name[0]) || name[0] == '_') {
        const char *e = getenv(name);
        return e ? e : empty_value;
    }
    return empty_value;
}

/*
 * Run `cmd` in a sub-fresh (vfork + exec /bin/fresh -c), capture up to
 * bufsz-1 bytes of stdout, NUL-terminate, strip trailing newlines.
 * Returns the captured length (>=0), or -1 on error.
 */
static int run_cmdsub(const char *cmd, char *out, int bufsz)
{
    int fds[2];
    pid_t pid;
    int n_total = 0;
    int st;

    if (!cmd || !out || bufsz < 2)
        return -1;
    if (pipe(fds) < 0)
        return -1;

    pid = vfork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        char *child_argv[4];
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        if (fds[1] != STDOUT_FILENO)
            close(fds[1]);
        child_argv[0] = "fresh";
        child_argv[1] = "-c";
        child_argv[2] = (char *)cmd;
        child_argv[3] = NULL;
        execvp("/bin/fresh", child_argv);
        _exit(127);
    }
    close(fds[1]);
    while (n_total < bufsz - 1) {
        int r = read(fds[0], out + n_total, bufsz - 1 - n_total);
        if (r > 0)
            n_total += r;
        else if (r == 0)
            break;
        else if (errno == EINTR)
            continue;
        else
            break;
    }
    close(fds[0]);
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR)
            break;
    }
    while (n_total > 0 && out[n_total - 1] == '\n')
        n_total--;
    out[n_total] = '\0';
    return n_total;
}

/* Append a string to the current expansion word, splitting on whitespace
 * when not_quoted is true (each whitespace ends one output word). */
static int exp_append_value(const char *value, int not_quoted,
                            char **bp, char *be,
                            char **word_start_io, int *word_used_io)
{
    while (*value) {
        if (not_quoted && (*value == ' ' || *value == '\t' || *value == '\n')) {
            if (*word_used_io) {
                if (*bp >= be) return -1;
                **bp = '\0'; (*bp)++;
                if (exp_count >= EXP_MAX_TOKS) return -1;
                exp_words[exp_count] = *word_start_io;
                exp_types[exp_count] = T_WORD;
                exp_count++;
                *word_start_io = *bp;
                *word_used_io = 0;
            }
            value++;
            continue;
        }
        if (*bp >= be - 1) return -1;
        **bp = *value;
        (*bp)++;
        value++;
        *word_used_io = 1;
    }
    return 0;
}

static int expand_one(const char *in, int quoted_mask, int argc,
                      char **bp, char *be)
{
    char *word_start = *bp;
    int word_used = 0;
    int not_quoted = (quoted_mask == 0);
    const char *p = in;

    /* A purely-quoted empty input must still emit one (empty) word. */
    if (quoted_mask != 0)
        word_used = 1;

    while (*p) {
        char c = *p;

        if (c == '$' && p[1] == '(') {
            const char *start = p + 2;
            const char *end = start;
            int depth = 1;
            char inner[256];
            char captured[CMDSUB_BUF];
            size_t inner_len;
            int caplen;

            while (*end && depth > 0) {
                if (*end == '\'') {
                    end++;
                    while (*end && *end != '\'') end++;
                    if (*end) end++;
                    continue;
                }
                if (*end == '"') {
                    end++;
                    while (*end && *end != '"') {
                        if (*end == '\\' && end[1]) end += 2;
                        else end++;
                    }
                    if (*end) end++;
                    continue;
                }
                if (*end == '(') depth++;
                else if (*end == ')') { depth--; if (depth == 0) break; }
                end++;
            }
            if (depth != 0) {
                /* unmatched — fall through, emit literal */
                if (*bp >= be - 1) return -1;
                **bp = '$'; (*bp)++; word_used = 1;
                p++;
                continue;
            }
            inner_len = (size_t)(end - start);
            if (inner_len >= sizeof(inner))
                return -1;
            memcpy(inner, start, inner_len);
            inner[inner_len] = '\0';

            caplen = run_cmdsub(inner, captured, sizeof(captured));
            if (caplen < 0)
                caplen = 0;
            if (exp_append_value(captured, not_quoted, bp, be,
                                 &word_start, &word_used) < 0)
                return -1;
            p = end + 1;
            continue;
        }

        if (c == '`') {
            const char *start = p + 1;
            const char *end = strchr(start, '`');
            char inner[256];
            char captured[CMDSUB_BUF];
            size_t inner_len;
            int caplen;

            if (!end) {
                if (*bp >= be - 1) return -1;
                **bp = '`'; (*bp)++; word_used = 1;
                p++;
                continue;
            }
            inner_len = (size_t)(end - start);
            if (inner_len >= sizeof(inner))
                return -1;
            memcpy(inner, start, inner_len);
            inner[inner_len] = '\0';
            caplen = run_cmdsub(inner, captured, sizeof(captured));
            if (caplen < 0)
                caplen = 0;
            if (exp_append_value(captured, not_quoted, bp, be,
                                 &word_start, &word_used) < 0)
                return -1;
            p = end + 1;
            continue;
        }

        if (c == '$') {
            char namebuf[32];
            const char *name_start = p + 1;
            const char *name_end;
            const char *value;
            int curly = 0;

            if (*name_start == '{') {
                curly = 1;
                name_start++;
                name_end = strchr(name_start, '}');
                if (!name_end) {
                    if (*bp >= be - 1) return -1;
                    **bp = '$'; (*bp)++; word_used = 1;
                    p++;
                    continue;
                }
            } else if (*name_start == '\0') {
                if (*bp >= be - 1) return -1;
                **bp = '$'; (*bp)++; word_used = 1;
                p++;
                continue;
            } else if (*name_start == '$' || *name_start == '?' ||
                       *name_start == '#') {
                name_end = name_start + 1;
            } else if (isdigit((unsigned char)*name_start)) {
                name_end = name_start + 1;
            } else if (isalpha((unsigned char)*name_start) ||
                       *name_start == '_') {
                const char *q = name_start;
                while (isalnum((unsigned char)*q) || *q == '_') q++;
                name_end = q;
            } else {
                if (*bp >= be - 1) return -1;
                **bp = '$'; (*bp)++; word_used = 1;
                p++;
                continue;
            }
            {
                size_t nl = (size_t)(name_end - name_start);
                if (nl >= sizeof(namebuf))
                    nl = sizeof(namebuf) - 1;
                memcpy(namebuf, name_start, nl);
                namebuf[nl] = '\0';
            }
            value = resolve_var(namebuf, argc);
            if (exp_append_value(value, not_quoted, bp, be,
                                 &word_start, &word_used) < 0)
                return -1;
            p = curly ? (name_end + 1) : name_end;
            continue;
        }

        /* Literal character */
        if (*bp >= be - 1) return -1;
        **bp = c; (*bp)++; word_used = 1;
        p++;
    }

    if (word_used) {
        if (*bp >= be) return -1;
        **bp = '\0'; (*bp)++;
        if (exp_count >= EXP_MAX_TOKS) return -1;
        exp_words[exp_count] = word_start;
        exp_types[exp_count] = T_WORD;
        exp_count++;
    }
    return 0;
}

static int expand_tokens(int script_argc)
{
    int i;
    char *bp = exp_buf;
    char *be = exp_buf + sizeof(exp_buf);

    exp_count = 0;
    for (i = 0; i < lex_count; i++) {
        if (lex_types[i] != T_WORD) {
            /* Operator tokens: pass through unchanged, lexeme stays in
             * lex_buf which lives until the next lex_line(). */
            if (exp_count >= EXP_MAX_TOKS)
                return -1;
            exp_words[exp_count] = lex_words[i];
            exp_types[exp_count] = lex_types[i];
            exp_count++;
            continue;
        }
        if (expand_one(lex_words[i], lex_quoted[i], script_argc, &bp, be) < 0)
            return -1;
    }
    exp_words[exp_count] = NULL;
    return exp_count;
}

static int parseLine(char *line)
{
    int n;

    if (!line)
        return 0;

    /* Skip script shebang explicitly so it doesn't even reach the lexer. */
    if (line[0] == '#' && line[1] == '!')
        return 0;

    n = lex_line(line);
    if (n < 0) {
        printf("fresh: parse error\r\n");
        return -1;
    }
    if (n == 0)
        return 0;
    if (expand_tokens(n) < 0) {
        printf("fresh: expansion overflow\r\n");
        return -1;
    }
    if (exp_count == 0)
        return 0;
    return commandHandler(exp_words, exp_count);
}

/* Fresh exec */

static int fresh_exec(char *arg0, char **argv)
{
    struct stat st;
    char *script_mem;
    int fd;
    int r, count = 0;
    char *line, *eol;
    int line_no = 0;

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
    script_mem[count] = '\0';

    line = script_mem;
    r = 0;
    while (line) {
        line_no++;
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

    /* `-c CMD` runs CMD as a single fresh line and exits with its
     * status. Used by command substitution ($() / backticks) — the
     * sub-fresh inherits stdout via the captured pipe. */
    if ((argc >= 3) && (strcmp(argv[1], "-c") == 0)) {
        int rc = 0;
        int n;
        shell_init(NULL);
        update_pwd_env();
        n = lex_line(argv[2]);
        if (n < 0) {
            rc = 2;
        } else if (n > 0) {
            if (expand_tokens(n) < 0) {
                rc = 2;
            } else if (exp_count > 0) {
                rc = commandHandler(exp_words, exp_count);
            }
        }
        fflush(stdout);
        fflush(stderr);
        _exit(rc);
    }

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
        fflush(stdout);
        fflush(stderr);
        _exit(ret);
    }

    welcomeScreen();
    fprintf(stdout, "Current pid = %d\r\n", getpid());

    while (1) {
        if (fresh_tty_trace_enabled && !fresh_prompt_trace_done) {
            fresh_prompt_trace_done = 1;
        }
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
