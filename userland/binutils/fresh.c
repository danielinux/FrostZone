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

/*
 * Parser + executor (P3 of the fresh refactor)
 *
 * Token stream (exp_words[] / exp_types[] from the expander) → AST →
 * exec_ast(). One commit replaces the legacy launchProg / pipeHandler
 * / commandHandler triumvirate with five small functions:
 *   parse_simple / parse_pipeline / parse_and_or / parse_list  (parser)
 *   try_builtin                                                 (builtins)
 *   run_simple                                                  (cmd leaf)
 *   run_pipeline                                                (pipe node)
 *   exec_ast                                                    (dispatch)
 *
 * Grammar (Bourne subset, recursive descent):
 *   list      := and_or ((';' | '&') and_or)*  [trailing ';' or '&']
 *   and_or    := pipeline (('&&' | '||') pipeline)*
 *   pipeline  := simple ('|' simple)*
 *   simple    := ['!'] WORD+ REDIR* | REDIR+
 *   REDIR     := '<' WORD | '>' WORD | '>>' WORD | '2>' WORD
 *
 * Static memory: ast_nodes (64), ast_redirs (16). All argv slots are
 * indices into exp_words[] — the lexer and expander already gave us
 * a stable, contiguous word array, so no copy.
 */

#define MAX_AST_NODES   64
#define MAX_REDIRS      16
#define MAX_PIPE_STAGES 8

enum ast_type {
    AST_CMD,
    AST_PIPE,
    AST_AND_OR,
    AST_SEQ,
};

#define AST_FLAG_AND  0x01   /* AST_AND_OR: connector is && */
#define AST_FLAG_OR   0x02   /* AST_AND_OR: connector is || */
#define AST_FLAG_BG   0x04   /* run subtree async, return 0 */
#define AST_FLAG_NEG  0x08   /* invert exit status (leading '!') */

enum redir_op {
    R_IN,        /* <  word */
    R_OUT,       /* >  word */
    R_APPEND,    /* >> word */
    R_ERR,       /* 2> word */
};

struct ast_redir {
    uint8_t op;
    uint8_t fd;            /* fd to redirect (0/1/2) */
    int16_t word_idx;      /* exp_words[] index for filename */
};

struct ast_node {
    uint8_t  type;
    uint8_t  flags;
    int16_t  left;
    int16_t  right;
    int16_t  argv_first;   /* AST_CMD: index in exp_words[] */
    int16_t  argv_n;
    int16_t  redir_first;  /* AST_CMD: index in ast_redirs[] */
    int16_t  redir_n;
};

static struct ast_node  ast_nodes[MAX_AST_NODES];
static struct ast_redir ast_redirs[MAX_REDIRS];
static int ast_node_count;
static int ast_redir_count;
static int parse_pos;
static int parse_n_tokens;

static int ast_alloc_node(uint8_t type)
{
    int idx;
    if (ast_node_count >= MAX_AST_NODES)
        return -1;
    idx = ast_node_count++;
    memset(&ast_nodes[idx], 0, sizeof(struct ast_node));
    ast_nodes[idx].type = type;
    ast_nodes[idx].left = -1;
    ast_nodes[idx].right = -1;
    ast_nodes[idx].argv_first = -1;
    ast_nodes[idx].redir_first = -1;
    return idx;
}

static int ast_alloc_redir(uint8_t op, uint8_t fd, int16_t word_idx)
{
    int idx;
    if (ast_redir_count >= MAX_REDIRS)
        return -1;
    idx = ast_redir_count++;
    ast_redirs[idx].op = op;
    ast_redirs[idx].fd = fd;
    ast_redirs[idx].word_idx = word_idx;
    return idx;
}

static int peek_type(void)
{
    if (parse_pos >= parse_n_tokens)
        return -1;
    return exp_types[parse_pos];
}

static const char *peek_word(void)
{
    if (parse_pos >= parse_n_tokens)
        return NULL;
    return exp_words[parse_pos];
}

