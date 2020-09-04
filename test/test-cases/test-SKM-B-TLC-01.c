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

static void* scx;
static char f_nonce[8192];
static char g_nonce[8192];

void run_test(void*);
int finish_test(void);

void
run_test(void* dummy)
{
	scx = kupdev_create("kup_dev", 2, 2);
	if (scx == NULL) {
		DEBUG_PRINT("Failed to create kup device\n");
		goto cleanup;
	}
	kupdev_notify(scx);
	int chan1_id = kupdev_wait_channel(scx);
	if (chan1_id < 0) {
		DEBUG_PRINT("Failed to acquire channel 1.\n");
		goto cleanup;
	}
	DEBUG_PRINT("Channel 1 ready (id: %d)\n", chan1_id);
	int chan2_id = kupdev_wait_channel(scx);
	if (chan2_id < 0) {
		DEBUG_PRINT("Allocation of channel 2 failed.\n");
		goto cleanup;
	}
	DEBUG_PRINT("Channel 2 ready (id: %d)\n", chan2_id);

	kupdev_pass(scx, chan1_id);
	char* r = (char*)kupdev_receive(scx, chan1_id);
	for (int i = 0; i < strlen(r); i++) {
		f_nonce[i] = r[i] + 1;
	}
	f_nonce[strlen(r)] = 0;
	for (int i = 0; i < strlen(r); i++) {
		g_nonce[i] = r[i] + 7;
	}
	g_nonce[strlen(r)] = 0;
	kupdev_unlock_channel(scx, chan1_id);
	DEBUG_PRINT("Sending f(nonce) on channel 1\n");
	kupdev_send(scx, f_nonce, strlen(f_nonce) + 1, chan1_id);
	DEBUG_PRINT("f(nonce) sent\n");

	DEBUG_PRINT("Sending  g(nonce) on channel 2\n");
	kupdev_send(scx, g_nonce, strlen(g_nonce) + 1, chan2_id);
	DEBUG_PRINT("g(nonce) sent\n");

cleanup:
	kproc_exit(0);
}

int
finish_test(void)
{
	return kupdev_unload(scx);
}

