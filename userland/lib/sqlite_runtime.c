/*
 * sqlite_runtime.c - dlopen/dlsym trampolines for SQLite
 *
 * Mirrors the wolfSSL runtime resolver: dlopen() /bin/libsqlite.so once
 * and forward each public sqlite3_* call through dlsym(). Apps link
 * against the resulting archive (libsqlite_runtime.a) and get normal
 * link-time resolution of the sqlite3_* symbols without having to
 * embed SQLite statically.
 *
 * The wrappers use void-pointer and fundamental types to stay
 * decoupled from sqlite3.h. Pointer, int, int64 and double return
 * types are split across different X-macro tables so the ABI matches
 * the declarations in sqlite3.h at every call site.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define SQLITE_SO_PATH "/bin/libsqlite.so"

typedef long long sqlite_int64;
typedef unsigned long long sqlite_uint64;

static void *sqlite_handle;

int sqlite_runtime_init(void)
{
    if (sqlite_handle == NULL)
        sqlite_handle = dlopen(SQLITE_SO_PATH, RTLD_NOW | RTLD_LOCAL);

    return (sqlite_handle != NULL) ? 0 : -1;
}

static void *sqlite_resolve(const char *symbol)
{
    void *fn;

    if (sqlite_runtime_init() != 0)
        return NULL;

    fn = dlsym(sqlite_handle, symbol);
    if (fn == NULL && errno == 0)
        errno = ENOENT;
    return fn;
}

__attribute__((constructor))
static void sqlite_runtime_autoload(void)
{
    (void)sqlite_runtime_init();
}

/* ---- Wrapper macros --------------------------------------------------
 * Return-type variants are kept separate so the r0/r0r1/d0 ABI slot
 * matches sqlite3.h's declaration at the call site.
 */