static int parse_simple(void)
{
    int idx;
    int neg = 0;

    if (peek_type() == T_WORD && peek_word() &&
        strcmp(peek_word(), "!") == 0) {
        neg = 1;
        parse_pos++;
    }

    {
        int t = peek_type();
        if (t != T_WORD && t != T_LT && t != T_GT &&
            t != T_DGT && t != T_ERR_GT)
            return -1;
    }

    idx = ast_alloc_node(AST_CMD);
    if (idx < 0)
        return -1;
    if (neg)
        ast_nodes[idx].flags |= AST_FLAG_NEG;

    while (parse_pos < parse_n_tokens) {
        int t = exp_types[parse_pos];

        if (t == T_WORD) {
            if (ast_nodes[idx].argv_first < 0)
                ast_nodes[idx].argv_first = (int16_t)parse_pos;
            ast_nodes[idx].argv_n++;
            parse_pos++;
            continue;
        }
        if (t == T_LT || t == T_GT || t == T_DGT || t == T_ERR_GT) {
            uint8_t op = R_IN;
            uint8_t fd = 0;
            int16_t target;
            int r;

            switch (t) {
            case T_LT:     op = R_IN;     fd = 0; break;
            case T_GT:     op = R_OUT;    fd = 1; break;
            case T_DGT:    op = R_APPEND; fd = 1; break;
            case T_ERR_GT: op = R_ERR;    fd = 2; break;
            default:       return -1;
            }
            parse_pos++;
            if (parse_pos >= parse_n_tokens ||
                exp_types[parse_pos] != T_WORD)
                return -1;
            target = (int16_t)parse_pos;
            parse_pos++;

            r = ast_alloc_redir(op, fd, target);
            if (r < 0)
                return -1;
            if (ast_nodes[idx].redir_first < 0)
                ast_nodes[idx].redir_first = (int16_t)r;
            ast_nodes[idx].redir_n++;
            continue;
        }
        break;
    }

    if (ast_nodes[idx].argv_n == 0)
        return -1;
    return idx;
}

static int parse_pipeline(void)
{
    int left = parse_simple();
    if (left < 0)
        return -1;

    while (peek_type() == T_PIPE) {
        int right, p;
        parse_pos++;
        right = parse_simple();
        if (right < 0)
            return -1;
        p = ast_alloc_node(AST_PIPE);
        if (p < 0)
            return -1;
        ast_nodes[p].left = (int16_t)left;
        ast_nodes[p].right = (int16_t)right;
        left = p;
    }
    return left;
}

static int parse_and_or(void)
{
    int left = parse_pipeline();
    if (left < 0)
        return -1;

    while (peek_type() == T_AND || peek_type() == T_OR) {
        int op = peek_type();
        int right, p;
        parse_pos++;
        right = parse_pipeline();
        if (right < 0)
            return -1;
        p = ast_alloc_node(AST_AND_OR);
        if (p < 0)
            return -1;
        ast_nodes[p].left = (int16_t)left;
        ast_nodes[p].right = (int16_t)right;
        ast_nodes[p].flags |= (op == T_AND) ? AST_FLAG_AND : AST_FLAG_OR;
        left = p;
    }
    return left;
}

static int parse_list(void)
{
    int left = parse_and_or();
    if (left < 0)
        return -1;

    if (peek_type() == T_AMP) {
        ast_nodes[left].flags |= AST_FLAG_BG;
        parse_pos++;
    }

    while (peek_type() == T_SEMI || peek_type() == T_AMP) {
        int right, s;
        if (peek_type() == T_SEMI) parse_pos++;
        if (parse_pos >= parse_n_tokens) break;
        right = parse_and_or();
        if (right < 0)
            return left;
        if (peek_type() == T_AMP) {
            ast_nodes[right].flags |= AST_FLAG_BG;
            parse_pos++;
        }
        s = ast_alloc_node(AST_SEQ);
        if (s < 0)
            return -1;
        ast_nodes[s].left = (int16_t)left;
        ast_nodes[s].right = (int16_t)right;
        left = s;
    }
    return left;
}

static int parse_tokens(void)
{
    ast_node_count = 0;
    ast_redir_count = 0;
    parse_pos = 0;
    parse_n_tokens = exp_count;
    if (parse_n_tokens == 0)
        return -1;
    return parse_list();
}

/* ---------------------------------------------------------------- */
/* Builtins                                                          */
/* ---------------------------------------------------------------- */

#define NOT_A_BUILTIN  (-2)

