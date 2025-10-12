#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum op_type {
    OP_READ,
    OP_WRITE,
};

struct scenario {
    const char *name;
    enum op_type op;
    uintptr_t addr;
    uint32_t value;
    const char *description;
};

static const struct scenario scenarios[] = {
    {
        .name = "periph-read",
        .op = OP_READ,
        .addr = 0x40004800UL, /* USART3 base */
        .description = "Probe privileged peripheral window (expect MemManage fault)",
    },
    {
        .name = "periph-write",
        .op = OP_WRITE,
        .addr = 0x40004800UL,
        .value = 0xDEADBEEFu,
        .description = "Attempt peripheral write from user mode (expect MemManage fault)",
    },
    {
        .name = "secure-ram",
        .op = OP_READ,
        .addr = 0x30000000UL,
        .description = "Touch secure RAM alias (expect secure fault routed to kernel)",
    },
    {
        .name = "secure-flash",
        .op = OP_READ,
        .addr = 0x0C000000UL,
        .description = "Touch secure supervisor flash (expect secure fault)",
    },
    {
        .name = "ns-ram-ok",
        .op = OP_READ,
        .addr = 0x20010000UL,
        .description = "Read non-secure RAM (should succeed)",
    },
    {
        .name = "xip-ok",
        .op = OP_READ,
        .addr = 0x08040000UL,
        .description = "Read user XIP filesystem (should succeed)",
    },
};

static void usage(const char *prog)
{
    size_t i;

    printf("Usage: %s <scenario>|poke <addr> [value]\n", prog);
    printf("Scenarios:\n");
    for (i = 0; i < (sizeof(scenarios) / sizeof(scenarios[0])); i++) {
        printf("  %-12s %s\n", scenarios[i].name, scenarios[i].description);
    }
    printf("  poke          Directly access address (optional write)\n");
}

static int parse_ulong(const char *arg, uintptr_t *out)
{
    char *end = NULL;
    unsigned long result;

    errno = 0;
    result = strtoul(arg, &end, 0);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }

    *out = (uintptr_t)result;
    return 0;
}

static void perform_access(enum op_type op, uintptr_t addr, uint32_t value)
{
    volatile uint32_t *const ptr = (volatile uint32_t *)(addr);

    printf("Accessing 0x%08lx as %s\n", (unsigned long)addr,
           (op == OP_WRITE) ? "write" : "read");
    fflush(stdout);

    if (op == OP_WRITE) {
        *ptr = value;
        printf("Write completed (unexpected if address is privileged)\n");
    } else {
        uint32_t result = *ptr;
        printf("Read completed: 0x%08lx\n", (unsigned long)result);
    }
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    size_t i;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "poke") == 0) {
        uintptr_t addr;
        uint32_t value = 0U;
        enum op_type op = OP_READ;

        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }

        if (parse_ulong(argv[2], &addr) != 0) {
            printf("Invalid address: %s\n", argv[2]);
            return 1;
        }

        if (argc > 3) {
            uintptr_t tmp;
            if (parse_ulong(argv[3], &tmp) != 0) {
                printf("Invalid value: %s\n", argv[3]);
                return 1;
            }
            value = (uint32_t)tmp;
            op = OP_WRITE;
        }

        printf("poke: %s 0x%08lx%s\n", (op == OP_WRITE) ? "write" : "read",
               (unsigned long)addr,
               (op == OP_WRITE) ? " <value provided>" : "");
        perform_access(op, addr, value);
        return 0;
    }

    for (i = 0; i < (sizeof(scenarios) / sizeof(scenarios[0])); i++) {
        if (strcmp(argv[1], scenarios[i].name) == 0) {
            printf("Scenario '%s': %s\n", scenarios[i].name, scenarios[i].description);
            perform_access(scenarios[i].op, scenarios[i].addr, scenarios[i].value);
            return 0;
        }
    }

    printf("Unknown scenario '%s'\n", argv[1]);
    usage(argv[0]);
    return 1;
}