#define DEFINE_SQLITE_INT(name, decl, call)          \
    int name decl                                    \
    {                                                \
        typedef int (*fn_t) decl;                    \
        static fn_t fn;                              \
        if (fn == NULL)                              \
            fn = (fn_t)sqlite_resolve(#name);        \
        if (fn == NULL)                              \
            return -1;                               \
        return fn call;                              \
    }

#define DEFINE_SQLITE_VOID(name, decl, call)         \
    void name decl                                   \
    {                                                \
        typedef void (*fn_t) decl;                   \
        static fn_t fn;                              \
        if (fn == NULL)                              \
            fn = (fn_t)sqlite_resolve(#name);        \
        if (fn == NULL)                              \
            return;                                  \
        fn call;                                     \
    }

#define DEFINE_SQLITE_INT64(name, decl, call)        \
    sqlite_int64 name decl                           \
    {                                                \
        typedef sqlite_int64 (*fn_t) decl;           \
        static fn_t fn;                              \
        if (fn == NULL)                              \
            fn = (fn_t)sqlite_resolve(#name);        \
        if (fn == NULL)                              \
            return 0;                                \
        return fn call;                              \
    }

#define DEFINE_SQLITE_DOUBLE(name, decl, call)       \
    double name decl                                 \
    {                                                \
        typedef double (*fn_t) decl;                 \
        static fn_t fn;                              \
        if (fn == NULL)                              \
            fn = (fn_t)sqlite_resolve(#name);        \
        if (fn == NULL)                              \
            return 0.0;                              \
        return fn call;                              \
    }

/* Any pointer-sized return (char*, sqlite3*, sqlite3_stmt*, unsigned char*,
 * void*, etc.) goes through the same ABI slot on ARMv8-M, so one macro
 * covers them all. */
#define DEFINE_SQLITE_PTR(name, decl, call)          \
    void *name decl                                  \
    {                                                \
        typedef void *(*fn_t) decl;                  \
        static fn_t fn;                              \
        if (fn == NULL)                              \
            fn = (fn_t)sqlite_resolve(#name);        \
        if (fn == NULL)                              \
            return NULL;                             \
        return fn call;                              \
    }

/* ---- sqlite3_* API surface ------------------------------------------ */

#define SQLITE_INT_APIS(X) \
    X(sqlite3_initialize, (void), ()) \
    X(sqlite3_shutdown, (void), ()) \
    X(sqlite3_os_init, (void), ()) \
    X(sqlite3_os_end, (void), ()) \
    X(sqlite3_threadsafe, (void), ()) \
    X(sqlite3_libversion_number, (void), ()) \
    X(sqlite3_open, (const char *filename, void **ppDb), (filename, ppDb)) \
    X(sqlite3_open_v2, (const char *filename, void **ppDb, int flags, const char *zVfs), (filename, ppDb, flags, zVfs)) \
    X(sqlite3_close, (void *db), (db)) \
    X(sqlite3_close_v2, (void *db), (db)) \
    X(sqlite3_exec, (void *db, const char *sql, int (*cb)(void*,int,char**,char**), void *cb_arg, char **errmsg), (db, sql, cb, cb_arg, errmsg)) \
    X(sqlite3_errcode, (void *db), (db)) \
    X(sqlite3_extended_errcode, (void *db), (db)) \
    X(sqlite3_extended_result_codes, (void *db, int onoff), (db, onoff)) \
    X(sqlite3_error_offset, (void *db), (db)) \
    X(sqlite3_prepare, (void *db, const char *sql, int nByte, void **ppStmt, const char **pzTail), (db, sql, nByte, ppStmt, pzTail)) \
    X(sqlite3_prepare_v2, (void *db, const char *sql, int nByte, void **ppStmt, const char **pzTail), (db, sql, nByte, ppStmt, pzTail)) \
    X(sqlite3_prepare_v3, (void *db, const char *sql, int nByte, unsigned int flags, void **ppStmt, const char **pzTail), (db, sql, nByte, flags, ppStmt, pzTail)) \
    X(sqlite3_step, (void *stmt), (stmt)) \
    X(sqlite3_reset, (void *stmt), (stmt)) \
    X(sqlite3_finalize, (void *stmt), (stmt)) \
    X(sqlite3_clear_bindings, (void *stmt), (stmt)) \
    X(sqlite3_stmt_busy, (void *stmt), (stmt)) \
    X(sqlite3_stmt_readonly, (void *stmt), (stmt)) \
    X(sqlite3_stmt_isexplain, (void *stmt), (stmt)) \
    X(sqlite3_stmt_status, (void *stmt, int op, int resetFlg), (stmt, op, resetFlg)) \
    X(sqlite3_column_count, (void *stmt), (stmt)) \
    X(sqlite3_column_type, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_int, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_bytes, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_bytes16, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_data_count, (void *stmt), (stmt)) \
    X(sqlite3_bind_null, (void *stmt, int i), (stmt, i)) \
    X(sqlite3_bind_int, (void *stmt, int i, int v), (stmt, i, v)) \
    X(sqlite3_bind_text, (void *stmt, int i, const char *v, int n, void (*d)(void*)), (stmt, i, v, n, d)) \
    X(sqlite3_bind_blob, (void *stmt, int i, const void *v, int n, void (*d)(void*)), (stmt, i, v, n, d)) \
    X(sqlite3_bind_zeroblob, (void *stmt, int i, int n), (stmt, i, n)) \
    X(sqlite3_bind_parameter_count, (void *stmt), (stmt)) \
    X(sqlite3_bind_parameter_index, (void *stmt, const char *name), (stmt, name)) \
    X(sqlite3_bind_value, (void *stmt, int i, const void *v), (stmt, i, v)) \
    X(sqlite3_changes, (void *db), (db)) \
    X(sqlite3_total_changes, (void *db), (db)) \
    X(sqlite3_get_autocommit, (void *db), (db)) \
    X(sqlite3_db_readonly, (void *db, const char *name), (db, name)) \
    X(sqlite3_db_status, (void *db, int op, int *pCur, int *pHiwtr, int resetFlg), (db, op, pCur, pHiwtr, resetFlg)) \
    X(sqlite3_status, (int op, int *pCur, int *pHiwtr, int resetFlg), (op, pCur, pHiwtr, resetFlg)) \
    X(sqlite3_complete, (const char *sql), (sql)) \
    X(sqlite3_busy_timeout, (void *db, int ms), (db, ms)) \
    X(sqlite3_busy_handler, (void *db, int (*cb)(void*,int), void *arg), (db, cb, arg)) \
    X(sqlite3_limit, (void *db, int id, int newVal), (db, id, newVal)) \
    X(sqlite3_config, (int op), (op)) \
    X(sqlite3_db_config, (void *db, int op), (db, op)) \
    X(sqlite3_enable_load_extension, (void *db, int onoff), (db, onoff)) \
    X(sqlite3_load_extension, (void *db, const char *file, const char *proc, char **errmsg), (db, file, proc, errmsg)) \
    X(sqlite3_create_function, (void *db, const char *name, int nArg, int eTextRep, void *pApp, void (*xFunc)(void*,int,void**), void (*xStep)(void*,int,void**), void (*xFinal)(void*)), (db, name, nArg, eTextRep, pApp, xFunc, xStep, xFinal)) \
    X(sqlite3_create_function_v2, (void *db, const char *name, int nArg, int eTextRep, void *pApp, void (*xFunc)(void*,int,void**), void (*xStep)(void*,int,void**), void (*xFinal)(void*), void (*xDestroy)(void*)), (db, name, nArg, eTextRep, pApp, xFunc, xStep, xFinal, xDestroy)) \
    X(sqlite3_create_collation, (void *db, const char *name, int eTextRep, void *pArg, int (*xCmp)(void*,int,const void*,int,const void*)), (db, name, eTextRep, pArg, xCmp)) \
    X(sqlite3_value_type, (const void *v), (v)) \
    X(sqlite3_value_numeric_type, (const void *v), (v)) \
    X(sqlite3_value_bytes, (const void *v), (v)) \
    X(sqlite3_value_int, (const void *v), (v)) \
    X(sqlite3_backup_step, (void *bk, int nPage), (bk, nPage)) \
    X(sqlite3_backup_finish, (void *bk), (bk)) \
    X(sqlite3_backup_remaining, (void *bk), (bk)) \
    X(sqlite3_backup_pagecount, (void *bk), (bk))

#define SQLITE_INT64_APIS(X) \
    X(sqlite3_last_insert_rowid, (void *db), (db)) \
    X(sqlite3_column_int64, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_value_int64, (const void *v), (v)) \
    X(sqlite3_memory_used, (void), ()) \
    X(sqlite3_memory_highwater, (int resetFlag), (resetFlag)) \
    X(sqlite3_soft_heap_limit64, (sqlite_int64 N), (N)) \
    X(sqlite3_hard_heap_limit64, (sqlite_int64 N), (N))

#define SQLITE_DOUBLE_APIS(X) \
    X(sqlite3_column_double, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_value_double, (const void *v), (v))

#define SQLITE_PTR_APIS(X) \
    X(sqlite3_libversion, (void), ()) \
    X(sqlite3_sourceid, (void), ()) \
    X(sqlite3_errmsg, (void *db), (db)) \
    X(sqlite3_errstr, (int code), (code)) \
    X(sqlite3_column_name, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_decltype, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_database_name, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_table_name, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_origin_name, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_text, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_blob, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_column_value, (void *stmt, int iCol), (stmt, iCol)) \
    X(sqlite3_value_text, (const void *v), (v)) \
    X(sqlite3_value_blob, (const void *v), (v)) \
    X(sqlite3_malloc, (int n), (n)) \
    X(sqlite3_malloc64, (sqlite_uint64 n), (n)) \
    X(sqlite3_realloc, (void *p, int n), (p, n)) \
    X(sqlite3_realloc64, (void *p, sqlite_uint64 n), (p, n)) \
    X(sqlite3_sql, (void *stmt), (stmt)) \
    X(sqlite3_expanded_sql, (void *stmt), (stmt)) \
    X(sqlite3_next_stmt, (void *db, void *stmt), (db, stmt)) \
    X(sqlite3_db_handle, (void *stmt), (stmt)) \
    X(sqlite3_db_filename, (void *db, const char *zDbName), (db, zDbName)) \
    X(sqlite3_db_name, (void *db, int N), (db, N)) \
    X(sqlite3_bind_parameter_name, (void *stmt, int i), (stmt, i)) \
    X(sqlite3_backup_init, (void *pDest, const char *zDestName, void *pSrc, const char *zSrcName), (pDest, zDestName, pSrc, zSrcName)) \
    X(sqlite3_context_db_handle, (void *ctx), (ctx)) \
    X(sqlite3_user_data, (void *ctx), (ctx)) \
    X(sqlite3_aggregate_context, (void *ctx, int nBytes), (ctx, nBytes))

#define SQLITE_VOID_APIS(X) \
    X(sqlite3_free, (void *p), (p)) \
    X(sqlite3_interrupt, (void *db), (db)) \
    X(sqlite3_progress_handler, (void *db, int nOps, int (*cb)(void*), void *arg), (db, nOps, cb, arg)) \
    X(sqlite3_result_null, (void *ctx), (ctx)) \
    X(sqlite3_result_int, (void *ctx, int v), (ctx, v)) \
    X(sqlite3_result_int64, (void *ctx, sqlite_int64 v), (ctx, v)) \
    X(sqlite3_result_double, (void *ctx, double v), (ctx, v)) \
    X(sqlite3_result_text, (void *ctx, const char *v, int n, void (*d)(void*)), (ctx, v, n, d)) \
    X(sqlite3_result_blob, (void *ctx, const void *v, int n, void (*d)(void*)), (ctx, v, n, d)) \
    X(sqlite3_result_error, (void *ctx, const char *msg, int n), (ctx, msg, n)) \
    X(sqlite3_result_error_code, (void *ctx, int code), (ctx, code)) \
    X(sqlite3_result_value, (void *ctx, const void *v), (ctx, v)) \
    X(sqlite3_result_zeroblob, (void *ctx, int n), (ctx, n))

SQLITE_INT_APIS(DEFINE_SQLITE_INT)
SQLITE_INT64_APIS(DEFINE_SQLITE_INT64)
SQLITE_DOUBLE_APIS(DEFINE_SQLITE_DOUBLE)
SQLITE_PTR_APIS(DEFINE_SQLITE_PTR)
SQLITE_VOID_APIS(DEFINE_SQLITE_VOID)

/* ---- Typed-specific bindings that take sqlite_int64 -------------------- */
int sqlite3_bind_int64(void *stmt, int i, sqlite_int64 v)
{
    typedef int (*fn_t)(void *, int, sqlite_int64);
    static fn_t fn;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite3_bind_int64");
    if (fn == NULL)
        return -1;
    return fn(stmt, i, v);
}

int sqlite3_bind_double(void *stmt, int i, double v)
{
    typedef int (*fn_t)(void *, int, double);
    static fn_t fn;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite3_bind_double");
    if (fn == NULL)
        return -1;
    return fn(stmt, i, v);
}

/* ---- Varargs forwarders ---------------------------------------------- */
void *sqlite3_mprintf(const char *fmt, ...)
{
    typedef void *(*fn_t)(const char *, va_list);
    static fn_t fn;
    va_list ap;
    void *ret;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite3_vmprintf");
    if (fn == NULL)
        return NULL;
    va_start(ap, fmt);
    ret = fn(fmt, ap);
    va_end(ap);
    return ret;
}

void *sqlite3_vmprintf(const char *fmt, va_list ap)
{
    typedef void *(*fn_t)(const char *, va_list);
    static fn_t fn;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite3_vmprintf");
    if (fn == NULL)
        return NULL;
    return fn(fmt, ap);
}

char *sqlite3_snprintf(int n, char *buf, const char *fmt, ...)
{
    typedef char *(*fn_t)(int, char *, const char *, va_list);
    static fn_t fn;
    va_list ap;
    char *ret;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite3_vsnprintf");
    if (fn == NULL)
        return buf;
    va_start(ap, fmt);
    ret = fn(n, buf, fmt, ap);
    va_end(ap);
    return ret;
}

char *sqlite3_vsnprintf(int n, char *buf, const char *fmt, va_list ap)
{
    typedef char *(*fn_t)(int, char *, const char *, va_list);
    static fn_t fn;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite3_vsnprintf");
    if (fn == NULL)
        return buf;
    return fn(n, buf, fmt, ap);
}

/* ---- SQLite shell CLI entry point ------------------------------------
 * libsqlite.so is built with shell.c's main() renamed to
 * sqlite_shell_main via -Dmain=sqlite_shell_main. The sqlite CLI
 * executable is a tiny stub that resolves and invokes it.
 */
int sqlite_shell_main(int argc, char **argv)
{
    typedef int (*fn_t)(int, char **);
    static fn_t fn;
    if (fn == NULL)
        fn = (fn_t)sqlite_resolve("sqlite_shell_main");
    if (fn == NULL)
        return -1;
    return fn(argc, argv);
}
