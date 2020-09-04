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


//#define KUP_DEBUG

#ifdef KUP_DEBUG
#define VM_OBJECT_DEBUG 1
#endif

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/kthread.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>

#include <sys/rwlock.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/selinfo.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/uma.h>

#include "kup_dev.h"

#define KUP_API

#define KUP_LOG_PREFIX "[kup] "

#ifdef KUP_DEBUG
#define DEBUG_PRINT(...) do{ printf( KUP_LOG_PREFIX __VA_ARGS__ ); } while(0)
#else
#define DEBUG_PRINT(...) do{ } while (0)
#endif

#define CMD_OFFSET(a)  ((int*)(a + 8))

#define DATA_SEND_OFFSET(c,i)				\
		((void*)(c->comm_channels[i].mem +  \
				PAGE_SIZE))

#define DATA_RECV_OFFSET(c,i) 				\
		((void*)(c->comm_channels[i].mem +  \
				(c->size + 1) * PAGE_SIZE))

typedef struct comm_channel {
	_Alignas(CACHE_LINE_SIZE)
	struct mtx 					lock;
	volatile vm_offset_t		mem;
	pid_t						pid;
	volatile int				status;
	SLIST_ENTRY(comm_channel)	next;
} comm_channel_t;

typedef struct {
		size_t i;
		comm_channel_t *chan;
} channel_it_t;
#define channel 		loop.chan
#define channel_index	loop.i
#define FOR_EACH_CHANNEL(d) 								\
	for (channel_it_t loop = {0, &d->comm_channels[0]};		\
					loop.i < d->channel_cnt;				\
					loop.i++,								\
					loop.chan = &d->comm_channels[loop.i])

typedef enum {
		CMD_ACTIVE,
		CMD_CLOSE
} channel_cmd_t;

typedef enum {
		CHAN_PENDING,
		CHAN_READY
} channel_status_t;

typedef enum {
		DAEMON = 0,
		KERNEL = 1
} turn_t;

static MALLOC_DEFINE(M_STUBDEV, "kup_dev",
		     "character device for kern-user proxy");

typedef struct kupdev_softc {
	struct cdev*		cdev;
	size_t				channel_cnt;
	// The size of each communication channel in count of pages.
	size_t				size;
	struct cv			condvar;
	struct mtx			lock;
	struct selinfo		rsel;
	struct selinfo		wsel;
	volatile int		disabled;
	struct proc			monitor_proc;
	// Communications channels in this device. There should be at least one.
	comm_channel_t	comm_channels[0];
	eventhandler_tag	monitor_cookie;
} kup_softc_t;

static int	kupdev_kqevent(struct knote*, long);
static void	kupdev_kqdetach(struct knote*);

static struct filterops kupdev_filterops = {
	.f_isfd =	1,
	.f_detach =	kupdev_kqdetach,
	.f_event =	kupdev_kqevent,
};

inline static void lock_channel_by_id(kup_softc_t* sc, size_t chan_id);
inline static void lock_channel(comm_channel_t* chan);
inline static void unlock_channel_by_id(kup_softc_t* sc, size_t chan_id);
inline static void unlock_channel(comm_channel_t* chan);

// A dummy wait channel used by various thread in KUP devices;
static int kup_wait_chan;

static void
kup_freeup(void *mem)
{
	vm_object_deallocate((vm_object_t)mem);
}

/**
 *	Returns a raw pointer to the channel with index 'chan_id' in 'sc'.
 */
static comm_channel_t*
get_channel(kup_softc_t* sc, int chan_id)
{
	return &sc->comm_channels[chan_id];
}

/**
 *	Returns a raw pointer to the channel with index 'chan_id' in 'sc'.
 *	The channel will be locked.
 */
static comm_channel_t*
get_channel_locked(kup_softc_t* sc, int chan_id)
{
	comm_channel_t* chan = get_channel(sc, chan_id);
	// XXX do we have a race condition here?
	lock_channel(chan);
	return chan;
}

inline
static void
lock_channel(comm_channel_t* chan)
{
	mtx_lock(&chan->lock);
}

