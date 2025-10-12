#define _GNU_SOURCE
#include <check.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Provide a controllable backing store for mempool.c */
static uint8_t *mempool_test_ptr = NULL;
static size_t mempool_map_size = 0;

#define __mempool_start__ (*mempool_test_ptr)

/* Include the supervisor sources directly so that static symbols are visible. */
#define memzero task_memzero
#include "../task.c"
#undef memzero
#include "../mempool.c"

#undef __mempool_start__

static void
mempool_fixture_setup(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    ck_assert_msg(page_size > 0, "sysconf(_SC_PAGESIZE) failed: %s", strerror(errno));

    mempool_map_size = (size_t)((MEMPOOL_SIZE + page_size - 1) / page_size) * (size_t)page_size;
    mempool_test_ptr = mmap(NULL, mempool_map_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ck_assert_msg(mempool_test_ptr != MAP_FAILED, "mmap failed: %s", strerror(errno));

    /* Reset supervisor bookkeeping */
    mempool_pool = NULL;
    memset(mempool_blocks, 0, sizeof(mempool_blocks));
    secure_task_table_init();

    mempool_init();
}

static void
mempool_fixture_teardown(void)
{
    if (mempool_test_ptr && mempool_test_ptr != MAP_FAILED) {
        munmap(mempool_test_ptr, mempool_map_size);
    }
    mempool_test_ptr = NULL;
    mempool_pool = NULL;
    mempool_map_size = 0;
}

START_TEST(test_mempool_initial_layout)
{
    ck_assert_ptr_eq(mempool_blocks[0].base, mempool_test_ptr);
    ck_assert_uint_eq(mempool_blocks[0].size, MEMPOOL_SIZE);
}
END_TEST

START_TEST(test_mempool_mmap_allocates_segment)
{
    const uint16_t task_id = 1;
    const size_t request_size = 4096;

    ck_assert_int_eq(register_secure_task(task_id, CAP_TASK, CONFIG_TASK_MAX_MEM), 0);
    secure_task_t *task = get_secure_task(task_id);
    ck_assert_ptr_nonnull(task);

    void *segment = mempool_mmap(request_size, task_id, 0);
    ck_assert_ptr_nonnull(segment);
    ck_assert_uint_eq(task->mempool_count, 1);
    ck_assert_ptr_eq(task->mempool[0].base, segment);
    ck_assert_uint_eq(task->mempool[0].size, request_size);
    ck_assert_uint_eq(task->limits.mem_used, request_size);
}
END_TEST

START_TEST(test_mempool_unmap_reclaims)
{
    const uint16_t task_id = 2;
    ck_assert_int_eq(register_secure_task(task_id, CAP_TASK, CONFIG_TASK_MAX_MEM), 0);

    void *segment = mempool_mmap(2048, task_id, 0);
    ck_assert_ptr_nonnull(segment);

    mempool_unmap(segment, task_id);

    secure_task_t *task = get_secure_task(task_id);
    ck_assert_ptr_nonnull(task);
    ck_assert_uint_eq(task->mempool_count, 0);
    ck_assert_uint_eq(task->limits.mem_used, 0);
    size_t free_total = 0;
    for (int i = 0; i < MAX_MEMPOOL_BLOCKS; ++i) {
        if (mempool_blocks[i].base != NULL)
            free_total += mempool_blocks[i].size;
    }
    ck_assert_uint_eq(free_total, MEMPOOL_SIZE);
}
END_TEST

START_TEST(test_mempool_alloc_stack_assigns_segment)
{
    const uint16_t task_id = 3;
    void *stack = mempool_alloc_stack(CONFIG_TASK_STACK_SIZE, task_id);
    ck_assert_ptr_nonnull(stack);

    secure_task_t *task = get_secure_task(task_id);
    ck_assert_ptr_nonnull(task);
    ck_assert_ptr_eq(task->stack_segment.base, stack);
    ck_assert_uint_eq(task->stack_segment.size, CONFIG_TASK_STACK_SIZE);
}
END_TEST

START_TEST(test_mempool_mmap_respects_limit)
{
    const uint16_t task_id = 4;
    ck_assert_int_eq(register_secure_task(task_id, CAP_TASK, 1024), 0);

    void *segment = mempool_mmap(2048, task_id, 0);
    ck_assert_ptr_null(segment);
}
END_TEST

START_TEST(test_mempool_alloc_stack_reuses_single_slot)
{
    const uint16_t task_id = 5;
    void *first = mempool_alloc_stack(CONFIG_TASK_STACK_SIZE, task_id);
    ck_assert_ptr_nonnull(first);

    secure_task_t *task = get_secure_task(task_id);
    ck_assert_ptr_nonnull(task);
    ck_assert_uint_eq(task->stack_segment.size, CONFIG_TASK_STACK_SIZE);

    void *second = mempool_alloc_stack(CONFIG_TASK_STACK_SIZE, task_id);
    ck_assert_ptr_nonnull(second);
    ck_assert_ptr_eq(task->stack_segment.base, second);
    ck_assert_uint_eq(task->stack_segment.size, CONFIG_TASK_STACK_SIZE);
    ck_assert_ptr_ne(second, first);
}
END_TEST

static Suite *
mempool_suite(void)
{
    Suite *s = suite_create("mempool");
    TCase *tc = tcase_create("core");

    tcase_add_checked_fixture(tc, mempool_fixture_setup, mempool_fixture_teardown);
    tcase_add_test(tc, test_mempool_initial_layout);
    tcase_add_test(tc, test_mempool_mmap_allocates_segment);
    tcase_add_test(tc, test_mempool_unmap_reclaims);
    tcase_add_test(tc, test_mempool_alloc_stack_assigns_segment);
    tcase_add_test(tc, test_mempool_mmap_respects_limit);
    tcase_add_test(tc, test_mempool_alloc_stack_reuses_single_slot);

    suite_add_tcase(s, tc);
    return s;
}

int
main(void)
{
    Suite *s = mempool_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_ENV);
    int failures = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failures == 0 ? 0 : 1;
}
