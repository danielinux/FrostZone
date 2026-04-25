/*
 *      Frosted strace — minimal Linux-style syscall tracer.
 *
 *      Usage: strace <command> [args...]
 *
 *      How it works:
 *        fork/vfork + PTRACE_TRACEME + execvp in the child;
 *        parent enables tracing with ptrace(PTRACE_SYSCALL, pid, ...);
 *        kernel appends a record per completed syscall (nr, args, retval,
 *        PC) to a per-task ring; parent drains the ring with
 *        ptrace(PTRACE_GET_SYSCALL_INFO, ...) in a loop and prints.
 *
 *      Ring is non-blocking: tracee runs at full speed. If the parent
 *      falls behind and the ring fills, oldest events are dropped
 *      and the lost counter is reported on next drain / at exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

/* libgloss ships sys_sched_yield but not sched_yield; call the raw syscall
 * wrapper directly to avoid pulling in <sched.h> (which isn't linked). */
extern int sys_sched_yield(void);

/* Mirror subset of frosted/scheduler.c's ptrace enum — just what we use. */
enum {
    PTRACE_TRACEME          = 0,
    PTRACE_CONT             = 7,
    PTRACE_KILL             = 8,
    PTRACE_GETREGS          = 12,
    PTRACE_DETACH           = 17,
    PTRACE_SYSCALL          = 24,
    PTRACE_GET_SYSCALL_INFO = 25,
};

/* Must match struct strace_event in frosted/scheduler.c bit-for-bit. */
struct strace_event {
    uint32_t nr;
    uint32_t args[5];
    int32_t  retval;
    uint32_t pc;
};

extern int ptrace(int request, pid_t pid, void *addr, void *data);

static const char *syscall_name(uint32_t nr)
{
    static const char * const names[] = {
        "sleep",            "ptsname",          "getpid",         "getppid",
        "open",             "close",            "read",           "write",
        "seek",             "mkdir",            "unlink",         "mmap",
        "munmap",           "opendir",          "readdir",        "closedir",
        "stat",             "poll",             "ioctl",          "link",
        "chdir",            "getcwd",           "sem_init",       "sem_post",
        "sem_wait",         "sem_trywait",      "sem_destroy",    "mutex_init",
        "mutex_unlock",     "mutex_lock",       "mutex_destroy",  "socket",
        "bind",             "accept",           "connect",        "listen",
        "sendto",           "recvfrom",         "setsockopt",     "getsockopt",
        "shutdown",         "dup",              "dup2",           "mount",
        "umount",           "kill",             "isatty",         "exec",
        "ttyname_r",        "exit",             "tcsetattr",      "tcgetattr",
        "tcsendbreak",      "pipe2",            "sigaction",      "sigprocmask",
        "sigsuspend",       "vfork",            "waitpid",        "lstat",
        "uname",            "getaddrinfo",      "freeaddrinfo",   "fstat",
        "getsockname",      "getpeername",      "readlink",       "fcntl",
        "setsid",           "ptrace",           "reboot",         "getpriority",
        "setpriority",      "ftruncate",        "truncate",       "pthread_create",
        "pthread_exit",     "pthread_join",     "pthread_detach", "pthread_cancel",
        "pthread_self",     "pthread_setcancelstate", "sched_yield",
        "pthread_mutex_init",
        "pthread_mutex_destroy", "pthread_mutex_lock", "pthread_mutex_trylock",
        "pthread_mutex_unlock",
        "pthread_kill",     "clock_settime",    "clock_gettime",
        "pthread_key_create", "pthread_setspecific", "pthread_getspecific",
        "alarm",            "ualarm",           "dlopen",         "dlsym",
        "dlclose",          "sendmsg",          "recvmsg",
    };
    if (nr < sizeof(names) / sizeof(names[0]))
        return names[nr];
    return "<unknown>";
}

/* Arg arity per syscall — mirrors the second column of syscall_table_gen.py.
 * Controls how many of the 5 captured args to print. */