inline
static void
lock_channel_by_id(kup_softc_t* sc, size_t chan_id)
{
	comm_channel_t* chan = get_channel(sc, chan_id);
	// XXX do we have a race condition here?
	lock_channel(chan);
}

inline
static void
unlock_channel(comm_channel_t* chan)
{
	mtx_unlock(&chan->lock);
}

/**
 *
 */
inline
static void
unlock_channel_by_id(kup_softc_t* sc, size_t chan_id)
{
	comm_channel_t* chan = get_channel(sc, chan_id);
	unlock_channel(chan);
}

inline
static void
lock_kupdev(kup_softc_t* sc)
{
	mtx_lock(&sc->lock);
}

inline
static void
unlock_kupdev(kup_softc_t* sc)
{
	mtx_unlock(&sc->lock);
}

inline
static int*
get_channel_turn(comm_channel_t* chan)
{
	return (int*)(chan->mem);
}

/**
 *	Sets the turn status of channel 'chan' to 'turn_id'. See enum
 *
 *	Assumes the channel is already locked by the current thread.
 */
static void
set_turn(comm_channel_t* chan, turn_t turn_id)
{
	// The first 8 bytes in each channel is used as the 'turn' indicator
	volatile int* turn = get_channel_turn(chan);
	*turn = turn_id;
	unlock_channel(chan);
}

/*
 * Creates a communication channel object, initializes it, and
 * adds it to the global list of communication channels.
 * The new communication channel objet is returned in locked state.
 */
static void
init_comm_channel(comm_channel_t* chan)
{
	mtx_init(&chan->lock, "comm_channel", NULL, MTX_DEF);
	chan->status = 0;
	chan->mem = (vm_offset_t) NULL;
	chan->pid = -1;
}

/**
 * Verifies that a process with pid `pid` is running.
 *
 * Returns 1 if such a process exists, 0 otherwise.
 */
static int
process_exists(pid_t pid)
{
	struct proc *p;
	int found = 0;
	sx_slock(&allproc_lock);
	LIST_FOREACH(p, PIDHASH(pid), p_hash) {
		PROC_LOCK(p);
		if (!p->p_vmspace || (p->p_flag & P_WEXIT)) {
			PROC_UNLOCK(p);
			continue;
		}
		if (p->p_pid == pid && p->p_state != PRS_NEW) {
			found = 1;
			PROC_UNLOCK(p);
			break;
		}
		PROC_UNLOCK(p);
	}
	sx_sunlock(&allproc_lock);
	return (found);
}

/**
 * This method blocks on a kup software context 'sc', until it finds a free
 * and usable channel that has been mapped to a memory segment via a
 * userspace daemon. This method polls the set of defined channels in the
 * context 10 times per second until it is done.
 *
 * Returns the channel index of the selected channel in 'sc'.
 */
KUP_API
int
kupdev_wait_channel(kup_softc_t* sc)
{
	if (!sc)
		return -2;

	lock_kupdev(sc);
	while (!sc->disabled) {
		FOR_EACH_CHANNEL(sc) {
			lock_channel(channel);
			if (channel->mem && channel->status == CHAN_PENDING) {
				DEBUG_PRINT("%s: New channel (id: %lu) attached.\n",
								__FUNCTION__, channel_index);
				channel->status = CHAN_READY;
				unlock_channel(channel);
				unlock_kupdev(sc);
				return channel_index;
			}
			unlock_channel(channel);
		}
		cv_wait(&sc->condvar, &sc->lock);
	}
	unlock_kupdev(sc);
	return -1;
}

/**
 *	Passes the turn on channel index 'chan_id' of software context 'sc' to the
 *  user space daemon.
 *
 *  Assumes the channel is already locked by this thread, and it will unlocked
 *  it before returning.
 */
inline
static void
pass_turn(kup_softc_t* sc, int chan_id)
{
	comm_channel_t* chan = get_channel(sc, chan_id);
	// This will unlock the channel
	set_turn(chan, DAEMON);
}

/**
 * This method passes the turn to the user space daemon by queueing a NULL
 * message.
 */
inline void
kupdev_pass(kup_softc_t* sc, int chan_id)
{
	kupdev_send(sc, "", 1, chan_id);
}