static int try_builtin(char **argv, int argc)
{
    int i;

    if (argc == 0)
        return NOT_A_BUILTIN;

    /* Lone `KEY=VALUE` token sets an env var. */
    if (argc == 1) {
        char *eq = strchr(argv[0], '=');
        if (eq && eq != argv[0]) {
            *eq = '\0';
            setenv(argv[0], eq + 1, 1);
            *eq = '=';
            return 0;
        }
    }

    if (strcmp(argv[0], "exit") == 0)
        _exit(argc > 1 ? atoi(argv[1]) : 0);

    if (strcmp(argv[0], "true") == 0 || strcmp(argv[0], ":") == 0)
        return 0;
    if (strcmp(argv[0], "false") == 0)
        return 1;

    if (strcmp(argv[0], "cd") == 0)
        return changeDirectory(argv) < 0 ? 1 : 0;

    if (strcmp(argv[0], "pwd") == 0) {
        printf("%s\r\n", getcwd(currentDirectory, sizeof(currentDirectory)));
        return 0;
    }

    if (strcmp(argv[0], "setenv") == 0) {
        if (argc < 3) {
            printf("usage: setenv NAME VALUE\r\n");
            return 1;
        }
        return setenv(argv[1], argv[2], 1) == 0 ? 0 : 1;
    }

    if (strcmp(argv[0], "export") == 0) {
        if (argc < 2) {
            printf("export: missing arguments\r\n");
            return 1;
        }
        for (i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                setenv(argv[i], eq + 1, 1);
                *eq = '=';
            } else if (i + 1 < argc) {
                setenv(argv[i], argv[++i], 1);
            } else {
                printf("export: missing value for %s\r\n", argv[i]);
                return 1;
            }
        }
        return 0;
    }

    if (strcmp(argv[0], "getenv") == 0) {
        if (argc > 1) {
            const char *v = getenv(argv[1]);
            if (v) {
                printf("%s\r\n", v);
                return 0;
            }
            printf("getenv: variable '%s' is not set\r\n", argv[1]);
            return 1;
        }
        {
            char **e;
            for (e = environ; e && *e; e++)
                printf("%s\r\n", *e);
        }
        return 0;
    }

    if (strcmp(argv[0], "unset") == 0 || strcmp(argv[0], "unsetenv") == 0) {
        if (argc < 2) {
            printf("unset: missing variable name\r\n");
            return 1;
        }
        for (i = 1; i < argc; i++)
            unsetenv(argv[i]);
        return 0;
    }

    if (strcmp(argv[0], "source") == 0 || strcmp(argv[0], ".") == 0)
        return builtin_source(argv, argc);

    if (strcmp(argv[0], "shift") == 0) {
        long count = (argc > 1) ? strtol(argv[1], NULL, 10) : 1;
        if (count < 0 || count > shell_paramc) {
            printf("shift: invalid count\r\n");
            return 1;
        }
        for (i = 0; i < count; i++) {
            if (shell_params[i]) {
                free(shell_params[i]);
                shell_params[i] = NULL;
            }
        }
        for (i = count; i < shell_paramc; i++)
            shell_params[i - count] = shell_params[i];
        for (i = shell_paramc - count; i < shell_paramc; i++)
            if (i >= 0)
                shell_params[i] = NULL;
        shell_paramc -= count;
        return 0;
    }

    return NOT_A_BUILTIN;
}

/* ---------------------------------------------------------------- */
/* Executor                                                          */
/* ---------------------------------------------------------------- */

/* Open a redir target and dup over the destination fd. Saves the
 * original via `save_fds[fd]` (if save_fds != NULL and slot not yet
 * saved) so the caller can restore on return. */
static int apply_redir(const struct ast_redir *r, int *save_fds)
{
    int fd;
    int flags;
    const char *path = exp_words[r->word_idx];

    switch (r->op) {
    case R_IN:     flags = O_RDONLY;                          break;
    case R_OUT:    flags = O_WRONLY | O_CREAT | O_TRUNC;      break;
    case R_APPEND: flags = O_WRONLY | O_CREAT | O_APPEND;     break;
    case R_ERR:    flags = O_WRONLY | O_CREAT | O_TRUNC;      break;
    default:       return -1;
    }
    fd = open(path, flags, 0600);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    if (save_fds && r->fd < 3 && save_fds[r->fd] < 0)
        save_fds[r->fd] = dup(r->fd);
    if (dup2(fd, r->fd) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static void restore_fds(int *save_fds)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (save_fds[i] >= 0) {
            dup2(save_fds[i], i);
            close(save_fds[i]);
            save_fds[i] = -1;
        }
    }
}

