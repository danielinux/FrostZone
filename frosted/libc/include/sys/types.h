/* unified sys/types.h: 
   start with sef's sysvi386 version.
   merge go32 version -- a few ifdefs.
   h8300hms, h8300xray, and sysvnecv70 disagree on the following types:

   typedef int gid_t;
   typedef int uid_t;
   typedef int dev_t;
   typedef int ino_t;
   typedef int mode_t;
   typedef int caddr_t;

   however, these aren't "reasonable" values, the sysvi386 ones make far 
   more sense, and should work sufficiently well (in particular, h8300 
   doesn't have a stat, and the necv70 doesn't matter.) -- eichin
 */

#ifndef _SYS_TYPES_H

#include <_ansi.h>
# include <sys/cdefs.h>

#ifndef __INTTYPES_DEFINED__
#define __INTTYPES_DEFINED__

#include <machine/_types.h>

#endif /* ! __INTTYPES_DEFINED */

#ifndef __need_inttypes

#define _SYS_TYPES_H
#include <sys/_types.h>
#include <sys/_stdint.h>

# include <stddef.h>
# include <machine/types.h>

/* To ensure the stat struct's layout doesn't change when sizeof(int), etc.
   changes, we assume sizeof short and long never change and have all types
   used to define struct stat use them and not int where possible.
   Where not possible, _ST_INTxx are used.  It would be preferable to not have
   such assumptions, but until the extra fluff is necessary, it's avoided.
   No 64 bit targets use stat yet.  What to do about them is postponed
   until necessary.  */
#ifdef __GNUC__
#define _ST_INT32 __attribute__ ((__mode__ (__SI__)))
#else
#define _ST_INT32
#endif

# if	__BSD_VISIBLE

#  define	physadr		physadr_t
#  define	quad		quad_t

#ifndef _BSDTYPES_DEFINED
/* also defined in mingw/gmon.h and in w32api/winsock[2].h */
#ifndef __u_char_defined
typedef	unsigned char	u_char;
#define __u_char_defined
#endif
#ifndef __u_short_defined
typedef	unsigned short	u_short;
#define __u_short_defined
#endif
#ifndef __u_int_defined
typedef	unsigned int	u_int;
#define __u_int_defined
#endif
#ifndef __u_long_defined
typedef	unsigned long	u_long;
#define __u_long_defined
#endif
#define _BSDTYPES_DEFINED
#endif

typedef	unsigned short	ushort;		/* System V compatibility */
typedef	unsigned int	uint;		/* System V compatibility */
typedef	unsigned long	ulong;		/* System V compatibility */
# endif	/*__BSD_VISIBLE */

#ifndef __clock_t_defined
typedef _CLOCK_T_ clock_t;
#define __clock_t_defined
#endif

#ifndef __time_t_defined
typedef _TIME_T_ time_t;
#define __time_t_defined
#endif

#ifndef __daddr_t_defined
typedef	long	daddr_t;
#define __daddr_t_defined
#endif
#ifndef __caddr_t_defined
typedef	char *	caddr_t;
#define __caddr_t_defined
#endif

typedef	unsigned short	ino_t;

/*
 * All these should be machine specific - right now they are all broken.
 * However, for all of Cygnus' embedded targets, we want them to all be
 * the same.  Otherwise things like sizeof (struct stat) might depend on
 * how the file was compiled (e.g. -mint16 vs -mint32, etc.).
 */

typedef _off_t	off_t;
typedef __dev_t dev_t;
typedef __uid_t uid_t;
typedef __gid_t gid_t;

typedef int pid_t;

typedef	long key_t;
typedef _ssize_t ssize_t;

typedef unsigned int mode_t _ST_INT32;

typedef unsigned short nlink_t;

#undef __MS_types__
#undef _ST_INT32


#ifndef __clockid_t_defined
typedef _CLOCKID_T_ clockid_t;
#define __clockid_t_defined
#endif

#ifndef __timer_t_defined
typedef _TIMER_T_ timer_t;
#define __timer_t_defined
#endif

typedef unsigned long useconds_t;

