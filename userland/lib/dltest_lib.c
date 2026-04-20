static int dltest_counter;

int dltest_add(int lhs, int rhs)
{
    return lhs + rhs;
}

int dltest_magic(void)
{
    return 0x13572468;
}

int dltest_counter_next(void)
{
    dltest_counter++;
    return dltest_counter;
}