/**
 *	This method blocks until the user space daemon corresponding to channel
 *	'chan_id' of kup software context 'sc' passes the turn to kernel. This
 *	method check the status of the channel in a polling mode for a short
 *	period and then gives up the processor and checks the channel status
 *	10 times per second.
 *
 *	Assumes that the channel is already locked.
 */
inline static int
wait_for_turn(kup_softc_t* sc, int chan_id)
{
	comm_channel_t* chan = get_channel(sc, chan_id);
	volatile int* turn = get_channel_turn(chan);
	int cnt = 0;
	while (*turn == DAEMON && !sc->disabled) {
		if (cnt < 2000000) {
			cnt++;
			cpu_spinwait();
		} else {
			unlock_channel(chan);
			DEBUG_PRINT("turn == %d\n", *turn);
			tsleep(&kup_wait_chan, 0, "waiting for channel to ready",
					100 * hz / 1000);
			DEBUG_PRINT("before lock chanel\n");
			lock_channel(chan);
			DEBUG_PRINT("after lock chanel\n");
			if (chan->status != CHAN_READY) {
				DEBUG_PRINT("breaking\n");
				break;
			}
			DEBUG_PRINT("before get turn\n");
			turn = get_channel_turn(chan);
			DEBUG_PRINT("after get turn\n");
		}
	}
	if (chan->status != CHAN_READY || sc->disabled)
		return (1);
	return (0);
}

/**
 *	Send 'len' bytes from buffer pointed to by 'data' over channel 'chan_id'
 *	of kup software context sc.
 *	This method automatically blocks and waits until turn is passed to kernel
 *	before starting a transaction.
 */
KUP_API
int
kupdev_send(kup_softc_t* sc, void *data, size_t len, int chan_id)
{
	int error;
	KASSERT(chan_id < sc->channel_cnt,
			("kup device received 'send' request for a "
			"non-existent channel: %d", chan_id));
	comm_channel_t* chan = get_channel_locked(sc, chan_id);
	if (chan->status != CHAN_READY) {
		unlock_channel(chan);
		return (-1);
	}
	error = wait_for_turn(sc, chan_id);
	if (error) {
		// Something has gone wrong, probably the KUP device is being closed
		// and no longer can be used.
		unlock_channel(chan);
		return (-2);
	}
	memcpy(DATA_SEND_OFFSET(sc, chan_id), data, len);
	pass_turn(sc, chan_id);
	return (0);
}

/**
 * Unlock the channel chan_id on device sc. This is specifically used after
 * a kupdev_receive(...) call which returns with the channel locked.
 */
KUP_API
void
kupdev_unlock_channel(kup_softc_t* sc, int chan_id)
{
	unlock_channel_by_id(sc, chan_id);
}

/**
 *	Blocks on channel 'chan_id' of software context 'sc' util we get the turn
 *	and then returns a pointer to the data filled by the user space daemon.
 *	Returns with the channel locked.
 */
KUP_API
void*
kupdev_receive(kup_softc_t* sc, int chan_id)
{
	int error;
	KASSERT(chan_id < sc->channel_cnt,
			("kup device received 'receive' request for a "
			"non-existent channel: %d", chan_id));
	comm_channel_t* chan = get_channel_locked(sc, chan_id);
	if (chan->status != CHAN_READY) {
		unlock_channel(chan);
		return (NULL);
	}
	error = wait_for_turn(sc, chan_id);
	if (error) {
		// Something has gone wrong, probably the KUP device is being closed
		// and no longer can be used.
		unlock_channel(chan);
		return (NULL);
	}
	void* result = DATA_RECV_OFFSET(sc, chan_id);
	/* unlock_channel_by_id(sc, chan_id); */
	return result;
}

static int
kupdev_kqevent(struct knote *kn, long hint)
{
	kup_softc_t* sc = kn->kn_hook;
	// If the sc has been marked as disabled, fire an EVFILT_USER which will
	// signal the user space library that it should shut down.
	if (sc->disabled) {
		kn->kn_filter = EVFILT_USER;
		return (1);
	}

	int found = 0;
	kup_softc_t* scx;
	scx = kn->kn_hook;
	FOR_EACH_CHANNEL(sc) {
		if (channel->status == CHAN_PENDING)
			found = 1;
	}
	kn->kn_data = 1;
	return (found);
}

