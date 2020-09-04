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

int const kChannels = 8;

void
run_test(void* dummy)
{
	scx = kupdev_create("kup_dev", 1, kChannels);
	if (scx == NULL) {
		DEBUG_PRINT("scx is NULL\n");
		goto cleanup;
	}
	DEBUG_PRINT("kupdev_softc created\n");

	for (int i = 0; i < kChannels; i++) {
		int chan_id = kupdev_wait_channel(scx);
		DEBUG_PRINT("wait finished. chan_id: %d\n", chan_id);
		if (chan_id < 0) {
			DEBUG_PRINT("channel id is negative for chan_id: \n", i);
			goto err_exit;
		} else {
			kupdev_notify(scx);
			DEBUG_PRINT("notify done\n");
			kupdev_send(scx, "", 1, chan_id);
			char const* nonce = (char const*)kupdev_receive(scx, chan_id);
			kupdev_send(scx, nonce, strlen(nonce) + 1, chan_id);
		}
	}

err_exit:
	kproc_exit(0);
}

int
finish_test(void)
{
	return kupdev_unload(scx);
}