/*
 * Run an AST_CMD: apply redirs, dispatch builtin, else vfork+exec.
 * Returns 0..255 exit status. Sets the `?` env var. Honours
 * AST_FLAG_NEG on the node.
 */
static int run_simple(int idx)
{
    struct ast_node *n = &ast_nodes[idx];
    char *argv[LEX_MAX_TOKS + 1];
    int argc = 0;
    int save_fds[3] = { -1, -1, -1 };
    int rc = 1;
    int i;

    if (n->argv_n <= 0)
        return 0;
    for (i = 0; i < n->argv_n && argc < LEX_MAX_TOKS; i++)
        argv[argc++] = exp_words[n->argv_first + i];
    argv[argc] = NULL;

    for (i = 0; i < n->redir_n; i++) {
        if (apply_redir(&ast_redirs[n->redir_first + i], save_fds) < 0)
            goto out;
    }

    rc = try_builtin(argv, argc);
    if (rc != NOT_A_BUILTIN)
        goto out;

    {
        char resolved[256];
        char interp[32] = "/bin/fresh";
        enum x_type xt;
        pid_t pid;
        int st = 0;

        if (resolve_cmd(argv[0], resolved, sizeof(resolved)) < 0) {
            printf("fresh: %s: command not found\r\n", argv[0]);
            rc = 127;
            goto out;
        }
        xt = get_x_type(resolved);

        pid = vfork();
        if (pid < 0) {
            perror("vfork");
            rc = 1;
            goto out;
        }
        if (pid == 0) {
            switch (xt) {
            case X_bFLT:
                execvp(resolved, argv);
                _exit(127);
            case X_PY:
                strcpy(interp, "/bin/python");
                /* fall through */
            case X_SH: {
                char *aux[LEX_MAX_TOKS + 2];
                int j = 0;
                aux[j++] = interp;
                aux[j++] = resolved;
                for (i = 1; i < argc && j < LEX_MAX_TOKS + 1; i++)
                    aux[j++] = argv[i];
                aux[j] = NULL;
                execvp(interp, aux);
                _exit(127);
            }
            case X_ELF:
                _exit(126);
            default:
                _exit(127);
            }
        }
        while (waitpid(pid, &st, 0) < 0) {
            if (errno != EINTR)
                break;
        }
        if (WIFEXITED(st))         rc = WEXITSTATUS(st);
        else if (WIFSIGNALED(st))  rc = 128 + WTERMSIG(st);
        else                       rc = 1;
    }

out:
    restore_fds(save_fds);
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", rc);
        setenv("?", buf, 1);
    }
    if (n->flags & AST_FLAG_NEG)
        rc = (rc == 0) ? 1 : 0;
    return rc;
}

static int collect_pipeline(int idx, int *stages, int max)
{
    if (idx < 0 || max < 1)
        return -1;
    if (ast_nodes[idx].type != AST_PIPE) {
        stages[0] = idx;
        return 1;
    }
    {
        int n = collect_pipeline(ast_nodes[idx].left, stages, max);
        if (n < 0 || n >= max)
            return -1;
        stages[n] = ast_nodes[idx].right;
        return n + 1;
    }
}

/*
 * Run a pipeline. Same shape as the legacy pipeHandler: open every
 * pipe up front, fork each stage with stdin/stdout bound to adjacent
 * pipes (per-stage redirs override in the child), close every pipe
 * fd in the parent so consumers see EOF, then waitpid each.
 */
