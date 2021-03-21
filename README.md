# Problem Statement
You want to create an __efficient__ communication channel between a kernel module and a userspace process.

# Kernel-User Proxy (KUP)
KUP is comprised of a FreeBSD kernel module, and a userspace library that provide multi-channel, zero-copy, syscall-free, two-way data transfer between the kernel and userspace. This is achieved by a pseudo device driver that injects some pages into the address space of the userspace process and the kernel, and then building a poll mode ping-pong style protocol over that shared memory region. KUP supports both blocking and non-blocking send/receive between kernel and userspace.

The KUP kernel module, exposes an API in the kernel sapce, that a kernel module can use to definea pseudo-device and the max number of channels on that device. Then a userspace process can use the KUP userspace lib to communicate with the kernel-side client.

# Architecture
Using the KUP kernel module, a kernel module can define a communication medium with one or more channels which can later be used by userland processes to send/receive data to that kernel module.

On the userland side, processes can employ libkup, to connect to existing KUP communication channels to send/receive data.

Separate channels can be used in parallel by multiple threads.

![image](https://user-images.githubusercontent.com/19773760/111913252-8794f580-8a82-11eb-9cd1-16ff0ffc77d8.png)

# Modes of Operation
- __Blocking__: Receive operation is blocked until data is fully copied over the shared device/channel and turn is passed to userland.
- __Nonblocking__: The userland process should check each channels in polling manner to check if data is ready for reception, or the turn has been passed back to it.
- __Asynchronous__: (Not implemented yet) The client will be notified through kqeueu and a callback is executed when data is ready or turn is passed back to the userland process.

# API
You can take a look at the tests folder to see examples of how to use the kernel module and the usespace library. As a reference here is the KUP kernel API functions (see kuplib/kup.h and kupdev/kupdev.h)
```c
// Create a new KUP device with name /dev/name with a count of chan_cnt
// channels, each having a size of size pages.
struct kupdev_softc *
kupdev_create(const char *name, size_t size, size_t chan_cnt);

// Wait until a userspace process attaches a channel on this KUP device
int
kupdev_wait_channel(struct kupdev_softc *sc);

int
kupdev_unload(struct kupdev_softc* sc);

// Notify the userspace that a new KUP channel is available
void
kupdev_notify(struct kupdev_softc *sc);

int
kupdev_send(struct kupdev_softc *sc, void *data, size_t len, int chan_id);

void*
kupdev_receive(struct kupdev_softc *sc, int chan_id);

// Free a receive cahnnel after we are done with the data in it.
void
kupdev_unlock_channel(struct kupdev_softc* sc, int chan_id);

// Pass the turn on channel chan_of of KUP device sc to the userspace process
void
kupdev_pass(struct kupdev_softc* sc, int chan_id);
```

And here is the userspace libkup.so API (see kuplib/kup.h):
```c
int kernproxy_errno;

void* kernproxy_open(char const *name);

// Attach to a specific channel of the KUP device represented by handle
void* kernproxy_channel(void* handle, size_t chan_id, size_t size);

void* kernproxy_receive(void *handle, int flags);

int kernproxy_send(void *handle, void *data, size_t len, int flags);

void kernproxy_close(void* handle);

int kernproxy_error(void* handle);
```

# Example
## Kernel-side code
First you need to ask the KUP kernel module to create a new devfs entry for your module (`mydev` here). You also have to specify the number of memory pages (1 here) dedicated to each channel, and the maximum number of channels (again 1 here).
```c
 scx = kupdev_create("mydev", 1, 1);
 if (scx == NULL) {
    DEBUG_PRINT("Failed to create KUP channel\n");
    goto cleanup;
 }
```
After the channel is declared, you have to wait until a userspace process connects and opens a channel.
```c
int chan_id = kupdev_wait_channel(scx);
```
Then you can send data to userland over that channel as simply as:
```c
kupdev_send(scx, data, data_len, chan_id);
```
Furthermore, when new channels are created by the kernel side, you can notify any pending userland clients of existence of the new channels via `kupdev_notify()` library call:
```c
kupdev_notify(scx);
```
__Note__: When a channel is first created, its the kernel side's turn to send data. At any point each side can just send a predefined dummy token to the other side to just pass the turn as per requirements of the application/protocol.
## Userland-side code

```c
void* handle = kernproxy_open("mydev");
if (!handle) { 
   fprintf(stderr, "Opening KUP device mydev failed\n");
   goto failed;
}
```
Then you can open a channel (here channel 0), with the specified size (here 1 page of memory) via `kernproxy_channel()`. On success a non-NULL value is returned which is a handle to the newly opened KUP channel.
```c
channel = kernproxy_channel(handle, 0, 1);
```
If the return value is NULL you can use `kernproxy_error()` to check the error code. For example:
```c
if (kernproxy_error(handle) == EKU_SHUTDOWN) {
    fprintf(stderr, "KUP module is shutting down\n");
    goto finito_error;
} else if (kernproxy_error(handle) == EKU_NOTREADY) {
    fprintf(stderr, "KUP kernel module is not ready\n");
    goto finito_error;
}
```
You can send/receive data over the opened channel as simple as:
```c
void* data = kernproxy_receive(channel, 0);
kernproxy_send(channel, data, data_len, 0);
```
