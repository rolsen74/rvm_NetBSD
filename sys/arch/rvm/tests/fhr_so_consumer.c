extern int provider(int);

int
consumer(int x)
{
    return provider(x) * 2;
}