static void
kupdev_kqdetach(struct knote *kn)
{
	kup_softc_t* sc;

	sc = kn->kn_hook;
	knlist_remove(&sc->rsel.si_note, kn, 0);
}

static int
kupdev_kqfilter(struct cdev *dev, struct knote *kn)
{
	kup_softc_t* sc;

	sc = dev->si_drv1;
	switch (kn->kn_filter) {
		case EVFILT_READ:
		case EVFILT_USER:
			kn->kn_hook = sc;
			kn->kn_fop = &kupdev_filterops;
			knlist_add(&sc->rsel.si_note, kn, 0);
			return (0);
		default:
			return (EINVAL);
	}
}

static int
kup_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return (0);
}

static int
kup_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	vm_object_t mem;
	int error = 0;

	// Do not allow read-only and write-only open()
	if ((oflags & (FREAD | FWRITE)) != (FREAD | FWRITE))
		return (EINVAL);

	// A separate VM object will be used for all mmap()ings for each open()ed
	// device instance. We will retreive this object in 'kup_mmap_single' when
	// the user space process tries mmap() a new segment of memory.
	mem = vm_pager_allocate(OBJT_DEFAULT, NULL, 0, VM_PROT_DEFAULT, 0,
			curthread->td_ucred);
	VM_OBJECT_WLOCK(mem);
	vm_object_clear_flag(mem, OBJ_ONEMAPPING);
	vm_object_set_flag(mem, OBJ_NOSPLIT);
	VM_OBJECT_WUNLOCK(mem);
	error = devfs_set_cdevpriv(mem, kup_freeup);
	if (error)
		vm_object_deallocate(mem);
	return (error);
}

/**
 * Determine if channel 'chan' is free to be assigned to a new process.
 */
static int
channel_is_free(comm_channel_t* chan)
{
	return (chan->status == CHAN_PENDING &&
					chan->mem == (vm_offset_t) NULL);
}

static int
channel_is_active(comm_channel_t* chan)
{
	return (chan->mem && chan->status != CHAN_PENDING);
}

/**
 * Try to assign a newly mapped memory segment to a free channel in sc.
 * This function assumes that the KUP device is already locked.
 *
 * Returns 0 on success and returns 1 if there are currently no free channels
 * in 'sc'.
 */
static int
assign_to_channel(kup_softc_t* sc, vm_offset_t mem)
{
	DEBUG_PRINT("%s: Assigning mem: %x\n",
			__FUNCTION__, (unsigned int)mem);
	int assigned = 0;
	FOR_EACH_CHANNEL(sc) {
		lock_channel(channel);
		if (channel_is_free(channel)) {
				channel->mem = mem;
				channel->pid = curproc->p_pid;
				*CMD_OFFSET(channel->mem) = CMD_ACTIVE;
				set_turn(channel, KERNEL);
				// Allow a kernel thread blocked in 'wait_comm_channel' to
				// take up this channel.
				cv_signal(&sc->condvar);
				assigned = 1;
				break;
		} else
			unlock_channel(channel);
	}
	return (1 - assigned);
}

