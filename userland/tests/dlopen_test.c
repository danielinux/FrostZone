#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>

#define DLTEST_PATH "/bin/libdltest.so"
#define DLTEST_MAGIC_VALUE 0x13572468

typedef int (*dltest_add_fn)(int lhs, int rhs);
typedef int (*dltest_magic_fn)(void);
typedef int (*dltest_counter_next_fn)(void);

static int fail_errno(const char *step)
{
    printf("dlopen_test: %s failed errno=%d\n", step, errno);
    return 1;
}

static int fail_value(const char *step, int actual, int expected)
{
    printf("dlopen_test: %s failed actual=%d expected=%d\n",
           step, actual, expected);
    return 1;
}

int main(void)
{
    void *handle;
    dltest_add_fn dltest_add;
    dltest_magic_fn dltest_magic;
    dltest_counter_next_fn dltest_counter_next;
    int value;

    handle = dlopen(DLTEST_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return fail_errno("dlopen");

    dltest_add = (dltest_add_fn)dlsym(handle, "dltest_add");
    if (!dltest_add)
        return fail_errno("dlsym dltest_add");

    dltest_magic = (dltest_magic_fn)dlsym(handle, "dltest_magic");
    if (!dltest_magic)
        return fail_errno("dlsym dltest_magic");

    dltest_counter_next = (dltest_counter_next_fn)dlsym(handle, "dltest_counter_next");
    if (!dltest_counter_next)
        return fail_errno("dlsym dltest_counter_next");

    errno = 0;
    if (dlsym(handle, "dltest_missing") != NULL) {
        printf("dlopen_test: missing symbol unexpectedly resolved\n");
        return 1;
    }
    if (errno != ENOENT)
        return fail_errno("dlsym missing symbol");

    value = dltest_add(7, 5);
    if (value != 12)
        return fail_value("dltest_add", value, 12);

    value = dltest_magic();
    if (value != DLTEST_MAGIC_VALUE)
        return fail_value("dltest_magic", value, DLTEST_MAGIC_VALUE);

    value = dltest_counter_next();
    if (value != 1)
        return fail_value("counter first call", value, 1);

    value = dltest_counter_next();
    if (value != 2)
        return fail_value("counter second call", value, 2);

    if (dlclose(handle) != 0)
        return fail_errno("dlclose first handle");

    handle = dlopen(DLTEST_PATH, RTLD_NOW);
    if (!handle)
        return fail_errno("dlopen reopen");

    dltest_counter_next = (dltest_counter_next_fn)dlsym(handle, "dltest_counter_next");
    if (!dltest_counter_next)
        return fail_errno("dlsym dltest_counter_next reopen");

    value = dltest_counter_next();
    if (value != 1)
        return fail_value("counter reset after reopen", value, 1);

    if (dlclose(handle) != 0)
        return fail_errno("dlclose reopen");

    printf("dlopen_test: ok (%s)\n", DLTEST_PATH);
    return 0;
}
