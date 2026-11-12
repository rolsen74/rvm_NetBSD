extern int lifecycle_counter;

__attribute__((constructor)) static void
library_init(void)
{
    lifecycle_counter++;
}

__attribute__((destructor)) static void
library_fini(void)
{
    lifecycle_counter += 10;
}

int
lifecycle_value(void)
{
    return lifecycle_counter;
}
