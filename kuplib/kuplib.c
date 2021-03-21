/**
 * BSD 3-Clause License
 * 
 * Copyright (c) 2020, Amin Saba
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <sys/event.h>
#include <sys/user.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <assert.h>
#include <libutil.h>

#include "kup.h"

#define KERNPROXY_API

/**
 *	This macro is used to execute/restart system calls that may get
 *	interrupted by signal handlers
 */
#define MAYINT(cmd) (					\
	{									\
		int ret__;						\
		while((ret__ = (cmd)) < 0 &&	\
			errno == EINTR)				\
			;							\
		ret__;							\
	}									\
	)

#define CHAN_CMD(c)			((int*)(c->mem + 8))
#define CHAN_DATA_RECV(c)	(c->mem + PAGE_SIZE)
#define CHAN_DATA_SEND(c)	(c->mem + PAGE_SIZE * (c->size + 1))

enum {
		CMD_ACTIVE,
		CMD_CLOSE
};

enum {
	DAEMON = 0,
	KERNEL = 1
};

struct fdinfo {
	int kf_type;
	int vnode_type;
};

#define KF_TYPE_VNODE     1
#define KF_VTYPE_VCHR     4

typedef struct {
	uint8_t*	mem;
	size_t 		size;
	void*   	handle;
} channel_t;

struct fdinfo*
getfdinfo(int fd)
{
	int cntp;
	struct fdinfo *fdi;

	pid_t pid = getpid();
	struct kinfo_file *result = kinfo_getfile(pid, &cntp);
	if (result == NULL)
		return (NULL);
	for (int j = 0; j < cntp; j++) {
		struct kinfo_file *kf = &result[j];
		if (kf->kf_fd == fd) {
			fdi = malloc(sizeof(*fdi));
			if (fdi == NULL) {
				free(result);
				return NULL;
			}
			fdi->kf_type = kf->kf_type;
			fdi->vnode_type = kf->kf_vnode_type;
			free(result);
			return fdi;
		}
	}
	free(result);
	return (NULL); 
}

typedef struct {
	int fd;
	int kdf;
	int kernproxy_errno;
	struct kevent event_list[2];
} kernproxy_t;

/**
 *	Opens a KUP device named 'name' and returns a handle to it.
 */
