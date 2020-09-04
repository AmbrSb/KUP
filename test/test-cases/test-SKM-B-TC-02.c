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
		DEBUG_PRINT("Failed to create kup device!\n");
		goto cleanup;
	}
	kupdev_notify(scx);

	int chan1_id = kupdev_wait_channel(scx);
	if (chan1_id < 0) {
		DEBUG_PRINT("Failed to acquire channel 1\n");
		goto cleanup;
	}
	DEBUG_PRINT("Channel 1 ready (id: %d)\n", chan1_id);

	int chan2_id = kupdev_wait_channel(scx);
	if (chan2_id < 0) {
		DEBUG_PRINT("Failed to acquire channel 2\n");
		goto cleanup;
	}
	DEBUG_PRINT("Channel 2 ready (id: %d)\n", chan2_id);

	kupdev_pass(scx, chan1_id);
	kupdev_pass(scx, chan2_id);

	char* expected_token1 = "SKM-B-TC-02-ABC";
	char* expected_token2 = "SKM-B-TC-02-DEFGH";

	void *r = kupdev_receive(scx, chan1_id);
	if (r) {
		if (strcmp(expected_token1, (char*)r)) {
			DEBUG_PRINT("Token 1 mismatch: %s\n", (char*)r);
			kupdev_unlock_channel(scx, chan1_id);
			kupdev_send(scx, "1", 2, chan1_id);
		} else {
			DEBUG_PRINT("SKM-B-TC-02 channel 1 passed.\n");
			kupdev_unlock_channel(scx, chan1_id);
			kupdev_send(scx, "0", 2, chan1_id);
		}
	}

	r = kupdev_receive(scx, chan2_id);
	if (r) {
		if (strcmp(expected_token2, (char*)r)) {
			DEBUG_PRINT("Token 2 mismatch: %s\n", (char*)r);
			kupdev_unlock_channel(scx, chan2_id);
			kupdev_send(scx, "1", 2, chan2_id);
		} else {
			DEBUG_PRINT("SKM-B-TC-02 channel 2 passed.\n");
			kupdev_unlock_channel(scx, chan2_id);
			kupdev_send(scx, "0", 2, chan2_id);
		}
	}

cleanup:
	kproc_exit(0);
}

int
finish_test(void)
{
	return kupdev_unload(scx);
}


