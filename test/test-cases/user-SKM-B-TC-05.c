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

#include "../kup.h"

int main(int argc, char* argv[])
{
	char const* dev_name = "/dev/kup_dev";
	void* channel1;
	void* channel2;

	void* handle = kernproxy_open(dev_name);
	if (!handle) {
			fprintf(stderr, "Opening device '%s' failed\n", dev_name);
			goto finito_error;
	}
	channel1 = kernproxy_channel(handle, 0, 1);
	if (!channel1) {
		if (kernproxy_error(handle) == EKU_SHUTDOWN) {
			fprintf(stderr, "EKU_SHUTDOWN\n");
			goto finito_error;
		} else if (kernproxy_error(handle) == EKU_NOTREADY) {
			fprintf(stderr, "EKU_NOTREADY\n");
			goto finito_error;
		}
	}
	channel2 = kernproxy_channel(handle, 1, 1);
	if (!channel2) {
		if (kernproxy_error(handle) == EKU_SHUTDOWN) {
			fprintf(stderr, "EKU_SHUTDOWN\n");
			goto finito_error;
		} else if (kernproxy_error(handle) == EKU_NOTREADY) {
			fprintf(stderr, "EKU_NOTREADY\n");
			goto finito_error;
		}
	}

	fprintf(stderr, "Trying to read from channel 1\n");
	void* data = kernproxy_receive(channel1, 0);
	if (!data) {
		fprintf(stderr, "Error: recv failed.\n");
		goto finito_error;
	} else if (*(char*)data != 0) {
		fprintf(stderr, "Error: expected null packet.\n");
		goto finito_error;
	}

	fprintf(stderr, "Trying to read from channel 2\n");
	data = kernproxy_receive(channel2, 0);
	if (!data) {
		fprintf(stderr, "Error: recv failed.\n");
		goto finito_error;
	} else if (*(char*)data != 0) {
		fprintf(stderr, "Error: expected null packet.\n");
		goto finito_error;
	}

	int counter1 = 0;
	int counter2 = 100;
	for (int i = 0; i < 30; i++) {
		kernproxy_send(channel1, (void*)&counter1, sizeof(counter1), 0);
		kernproxy_send(channel2, (void*)&counter2, sizeof(counter2), 0);
		data = kernproxy_receive(channel1, 0);
		if (data) {
			if (*(int*)data != counter1 + 1) {
				fprintf(stderr, "counter 1 failed %d != %d\n", *(int*)data, counter1);
				goto finito_error;
			}
		}
		else { // Close channel 1
			if (kernproxy_error(handle) == EKU_SHUTDOWN) {
				fprintf(stderr, "Kernel asked on channel 1 to shutdown\n");
				goto finito_error;
			}
		}
	
		data = kernproxy_receive(channel2, 0);
		if (data) {
			if (*(int*)data != counter2 + 1) {
				fprintf(stderr, "counter 2 failed %d != %d\n", *(int*)data, counter2);
				goto finito_error;
			}
		}
		else { // Close channel 2
			if (kernproxy_error(handle) == EKU_SHUTDOWN) {
				fprintf(stderr, "Kernel asked on channel 2 to shutdown\n");
				goto finito_error;
			}
		}
		counter1++;
		counter2++;
	}

	fprintf(stderr, "Test passed\n");
	kernproxy_close(handle);
	return 0;

finito_error:
	fprintf(stderr, "Test failed\n");
	return 1;
}

