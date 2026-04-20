#ifndef _FROSTED_DLFCN_H
#define _FROSTED_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LOCAL 0x0
#define RTLD_LAZY  0x1
#define RTLD_NOW   0x2

void *dlopen(const char *path, int mode);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);

#ifdef __cplusplus
}
#endif

#endif
