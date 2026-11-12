#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    int (*first)(int), (*second)(void), (*bump)(void);
    int *value, *scratch;
    void *library = dlopen("/usr/lib/librvmtest.so", RTLD_NOW);
    if (!library) {
        printf("FHR DSO: dlopen failed: %s\n", dlerror());
        return 1;
    }
    first = (int (*)(int))dlsym(library, "exported");
    second = (int (*)(void))dlsym(library, "second");
    bump = (int (*)(void))dlsym(library, "bump");
    value = dlsym(library, "own");
    scratch = dlsym(library, "scratch");
    if (!first || !second || !bump || !value || !scratch ||
        first(7) != 12 || second() != 7 || *value != 5 ||
        scratch[19999] != 0) {
        printf("FHR DSO: symbol or relocation failed: %s\n", dlerror());
        return 2;
    }
    *value = 13;
    if (bump() != 1 || first(7) != 21 || second() != 15 ||
        scratch[19999] != 1 || dlclose(library) != 0) {
        printf("FHR DSO: writable data or dlclose failed: %s\n", dlerror());
        return 3;
    }
    if (dlopen("/usr/lib/librvmunresolved.so", RTLD_NOW) != NULL ||
        dlerror() == NULL) {
        puts("FHR DSO: unresolved import was accepted");
        return 4;
    }
    if (dlopen("/usr/lib/librvmmissing.so", RTLD_NOW) != NULL ||
        dlerror() == NULL) {
        puts("FHR DSO: missing dependency was accepted");
        return 5;
    }
    void *cycle = dlopen("/usr/lib/librvmcyclea.so", RTLD_NOW);
    const char *cycle_error = dlerror();
    if (cycle != NULL || !cycle_error || !strstr(cycle_error, "cyclic")) {
        puts("FHR DSO: cyclic dependency was accepted");
        return 6;
    }
    void *consumer_lib = dlopen("/usr/lib/librvmconsumer.so", RTLD_NOW);
    int (*consumer)(int) = consumer_lib ?
        (int (*)(int))dlsym(consumer_lib, "consumer") : NULL;
    if (!consumer_lib || !consumer || consumer(4) != 14 ||
        !dlsym(consumer_lib, "provider") ||
        dlsym(RTLD_DEFAULT, "provider") != NULL ||
        dlclose(consumer_lib) != 0) {
        printf("FHR DSO: automatic dependency failed: %s\n", dlerror());
        return 7;
    }
    (void)dlerror();
    void *provider_lib = dlopen("/usr/lib/librvmprovider.so", RTLD_NOW | RTLD_GLOBAL);
    consumer_lib = dlopen("/usr/lib/librvmunresolved.so", RTLD_NOW);
    consumer = consumer_lib ?
        (int (*)(int))dlsym(consumer_lib, "consumer") : NULL;
    if (!provider_lib || !consumer_lib || !consumer || consumer(4) != 14 ||
        !dlsym(RTLD_DEFAULT, "provider") ||
        dlclose(provider_lib) == 0 || dlclose(consumer_lib) != 0 ||
        dlclose(provider_lib) != 0) {
        printf("FHR DSO: import or dependency failed: %s\n", dlerror());
        return 8;
    }
    void *lifecycle_provider = dlopen("/usr/lib/librvmlifecycleprovider.so", RTLD_NOW);
    int *counter = lifecycle_provider ? dlsym(lifecycle_provider, "lifecycle_counter") : NULL;
    void *lifecycle = dlopen("/usr/lib/librvmlifecycle.so", RTLD_NOW);
    int (*lifecycle_value)(void) = lifecycle ? dlsym(lifecycle, "lifecycle_value") : NULL;
    if (!lifecycle_provider || !counter || !lifecycle || !lifecycle_value) {
        printf("FHR DSO: lifecycle load failed: %s\n", dlerror());
        return 9;
    }
    if (*counter != 1 || lifecycle_value() != 1) {
        printf("FHR DSO: constructor failed: counter=%d value=%d\n",
            *counter, lifecycle_value());
        return 9;
    }
    if (dlclose(lifecycle) != 0) {
        printf("FHR DSO: lifecycle close failed: %s\n", dlerror());
        return 9;
    }
    if (*counter != 11) {
        printf("FHR DSO: destructor failed: counter=%d\n", *counter);
        return 9;
    }
    if (dlclose(lifecycle_provider) != 0) {
        printf("FHR DSO: lifecycle provider close failed: %s\n", dlerror());
        return 9;
    }
    puts("FHR DSO: symbols, dependencies, constructors, destructors and dlclose verified");
    return 0;
}
