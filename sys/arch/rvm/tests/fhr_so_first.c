int own = 5;
int scratch[20000];

int
exported(int x)
{
    return x + own + scratch[19999];
}

int
bump(void)
{
    return ++scratch[19999];
}
