#ifndef SYS_PTRACE_H
#define SYS_PTRACE_H



enum __ptrace_request
{
    PTRACE_TRACEME = 0,
    PTRACE_PEEKTEXT = 1,
    PTRACE_PEEKDATA = 2,
    PTRACE_PEEKUSER = 3,
    PTRACE_POKETEXT = 4,
    PTRACE_POKEDATA = 5,
    PTRACE_POKEUSER = 6,
    PTRACE_CONT = 7,
    PTRACE_KILL = 8,
    PTRACE_SINGLESTEP = 9,
    /* */

    PTRACE_GETREGS = 12,
    PTRACE_SETREGS = 13,

    PTRACE_GETFPREGS = 14,
    PTRACE_SETFPREGS = 15,
    PTRACE_ATTACH = 16,
    PTRACE_DETACH = 17,

    /* */
    PTRACE_SYSCALL = 24,
};
    
  
int ptrace(enum __ptrace_request request, pid_t pid, void *data, void *addr);
#endif
