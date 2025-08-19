#ifndef _FROSTED_FCNTL_H
#define _FROSTED_FCNTL_H

/* File access modes */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003

/* File creation flags */
#define O_CREAT     0x0200  /* Create file if it does not exist */
#define O_TRUNC     0x0400  /* Truncate flag */
#define O_EXCL      0x0800  /* Error if O_CREAT and the file exists */

#define O_NOCTTY    0x8000  /* Do not assign controlling terminal */

/* File status flags */
#define O_APPEND    0x0008  /* Writes append to end of file */
#define O_NONBLOCK  0x4000  /* Non-blocking mode */
#define O_SYNC      0x2000  /* Synchronous writes */
#define O_DSYNC     O_SYNC  /* Synchronous data writes */

/* Misc */
#define O_CLOEXEC   0x40000 /* Close on exec */
#define O_DIRECTORY 0x200000 /* Must be a directory */

#endif /* _FROSTED_FCNTL_H */
