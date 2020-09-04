# Kernel-User Proxy (KUP)
KUP is comprised of a FreeBSD kernel module, and a userspace library that provide multi-channel, zero-copy, syscall-free, two-way data transfer between the kernel and userspace. This is achieved by a pseudo device driver that injects some pages into the address space of the userspace process and the kernel, and then building a poll mode ping-pong style protocol over that shared memory region. KUP supports both blocking and non-blocking send/receive between kernel and userspace.

The KUP kernel module, exposes an API in the kernel sapce, that a kernel module can use to definea pseudo-device and the max number of channels on that device. Then a userspace process can use the KUP userspace lib to communicate with the kernel-side client.

# Example
You can take a look at the tests folder to see examples of how to use the kernel module and the usespace library. As a reference here is the KUP kernel API functions (see kupdev/kupdev.h)
```c
struct kupdev_softc *
kupdev_create(const char *name, size_t size, size_t chan_cnt);

int
kupdev_wait_channel(struct kupdev_softc *sc);

int
kupdev_unload(struct kupdev_softc* sc);

void
kupdev_notify(struct kupdev_softc *sc);

int
kupdev_send(struct kupdev_softc *sc, void *data, size_t len, int chan_id);

void*
kupdev_receive(struct kupdev_softc *sc, int chan_id);

void
kupdev_unlock_channel(struct kupdev_softc* sc, int chan_id);

void
kupdev_pass(struct kupdev_softc* sc, int chan_id);
```

And here is the userspace libkup.so API (see kuplib/kup.h):
```c
int kernproxy_errno;

void* kernproxy_open(char const *name);

void* kernproxy_channel(void* handle, size_t chan_id, size_t size);

void* kernproxy_receive(void *handle, int flags);

int kernproxy_send(void *handle, void *data, size_t len, int flags);

void kernproxy_close(void* handle);

int kernproxy_error(void* handle);
```
