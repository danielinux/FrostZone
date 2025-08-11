/* Frosted version of signals */

#ifndef SIGNAL_H_INCLUDED
#define SIGNAL_H_INCLUDED
#include <stdint.h>



#define SIGHUP      1   /* Hangup (POSIX).  */
#define SIGINT      2   /* Interrupt (ANSI).  */
#define SIGQUIT     3   /* Quit (POSIX).  */
#define SIGILL      4   /* Illegal instruction (ANSI).  */
#define SIGTRAP     5   /* Trace trap (POSIX).  */
#define SIGABRT     6   /* Abort (ANSI).  */
#define SIGIOT      6   /* IOT trap (4.2 BSD).  */
#define SIGBUS      7   /* BUS error (4.2 BSD).  */
#define SIGFPE      8   /* Floating-point exception (ANSI).  */
#define SIGKILL     9   /* Kill, unblockable (POSIX).  */
#define SIGUSR1     10  /* User-defined signal 1 (POSIX).  */
#define SIGSEGV     11  /* Segmentation violation (ANSI).  */
#define SIGUSR2     12  /* User-defined signal 2 (POSIX).  */
#define SIGPIPE     13  /* Broken pipe (POSIX).  */
#define SIGALRM     14  /* Alarm clock (POSIX).  */
#define SIGTERM     15  /* Termination (ANSI).  */
#define SIGSTKFLT   16  /* Stack fault.  */
#define SIGCLD      SIGCHLD /* Same as SIGCHLD (System V).  */
#define SIGCHLD     17  /* Child status has changed (POSIX).  */
#define SIGCONT     18  /* Continue (POSIX).  */
#define SIGSTOP     19  /* Stop, unblockable (POSIX).  */
#define SIGTSTP     20  /* Keyboard stop (POSIX).  */
#define SIGTTIN     21  /* Background read from tty (POSIX).  */
#define SIGTTOU     22  /* Background write to tty (POSIX).  */
#define SIGURG      23  /* Urgent condition on socket (4.2 BSD).  */
#define SIGXCPU     24  /* CPU limit exceeded (4.2 BSD).  */
#define SIGXFSZ     25  /* File size limit exceeded (4.2 BSD).  */
#define SIGVTALRM   26  /* Virtual alarm clock (4.2 BSD).  */
#define SIGPROF     27  /* Profiling alarm clock (4.2 BSD).  */
#define SIGWINCH    28  /* Window size change (4.3 BSD, Sun).  */
#define SIGPOLL     SIGIO   /* Pollable event occurred (System V).  */
#define SIGIO       29  /* I/O now possible (4.2 BSD).  */
#define SIGPWR      30  /* Power failure restart (System V).  */
#define SIGSYS      31  /* Bad system call.  */
#define SIGUNUSED   31
#define SIGMAX      32
#define _N_SIG      32
#define NSIG        _N_SIG


typedef uint32_t sigset_t;
#define SI_USER    1    /* Sent by a user. kill(), abort(), etc */
#define SI_QUEUE   2    /* Sent by sigqueue() */
#define SI_TIMER   3    /* Sent by expiration of a timer_settime() timer */
#define SI_ASYNCIO 4    /* Indicates completion of asycnhronous IO */
#define SI_MESGQ   5    /* Indicates arrival of a message at an empty queue */

#define SIG_DFL ((void (*)(int)) 0)
#define SIG_IGN ((void (*)(int)) 0xFFFFFFFF)

#define SA_NOCLDSTOP 1   		/* Do not generate SIGCHLD */
#define SA_SIGINFO   2   		/* Invoke the signal catching function */
#define SA_RESTART   0x10000000 	/* Restart syscall on signal return */
#define SA_ONSTACK   0x20000000		/* Call signal handler on alternate signal stack provided by sigaltstack(2). */
#define SA_NODEFER   0x40000000		/* Don't automatically block the signal when its handler is being executed  */
#define SA_RESETHAND 0x80000000		/* Reset to SIG_DFL on entry to handler */
#define SA_ONESHOT   SA_RESETHAND	/* Historical linux name */
#define SA_NOMASK    SA_NODEFER		/* Historical linux name */

union sigval {
  int    sival_int;    /* Integer signal value */
  void  *sival_ptr;    /* Pointer signal value */
};

typedef struct {
  int          si_signo;    /* Signal number */
  int          si_code;     /* Cause of the signal */
  union sigval si_value;    /* Signal value */
} siginfo_t;

struct sigevent {
  int              sigev_notify;               /* Notification type */
  int              sigev_signo;                /* Signal number */
  union sigval     sigev_value;                /* Signal value */
};

struct sigaction {
    void     (*sa_handler)(int);
    void     (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t   sa_mask;
    int        sa_flags;
    void     (*sa_restorer)(void);
};

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

int sigisemptyset(const sigset_t *set);
int sigorset(sigset_t *dest, const sigset_t *left, const sigset_t *right);
int sigandset(sigset_t *dest, const sigset_t *left, const sigset_t *right);

/* Process mask functions */
#define SIG_SETMASK 0	/* set mask with sigprocmask() */
#define SIG_BLOCK 1	/* set of signals to block */
#define SIG_UNBLOCK 2	/* set of signals to, well, unblock */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

/* Signal function */
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);

/* Kill function */
int kill(int pid, int sig);


 

#endif
