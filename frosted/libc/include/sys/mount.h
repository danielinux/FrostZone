#ifndef SYS_MOUNT_INCLUDED
#define SYS_MOUNT_INCLUDED

int mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data);

int umount(const char *target);
int umount2(const char *target, int flags);

#endif
