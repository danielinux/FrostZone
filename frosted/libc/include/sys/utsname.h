#ifndef UTSNAME_DEFINED
#define UTSNAME_DEFINED
struct utsname {
    char sysname[16];    /* Operating system name (e.g., "Frosted") */
    char nodename[16];   /* Name within network */
    char release[16];    /* Operating system release (e.g., "16.03") */
    char version[16];    /* Operating system version (e.g., "16") */
    char machine[16];    /* Hardware identifier */
    char domainname[16]; /* NIS or YP domain name */
};
#endif