static int run_pipeline(int idx)
{
    int stages[MAX_PIPE_STAGES];
    int n = collect_pipeline(idx, stages, MAX_PIPE_STAGES);
    int pipes[MAX_PIPE_STAGES - 1][2];
    pid_t pids[MAX_PIPE_STAGES];
    int last_status = 0;
    int saved_in, saved_out;
    int i, j;

    if (n < 1)
        return 1;
    if (n == 1)
        return run_simple(stages[0]);

    for (i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            for (j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }
    saved_in  = dup(STDIN_FILENO);
    saved_out = dup(STDOUT_FILENO);

    for (i = 0; i < n; i++) {
        struct ast_node *cn = &ast_nodes[stages[i]];
        char *argv[LEX_MAX_TOKS + 1];
        int argc = 0;
        char resolved[256];
        int in_fd  = (i == 0)     ? saved_in  : pipes[i - 1][0];
        int out_fd = (i == n - 1) ? saved_out : pipes[i][1];
        pid_t pid;

        if (cn->type != AST_CMD) {
            pids[i] = -1;
            continue;
        }
        for (j = 0; j < cn->argv_n && argc < LEX_MAX_TOKS; j++)
            argv[argc++] = exp_words[cn->argv_first + j];
        argv[argc] = NULL;
        if (argc == 0) {
            pids[i] = -1;
            continue;
        }

        dup2(in_fd, STDIN_FILENO);
        dup2(out_fd, STDOUT_FILENO);

        if (resolve_cmd(argv[0], resolved, sizeof(resolved)) < 0) {
            printf("fresh: %s: command not found\r\n", argv[0]);
            pids[i] = -1;
            dup2(saved_in, STDIN_FILENO);
            dup2(saved_out, STDOUT_FILENO);
            continue;
        }

        pid = vfork();
        if (pid < 0) {
            perror("vfork");
            pids[i] = -1;
            dup2(saved_in, STDIN_FILENO);
            dup2(saved_out, STDOUT_FILENO);
            continue;
        }
        if (pid == 0) {
            int sv[3] = { -1, -1, -1 };
            for (j = 0; j < cn->redir_n; j++) {
                if (apply_redir(&ast_redirs[cn->redir_first + j], sv) < 0)
                    _exit(1);
            }
            execvp(resolved, argv);
            _exit(127);
        }
        pids[i] = pid;
        dup2(saved_in, STDIN_FILENO);
        dup2(saved_out, STDOUT_FILENO);
    }

    for (i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    close(saved_in);
    close(saved_out);

    for (i = 0; i < n; i++) {
        int st;
        if (pids[i] < 0)
            continue;
        while (waitpid(pids[i], &st, 0) < 0) {
            if (errno != EINTR)
                break;
        }
        last_status = st;
    }
    if (WIFEXITED(last_status))
        return WEXITSTATUS(last_status);
    if (WIFSIGNALED(last_status))
        return 128 + WTERMSIG(last_status);
    return 1;
}

static int exec_ast(int idx);

static int exec_ast(int idx)
{
    struct ast_node *n;
    int rc = 0;

    if (idx < 0)
        return 0;
    n = &ast_nodes[idx];

    /* Background: vfork the whole subtree, return 0 immediately. */
    if (n->flags & AST_FLAG_BG) {
        pid_t pid;
        ast_nodes[idx].flags &= ~AST_FLAG_BG;
        pid = vfork();
        if (pid < 0) {
            perror("vfork");
            ast_nodes[idx].flags |= AST_FLAG_BG;
            return 1;
        }
        if (pid == 0) {
            (void)exec_ast(idx);
            _exit(0);
        }
        ast_nodes[idx].flags |= AST_FLAG_BG;
        printf("[%d]\r\n", (int)pid);
        return 0;
    }

    switch (n->type) {
    case AST_CMD:
        rc = run_simple(idx);
        break;
    case AST_PIPE:
        rc = run_pipeline(idx);
        break;
    case AST_AND_OR: {
        int lrc = exec_ast(n->left);
        if ((n->flags & AST_FLAG_AND) && lrc != 0)
            rc = lrc;
        else if ((n->flags & AST_FLAG_OR) && lrc == 0)
            rc = lrc;
        else
            rc = exec_ast(n->right);
        break;
    }
    case AST_SEQ:
        (void)exec_ast(n->left);
        rc = exec_ast(n->right);
        break;
    default:
        rc = 1;
        break;
    }
    return rc;
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
    {
        int root = parse_tokens();
        int rc;
        if (root < 0) {
            printf("fresh: syntax error\r\n");
            return -1;
        }
        rc = exec_ast(root);
        return rc;
    }
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
                int root = parse_tokens();
                rc = (root < 0) ? 2 : exec_ast(root);
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
