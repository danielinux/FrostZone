/*
 * sqlite - SQLite command-line shell launcher
 *
 * The upstream shell.c (with main() renamed via -Dmain=sqlite_shell_main)
 * lives inside /bin/libsqlite.so. This tiny CLI stub just forwards argv
 * through the dlopen/dlsym trampoline in libsqlite_runtime.a so the
 * heavy shell logic is shared with any other app that also links the
 * runtime resolver.
 */

extern int sqlite_shell_main(int argc, char **argv);

int main(int argc, char **argv)
{
    return sqlite_shell_main(argc, argv);
}
