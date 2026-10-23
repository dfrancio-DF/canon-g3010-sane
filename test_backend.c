#include <sane/sane.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    void *so;
    SANE_Int version = 0;
    SANE_Status (*sane_init_fn)(SANE_Int *, SANE_Auth_Callback);
    SANE_Status (*sane_get_devices_fn)(const SANE_Device ***, SANE_Bool);
    SANE_Status (*sane_open_fn)(SANE_String_Const, SANE_Handle *);
    void (*sane_close_fn)(SANE_Handle);
    void (*sane_exit_fn)(void);
    const SANE_Device **devices = NULL;
    SANE_Handle handle = NULL;
    SANE_Status status;
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s backend.so\n", argv[0]);
        return 2;
    }
    so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!so) { fprintf(stderr, "%s\n", dlerror()); return 2; }
#define LOAD(name) do { *(void **) (&sane_##name##_fn) = \
    dlsym(so, "sane_canon_chmp_" #name); \
    if (!sane_##name##_fn) { fprintf(stderr, "missing backend symbol %s\n", #name); return 2; } } while (0)
    LOAD(init);
    LOAD(get_devices);
    LOAD(open);
    LOAD(close);
    LOAD(exit);
    status = sane_init_fn(&version, NULL);
    if (status != SANE_STATUS_GOOD) { fprintf(stderr, "sane_init: %d\n", status); return 1; }
    status = sane_get_devices_fn(&devices, SANE_FALSE);
    if (status != SANE_STATUS_GOOD) { fprintf(stderr, "sane_get_devices: %d\n", status); return 1; }
    for (i = 0; devices && devices[i]; ++i) {
        printf("%s | %s | %s\n", devices[i]->name, devices[i]->vendor, devices[i]->model);
        status = sane_open_fn(devices[i]->name, &handle);
        if (status != SANE_STATUS_GOOD) { fprintf(stderr, "sane_open: %d\n", status); return 1; }
        sane_close_fn(handle);
    }
    sane_exit_fn();
    dlclose(so);
    return i ? 0 : 1;
}
