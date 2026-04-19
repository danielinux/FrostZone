#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/frosted.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern int syscall(uint32_t nr, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

static int fail(const char *step)
{
    fprintf(stderr, "phase0_memfs: %s errno=%d\n", step, errno);
    return 1;
}

static int contains_text(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

static int run_ls(const char *path, const char *capture_path, const char *expected)
{
    int status;
    int fd;
    int rfd;
    pid_t pid;
    char buf[2048];
    ssize_t got;
    ssize_t total = 0;
    char *const argv[] = { "ls", (char *)path, NULL };

    fd = open(capture_path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd < 0)
        return fail("open ls capture");

    pid = vfork();
    if (pid < 0) {
        close(fd);
        return fail("vfork ls");
    }
    if (pid == 0) {
        if (fd != STDOUT_FILENO) {
            dup2(fd, STDOUT_FILENO);
        }
        if (fd != STDERR_FILENO) {
            dup2(fd, STDERR_FILENO);
        }
        if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
            close(fd);
        }
        execve("/bin/ls", argv, NULL);
        _exit(127);
    }
    close(fd);

    if (waitpid(pid, &status, 0) < 0)
        return fail("waitpid ls");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "phase0_memfs: ls %s exit=%d\n", path, status);
        return 1;
    }
    rfd = open(capture_path, O_RDONLY, 0);
    if (rfd < 0)
        return fail("open ls capture read");
    while (total < (ssize_t)(sizeof(buf) - 1)) {
        got = read(rfd, buf + total, sizeof(buf) - 1 - total);
        if (got < 0) {
            close(rfd);
            return fail("read ls capture");
        }
        if (got == 0)
            break;
        total += got;
    }
    buf[total] = '\0';
    close(rfd);

    if (!contains_text(buf, expected)) {
        fprintf(stderr, "phase0_memfs: ls %s missing %s\n", path, expected);
        return 1;
    }
    return 0;
}

static int run_cmd(const char *prog, char *const argv[])
{
    int status;
    pid_t pid;

    pid = vfork();
    if (pid < 0)
        return fail("vfork command");
    if (pid == 0) {
        execve(prog, argv, NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return fail("waitpid command");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "phase0_memfs: %s exit=%d\n", prog, status);
        return 1;
    }
    return 0;
}

static int phase0_mkdir(const char *path, unsigned long mode)
{
    return syscall(SYS_MKDIR, (uint32_t)path, (uint32_t)mode, 0, 0, 0);
}

static int read_all(const char *path, char *buf, size_t len)
{
    int fd;
    ssize_t got;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return fail("open read");
    got = read(fd, buf, len - 1);
    if (got < 0) {
        close(fd);
        return fail("read");
    }
    buf[got] = '\0';
    close(fd);
    return 0;
}

static int verify_tmp_mtab(void)
{
    char buf[512];
    int fd;
    ssize_t got;

    fd = open("/sys/mtab", O_RDONLY, 0);
    if (fd < 0)
        return fail("open /sys/mtab");
    got = read(fd, buf, sizeof(buf) - 1);
    if (got < 0) {
        close(fd);
        return fail("read /sys/mtab");
    }
    buf[got] = '\0';
    close(fd);

    if (!strstr(buf, "/tmp") || !strstr(buf, "memfs")) {
        fprintf(stderr, "phase0_memfs: /tmp memfs mount missing\n");
        return 1;
    }
    return 0;
}

static void cleanup_best_effort(void)
{
    unlink("/tmp/phase0_a/phase0_b/note.txt");
    unlink("/tmp/phase0_a/phase0_b");
    unlink("/tmp/phase0_a");
}

int main(void)
{
    static const char payload[] = "phase0 memfs payload\n";
    char buf[64];
    int fd;

    if (verify_tmp_mtab() != 0)
        return 1;

    /* Verify vfork+exec works (ls runs and exits 0) */
    if (run_cmd("/bin/ls", (char *const[]){ "ls", "/bin", NULL }) != 0)
        return 1;

    if (phase0_mkdir("/tmp/phase0_a", 0777) < 0)
        return fail("mkdir /tmp/phase0_a");
    if (phase0_mkdir("/tmp/phase0_a/phase0_b", 0777) < 0)
        return fail("mkdir /tmp/phase0_a/phase0_b");

    if (chdir("/tmp/phase0_a/phase0_b") < 0)
        return fail("chdir /tmp/phase0_a/phase0_b");

    fd = open("note.txt", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd < 0)
        return fail("open note.txt write");
    if (write(fd, payload, sizeof(payload) - 1) != (ssize_t)(sizeof(payload) - 1)) {
        close(fd);
        return fail("write note.txt");
    }
    close(fd);

    if (read_all("note.txt", buf, sizeof(buf)) != 0)
        return 1;
    if (strcmp(buf, payload) != 0) {
        fprintf(stderr, "phase0_memfs: readback mismatch\n");
        return 1;
    }

    if (unlink("note.txt") < 0)
        return fail("unlink note.txt");

    if (chdir("/") < 0)
        return fail("chdir /");

    if (unlink("/tmp/phase0_a/phase0_b") < 0)
        return fail("unlink /tmp/phase0_a/phase0_b");
    if (unlink("/tmp/phase0_a") < 0)
        return fail("unlink /tmp/phase0_a");

    /* Test output capture: ls /bin > /tmp/ls_out via dup2 */
    if (run_ls("/bin", "/tmp/ls_out", "ls") != 0)
        return 1;

    if (unlink("/tmp/ls_out") < 0)
        return fail("unlink /tmp/ls_out");

    __asm volatile ("bkpt #0x43");
    return 0;
}