static int
kup_mmap_single(struct cdev* cdev, vm_ooffset_t* vmoffset, vm_size_t vmsize,
		  vm_object_t* object, int nprot)
{
	vm_object_t vmobj;
	vm_pindex_t vmobj_size;
	kup_softc_t* sc;
	vm_ooffset_t increment;
	int error, res;

	error = devfs_get_cdevpriv((void **)&vmobj);
	if (error)
		return (error);

	sc = cdev->si_drv1;
	lock_kupdev(sc);
	if (sc->disabled) {
		DEBUG_PRINT("%s, WARNING: attempted to mmap a kup device after it has "
				"been disabled", __FUNCTION__);
		unlock_kupdev(sc);
		return (EOPNOTSUPP);
	}

	vm_offset_t newsize = *vmoffset + vmsize;
	vmobj_size = OFF_TO_IDX(newsize) + ((newsize & PAGE_MASK)? 1 : 0);

	VM_OBJECT_WLOCK(vmobj);
	if (vmobj_size > vmobj->size) {
		increment = ptoa(vmobj_size - vmobj->size);
		res = swap_reserve(increment);
		if (0 == res) {
			VM_OBJECT_WUNLOCK(vmobj);
			unlock_kupdev(sc);
			return (ENOMEM);
		}
		vmobj->charge += increment;
		vmobj->size = vmobj_size;
	}
	vm_object_reference_locked(vmobj);
	*object = vmobj;
	VM_OBJECT_WUNLOCK(vmobj);

	vm_offset_t addr = vm_map_min(kernel_map);
	int rv = vm_map_find(kernel_map, vmobj, *vmoffset, &addr, vmsize, 0,
			VMFS_OPTIMAL_SPACE, VM_PROT_READ | VM_PROT_WRITE,
			VM_PROT_READ | VM_PROT_WRITE, 0);
	if (rv != KERN_SUCCESS) {
		printf("%s: vm_map_find(%zx) failed\n",
						__FUNCTION__, (size_t)vmsize);
		goto error_unlock;
	}
	rv = vm_map_wire(kernel_map, addr, addr + vmsize,
			VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);
	if (rv != KERN_SUCCESS) {
		printf("%s: vm_map_wire failed\n", __FUNCTION__);
		goto error_free;
	}
	error = assign_to_channel(sc, addr);
	unlock_kupdev(sc);
	return (error);

error_free:
	vm_map_remove(kernel_map, addr, addr + vmsize);
error_unlock:
	unlock_kupdev(sc);
	return (ENOMEM);
}

static struct cdevsw*
create_cdevsw(char const* name)
{
	struct cdevsw* kupdev_cdevsw = malloc(sizeof(struct cdevsw),
					M_TEMP, M_WAITOK | M_ZERO);
	kupdev_cdevsw->d_version = D_VERSION;
	kupdev_cdevsw->d_open = kup_open;
	kupdev_cdevsw->d_close = kup_close;
	kupdev_cdevsw->d_mmap_single = kup_mmap_single;
	kupdev_cdevsw->d_kqfilter = kupdev_kqfilter;
	kupdev_cdevsw->d_name = name;

	return kupdev_cdevsw;
}

/**
 *	This method is the main loop ran by a background thread in kernel for
 *	each instance of KUP device created by each client kernel module.
 *	It monitors validity of each channel in the software context by verifying
 *	that the owning daemon in the user space is still running, and frees
 *	any dangling channels to be taken up by a new instance of the daemon.
 */
static void
channels_monitor(void *scp)
{
	kup_softc_t* sc = (kup_softc_t*)scp;

	if (!sc->disabled) {
		FOR_EACH_CHANNEL(sc) {
			lock_channel(channel);
			if (channel_is_active(channel) &&
							!process_exists(channel->pid)) {
					DEBUG_PRINT("%s: proc (%d) not found. Will "
						"release the corresponding "
						"channel.\n", __FUNCTION__, channel->pid);
					channel->status = CHAN_PENDING;
					channel->mem = 0;
					channel->pid = -1;
					// Inform any pending user space daemons that a new
					// channel is available to be taken up.
					KNOTE_UNLOCKED(&sc->rsel.si_note, 0);
			}
			unlock_channel(channel);
		}
	}
}

/**
 *	Create a new kup device named /dev/'name' with 'chan_cnt' number of
 *	communication channels with user space. The size of each channel
 *	will be 'size" bytes.
 *	Returns a software context representing the newly created device with
 *	type 'kup_softc_t'. This type is opaque to the user of this
 *	API, and will be used as a handle (void*) to other API functions.
 */
