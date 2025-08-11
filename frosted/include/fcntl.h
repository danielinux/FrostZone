#ifndef _FROSTED_FCNTL_H
#define _FROSTED_FCNTL_H

/* File access modes */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003

/* File creation flags */
#define O_CREAT     0x0100  /* Create file if it does not exist */
#define O_EXCL      0x0200  /* Error if O_CREAT and the file exists */
#define O_NOCTTY    0x0400  /* Do not assign controlling terminal */
#define O_TRUNC     0x0800  /* Truncate flag */

/* File status flags */
#define O_APPEND    0x1000  /* Writes append to end of file */
#define O_NONBLOCK  0x2000  /* Non-blocking mode */
#define O_SYNC      0x4000  /* Synchronous writes */
#define O_DSYNC     0x8000  /* Synchronous data writes */

/* Misc */
#define O_DIRECTORY 0x10000 /* Must be a directory */
#define O_CLOEXEC   0x20000 /* Close on exec */

#endif /* _FROSTED_FCNTL_H */
