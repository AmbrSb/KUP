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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kthread.h>

#include "../kupdev.h"
#include "../test_module.h"

void run_test(void*);
int finish_test(void);
void* scx;

void
run_test(void* dummy)
{
	scx = kupdev_create("kup_dev", 1, 2);
	if (scx == NULL) {
		DEBUG_PRINT("scx is NULL\n");
		goto cleanup;
	}
	DEBUG_PRINT("kupdev_softc created\n");
	kupdev_notify(scx);
	DEBUG_PRINT("notify done\n");
	int chan_id1 = kupdev_wait_channel(scx);
	int chan_id2 = kupdev_wait_channel(scx);
	DEBUG_PRINT("wait finished. chan_id1: %d\n", chan_id1);
	DEBUG_PRINT("wait finished. chan_id2: %d\n", chan_id2);
	if (chan_id1 < 0 || chan_id2 < 0) {
		DEBUG_PRINT("channel id is negative\n");
	} else {
		kupdev_send(scx, "", 1, chan_id1);
		kupdev_send(scx, "", 1, chan_id2);
		for (int i = 0; i < 1000000; i++) {
			int* cnt1 = (int*)kupdev_receive(scx, chan_id1);
			int* cnt2 = (int*)kupdev_receive(scx, chan_id2);
			DEBUG_PRINT(">> KERN got chan_id1: %d\n", cnt1);
			DEBUG_PRINT(">> KERN got chan_id2: %d\n", cnt2);
			kupdev_send(scx, cnt1 + 1, sizeof(cnt1), chan_id1);
			kupdev_send(scx, cnt2 + 1, sizeof(cnt2), chan_id2);
		}
	}

	kproc_exit(0);
}

int
finish_test(void)
{
	return kupdev_unload(scx);
}