KUP_API
kup_softc_t*
kupdev_create(const char *name, size_t size, size_t chan_cnt)
{
	kup_softc_t* sc;

	struct cdevsw *cdevsw = create_cdevsw(name);
	sc = malloc(sizeof(*sc) + chan_cnt * sizeof(comm_channel_t),
					M_STUBDEV, M_WAITOK | M_ZERO);
	mtx_init(&sc->lock, name, NULL, MTX_DEF);
	sc->channel_cnt = chan_cnt;
	sc->size = size;
	FOR_EACH_CHANNEL(sc) {
		init_comm_channel(channel);
	}
	knlist_init_mtx(&sc->rsel.si_note, NULL);
	knlist_init_mtx(&sc->wsel.si_note, NULL);
	sc->cdev = make_dev(cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "%s", name);
	if (sc->cdev == NULL) {
		knlist_destroy(&sc->rsel.si_note);
		knlist_destroy(&sc->wsel.si_note);
		mtx_destroy(&sc->lock);
		free(sc, M_STUBDEV);
		printf("[kup] %s: kupdev: Failed to create /dev/%s\n",
						__FUNCTION__, name);
		return (NULL);
	}
	sc->cdev->si_drv1 = sc;
	// Whenever a process exits, we check if it was attached to any of the
	// cahnnels of this KUP device instance. This is implemented in
	// 'cahnnels_monitor' function.
	sc->monitor_cookie = EVENTHANDLER_REGISTER(process_exit, channels_monitor,
					sc, EVENTHANDLER_PRI_ANY);

	return (sc);
}

static void
kupdev_destroy(kup_softc_t* sc)
{
	EVENTHANDLER_DEREGISTER(process_exit, sc->monitor_cookie);
	destroy_dev(sc->cdev);
	knlist_destroy(&sc->rsel.si_note);
	knlist_destroy(&sc->wsel.si_note);
	seldrain(&sc->rsel);
	seldrain(&sc->wsel);
	mtx_destroy(&sc->lock);
	free(sc, M_STUBDEV);
}

/*
 *	Unloads and destroys the KUP device represented by sc and frees all
 *	associated buffers and resources.
 *
 *	@param sc  This is a device handle returned to the user by kupdev_create
 *	and is opaque to the users of this API.
 *
 *	Returns 0 on success. If there is a user space daemon connected to this
 *	device this method does nothing and returns 1 to signal
 *	failure.
 */
KUP_API
int
kupdev_unload(kup_softc_t* sc)
{
	lock_kupdev(sc);
	FOR_EACH_CHANNEL(sc) {
		/* lock_channel(channel); */
		if (process_exists(channel->pid)) {
			// There is a user space process attached to this channel, so we
			// cannot allow unloading of this device.
			/* unlock_channel(channel); */
			unlock_kupdev(sc);
			return 1;
		}
		/* unlock_channel(channel); */
	}
	sc->disabled = 1;
	unlock_kupdev(sc);
	KNOTE_UNLOCKED(&sc->rsel.si_note, 0);
	FOR_EACH_CHANNEL(sc) {
		lock_channel(channel);
		channel->status = CHAN_PENDING;
		channel->pid = -1;
		if (channel->mem) {
			*CMD_OFFSET(channel->mem) = CMD_CLOSE;
			// Pass turn to user space on this channel, so it receives the
			// CMD_CLOSE command and starts shutting down.
			// pass_turn will also unlock channel->lock
			pass_turn(sc, channel_index);
			// Setting channel->mem to zero after pass_turn which has already
			// unlocked channel is OK, because its status is already set to
			// CHAN_PENDING.
			channel->mem = 0;
		}
		mtx_destroy(&channel->lock);
	}
	kupdev_destroy(sc);

	return 0;
}

/**
 * Notify the user space daemon of a new event. This will awaken a user space
 * daemon blocked in kernproxy_open() blocked by a kevent() call. This usually
 * means a new channel is available to be mapped by the user space daemon.
 */
KUP_API
void
kupdev_notify(kup_softc_t* sc)
{
	KNOTE_UNLOCKED(&sc->rsel.si_note, 0);
}

static int
kup_event_handler(struct module *mod, int event_t, void *arg)
{

    int retval = 0;

    switch (event_t)
    {
    case MOD_LOAD:
        printf("[kup] kernel module loaded\n");
        break;
    case MOD_UNLOAD:
        printf("[kup] unload: kernel module is going to unload.\n");
        break;
    case MOD_SHUTDOWN:
        printf("[kup] shutdown: kernel module is going to unload.\n");
	break;

    default:
        retval = EOPNOTSUPP;
        break;
    }

    return (retval);
}

static moduledata_t kup_data = {
    "kup_dev",
    kup_event_handler,
    NULL
};

DECLARE_MODULE(kup_dev, kup_data, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(kup_dev, 1);