#ifndef _SUSECONDS_T_DECLARED
typedef	__suseconds_t	suseconds_t;
#define	_SUSECONDS_T_DECLARED
#endif

typedef	__int64_t	sbintime_t;

#include <sys/features.h>


/* Cygwin will probably never have full posix compliance due to little things
 * like an inability to set the stackaddress. Cygwin is also using void *  
 * pointers rather than structs to ensure maximum binary compatability with
 * previous releases.
 * This means that we don't use the types defined here, but rather in
 * <cygwin/types.h>
 */
#if defined(_POSIX_THREADS) && !defined(__CYGWIN__)

#include <sys/sched.h>

/*
 *  2.5 Primitive System Data Types,  P1003.1c/D10, p. 19.
 */


/* P1003.1c/D10, p. 118-119 */
#define PTHREAD_SCOPE_PROCESS 0
#define PTHREAD_SCOPE_SYSTEM  1

/* P1003.1c/D10, p. 111 */
#define PTHREAD_INHERIT_SCHED  1      /* scheduling policy and associated */
                                      /*   attributes are inherited from */
                                      /*   the calling thread. */
#define PTHREAD_EXPLICIT_SCHED 2      /* set from provided attribute object */

/* P1003.1c/D10, p. 141 */
#define PTHREAD_CREATE_DETACHED 0
#define PTHREAD_CREATE_JOINABLE  1


#if defined(_POSIX_THREAD_PROCESS_SHARED)
/* NOTE: P1003.1c/D10, p. 81 defines following values for process_shared.  */

#define PTHREAD_PROCESS_PRIVATE 0 /* visible within only the creating process */
#define PTHREAD_PROCESS_SHARED  1 /* visible too all processes with access to */
                                  /*   the memory where the resource is */
                                  /*   located */
#endif

#if defined(_POSIX_THREAD_PRIO_PROTECT)
/* Mutexes */

/* Values for blocking protocol. */

#define PTHREAD_PRIO_NONE    0
#define PTHREAD_PRIO_INHERIT 1
#define PTHREAD_PRIO_PROTECT 2
#endif

#if defined(_UNIX98_THREAD_MUTEX_ATTRIBUTES)

/* Values for mutex type */

/* The following defines are part of the X/Open System Interface (XSI). */

/*
 * This type of mutex does not detect deadlock. A thread attempting to
 * relock this mutex without first unlocking it shall deadlock. Attempting
 * to unlock a mutex locked by a different thread results in undefined
 * behavior.  Attempting to unlock an unlocked mutex results in undefined
 * behavior.
 */
#define PTHREAD_MUTEX_NORMAL     0

/*
 * A thread attempting to relock this mutex without first unlocking
 * it shall succeed in locking the mutex.  The relocking deadlock which
 * can occur with mutexes of type PTHREAD_MUTEX_NORMAL cannot occur with
 * this type of mutex.  Multiple locks of this mutex shall require the
 * same number of unlocks to release the mutex before another thread can
 * acquire the mutex. A thread attempting to unlock a mutex which another
 * thread has locked shall return with an error.  A thread attempting to
 * unlock an unlocked mutex shall return with an error.
 */
#define PTHREAD_MUTEX_RECURSIVE  1

/* 
 * This type of mutex provides error checking. A thread attempting
 * to relock this mutex without first unlocking it shall return with an
 * error. A thread attempting to unlock a mutex which another thread has
 * locked shall return with an error. A thread attempting to unlock an
 * unlocked mutex shall return with an error.
 */
#define PTHREAD_MUTEX_ERRORCHECK 2

/*
 * Attempting to recursively lock a mutex of this type results
 * in undefined behavior. Attempting to unlock a mutex of this type
 * which was not locked by the calling thread results in undefined
 * behavior. Attempting to unlock a mutex of this type which is not locked
 * results in undefined behavior. An implementation may map this mutex to
 * one of the other mutex types.
 */
#define PTHREAD_MUTEX_DEFAULT    3

#endif /* !defined(_UNIX98_THREAD_MUTEX_ATTRIBUTES) */

#endif /* __CYGWIN__ */

#endif  /* !__need_inttypes */

#undef __need_inttypes

#endif	/* _SYS_TYPES_H */