static int syscall_arity(uint32_t nr)
{
    static const uint8_t arity[] = {
        2,3,0,0,3,1,3,3,3,2,   /* 0..9   */
        1,3,2,1,2,1,2,3,3,2,   /* 10..19 */
        1,2,2,1,2,1,1,0,1,1,   /* 20..29 */
        1,3,2,2,2,2,5,5,5,5,   /* 30..39 */
        2,1,2,5,2,2,1,2,3,1,   /* 40..49 */
        3,2,2,2,3,3,1,0,3,2,   /* 50..59 */
        1,4,1,2,2,2,3,3,0,4,   /* 60..69 */
        3,2,3,2,2,4,1,2,1,1,   /* 70..79 */
        0,2,0,2,1,1,1,1,2,2,   /* 80..89 */
        2,2,2,2,1,2,2,2,1,3,   /* 90..99 */
        3,                      /* 100    */
    };
    if (nr < sizeof(arity) / sizeof(arity[0]))
        return arity[nr];
    return 0;
}

static void print_event(const struct strace_event *ev)
{
    int n = syscall_arity(ev->nr);
    int i;
    fprintf(stderr, "%-16s(", syscall_name(ev->nr));
    for (i = 0; i < n; i++) {
        if (i > 0)
            fprintf(stderr, ", ");
        fprintf(stderr, "0x%x", (unsigned)ev->args[i]);
    }
    fprintf(stderr, ") = %d", (int)ev->retval);
    if (ev->retval < 0 && ev->retval > -200)
        fprintf(stderr, " (%s)", strerror(-ev->retval));
    fprintf(stderr, "  @pc=0x%08x\n", (unsigned)ev->pc);
}

static volatile int child_exited = 0;
static volatile int child_status = 0;
static pid_t child_pid_g = -1;

static void on_sigchld(int sig)
{
    int st;
    pid_t r;
    (void)sig;
    r = waitpid(child_pid_g, &st, WNOHANG);
    if (r == child_pid_g) {
        child_status = st;
        child_exited = 1;
    }
}

static int drain_events(pid_t pid)
{
    struct strace_event ev;
    int r;
    int n = 0;
    for (;;) {
        r = ptrace(PTRACE_GET_SYSCALL_INFO, pid, 0, &ev);
        if (r != 0)
            break;           /* 1 = ring empty, <0 = gone */
        print_event(&ev);
        n++;
    }
    return n;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <command> [args...]\n", prog);
}

static int resolve_target(const char *cmd, char *resolved, size_t resolved_len)
{
    struct stat st;

    if (!cmd || !resolved || resolved_len == 0)
        return -1;

    if (strchr(cmd, '/')) {
        strncpy(resolved, cmd, resolved_len - 1);
        resolved[resolved_len - 1] = '\0';
    } else {
        snprintf(resolved, resolved_len, "/bin/%s", cmd);
    }

    if (stat(resolved, &st) < 0)
        return -1;
    return 0;
}

#ifndef APP_STRACE_MODULE
int main(int argc, char *argv[])
#else
int icebox_strace(int argc, char *argv[])
#endif
{
    struct sigaction sa;
    pid_t pid;
    char resolved[64];

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (resolve_target(argv[1], resolved, sizeof(resolved)) < 0) {
        fprintf(stderr, "strace: cannot stat '%s': %s\n",
                argv[1], strerror(errno));
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigchld;
    sigaction(SIGCHLD, &sa, NULL);

    pid = vfork();
    if (pid < 0) {
        perror("vfork");
        return 1;
    }
    if (pid == 0) {
        /* Child: enroll as tracee, then exec the target. Pass argv+1
         * directly (no heap alloc — Frosted vfork doesn't preserve
         * parent-heap pointers in the child's address space). */
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execvp(resolved, &argv[1]);
        _exit(127);
    }

    child_pid_g = pid;

    /* Enable per-syscall tracing on the tracee. */
    if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) < 0) {
        fprintf(stderr, "strace: ptrace(SYSCALL) failed\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    /* Drain events until the child exits. sched_yield between reads so
     * the tracee gets CPU. No idle timeout, no kill(pid, 0) liveness
     * check: Frosted's sys_kill_hdlr does not special-case sig 0
     * (SIG_DFL falls through to task_terminate), so the previous
     * "liveness" probe was actually killing every traced child on the
     * first poll iteration. Trust SIGCHLD (which sets child_exited)
     * to terminate the loop. */
    while (!child_exited) {
        drain_events(pid);
        sys_sched_yield();
    }
    drain_events(pid);

    if (WIFEXITED(child_status))
        fprintf(stderr, "+++ exited with %d +++\n", WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        fprintf(stderr, "+++ killed by signal %d +++\n", WTERMSIG(child_status));

    return WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 1;
}