KERNPROXY_API
void*
kernproxy_open(char const *name)
{
	int cdev = open(name, O_RDWR, 0);
	if (cdev < 0) {
		perror("open device failed");
		return NULL;
	}
	struct fdinfo* finfo = getfdinfo(cdev);
	if (finfo->kf_type 		!= KF_TYPE_VNODE ||
		finfo->vnode_type	!= KF_VTYPE_VCHR) {
		fprintf(stderr, "%s: Invalid device!\n", __FUNCTION__);
		MAYINT(close(cdev));
		free(finfo);
		return NULL;
	}
	free(finfo);

	int kdf = MAYINT(kqueue());
	if (kdf == -1) {
		perror("kqueue");
		return NULL;
	}
	kernproxy_t* kp = malloc(sizeof(*kp));
	kp->fd = cdev;
	kp->kdf = kdf;
	// This event 'EVFILT_READ' is fired when a new channel is available
	// on this device.
	EV_SET(&kp->event_list[0], kp->fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	// This event 'EVFILT_USER` is fired when this KUP device is about to be
	// destroyed in the kernel.
	EV_SET(&kp->event_list[1], kp->fd, EVFILT_USER, EV_ADD, 0, 0, NULL);
	int error = MAYINT(kevent(kp->kdf, kp->event_list, 2, NULL, 0, NULL));
	if (error == -1) {
		free(kp);
		perror("kevent");
		return NULL;
	}

	return kp;
}

/**
 * Set the turn on 'channel' to 'turn_id'
 */
static void
set_turn(channel_t* channel, int turn_id)
{
	volatile int* turn = (int*)(channel->mem);
	*turn = turn_id;
}

/**
 * Pass the turn on this channel to kernel
 */
static void
switch_turn(channel_t* channel)
{
	set_turn(channel, KERNEL);
}

/**
 * This function blocks until the KUP kernel module passes the turn
 * on channel 'channel' to user space.
 */
static void
wait_for_turn(channel_t* channel)
{
	volatile int *turn = (int*)(channel->mem);
	while (*turn == KERNEL);
}

/**
 * This function check if we currently have the turn on channel 'channel'
 * or not.
 *
 * Returns 1 if it is our turn, 0 otherwise.
 */
static int
is_our_turn(channel_t* channel)
{
	volatile int *turn = (int*)(channel->mem);
	return (*turn == DAEMON);
}

/**
 *	Returns the current error code on KUP device 'handle'. The return value of
 *	this function is valid only when an error condition is indicated by some
 *	other KUP API function.
 */
KERNPROXY_API
int
kernproxy_error(void* handle)
{
	kernproxy_t* kp = (kernproxy_t*) handle;
	return kp->kernproxy_errno;
}

/**
 *	This method blocks until channel 'chan_id' of size 'size' becomes available
 *	on the KUP device corresponding to 'handle'. It then returns a handle to
 *	that cahnnel. This handle can be used to send/receive data to/from the
 *	channel.
 */
KERNPROXY_API
void*
kernproxy_channel(void* handle, size_t chan_id, size_t size)
{
	kernproxy_t* kp = (kernproxy_t*) handle;
	int ret = kevent(kp->kdf, NULL, 0, kp->event_list, 2, NULL);
	if (ret == -1) {
		perror("kevent");
		exit(4);
	}
	for (int i = 0; i < ret; i++) {
		if (kp->event_list[i].flags & EV_ERROR) {
			fprintf(stderr, "ERROR: %s: %s\n", __FUNCTION__,
							strerror(kp->event_list[i].data));
		} else if (kp->event_list[i].filter == EVFILT_USER) {
			fprintf(stderr, "%s: Shutdown request from kernel.\n",
							__FUNCTION__);
			kp->kernproxy_errno = EKU_SHUTDOWN;
			return NULL;
		}
	}

#define CHAN_SIZE(s)  ((1 + 2 * s) * PAGE_SIZE)

	void* mem = mmap(0, CHAN_SIZE(size), PROT_READ | PROT_WRITE, MAP_SHARED,
					 kp->fd, CHAN_SIZE(size) * chan_id);
	if (mem == MAP_FAILED) {
		perror("mmap failed");
		kp->kernproxy_errno = EKU_NOTREADY;
		return NULL;
	}
	channel_t* chan = malloc(sizeof(*chan));
	chan->mem	= mem;
	chan->size	= size;
	chan->handle = handle;
	return chan;
}

/**
 *	This function returns a pointer to the buffer containing data received on
 *	channel 'channelp'.
 *
 *	param flags: If flags contains KP_NB, then the function retruns immediately
 *	if data is not yet available on the channel. Otherwise, the function blocks
 *	util data is available.
 */
KERNPROXY_API
void*
kernproxy_receive(void* channelp, int flags)
{
	channel_t* channel = (channel_t*)channelp;
	kernproxy_t* kp = (kernproxy_t*) channel->handle;
	if(*CHAN_CMD(channel) == CMD_CLOSE) {
		kp->kernproxy_errno = EKU_SHUTDOWN;
		fprintf(stderr, "chann closed!\n");
		return NULL;
	}
	if (flags & KP_NB) {
		if (!is_our_turn(channel)) {
			kp->kernproxy_errno = EKU_NOTREADY;
			return NULL;
		}
	} else
		wait_for_turn(channel);
	return CHAN_DATA_RECV(channel);
}

/**
 *	This function sends 'len' bytes of from buffer pointed to by 'data' over
 *	channel 'channelp'.
 *
 *	@param flags: If flags contains KP_NB, then the function terminates
 *	immediately if the channel is not ready for transmission. Otherwise,
 *	it blocks until the channel is ready.
 */
KERNPROXY_API
int
kernproxy_send(void* channelp, void *data, size_t len, int flags)
{
	channel_t* channel = (channel_t*)channelp;
	kernproxy_t* kp = (kernproxy_t*) channel->handle;
	if (flags & KP_NB) {
		if (!is_our_turn(channel)) {
			kp->kernproxy_errno = EKU_NOTREADY;
			return -1;
		}
	} else
		wait_for_turn(channel);
	memcpy(CHAN_DATA_SEND(channel), data, len);
	switch_turn(channel);
	return (0);
}

/**
 *	Closes the KUP device pointed to by 'handle'. This will release the kernel
 *	resources allocated for this instance.
 */
KERNPROXY_API
void
kernproxy_close(void* handle)
{
	kernproxy_t* kp = (kernproxy_t*) handle;
	MAYINT(close(kp->fd));
}

