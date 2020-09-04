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
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/event.h>
#include <sys/kthread.h>

#include "../kupdev.h"
#include "test_module.h"


/**
 * run_test and finish_test should be implemented in each individual
 * test case which we will compile with this kernel module and call at runtime.
 */
extern void run_test(void*);
extern int finish_test(void);

static int
init_test()
{
	int error;
	error = kproc_create(run_test, NULL, NULL, 0, 0, TEST_ID " kernel process");
	if (error)
		printf("failed to start test kernel process %s\n", TEST_ID);
	return error;
}

static int
lkm_event_handler(struct module *mod, int event_t, void *arg)
{
    int retval = 0;
    int error;

    switch (event_t)
    {
    case MOD_LOAD:
        printf(TEST_ID " kernel module 2 loading\n");
		error = init_test();
		if (error)
			retval = EAGAIN;
		else
        	printf(TEST_ID " kernel module 2 loaded\n");
        break;
    case MOD_UNLOAD:
		error = finish_test();
		if (error) {
			printf(TEST_ID "Cannot unload module 2 at this time!\n");
			retval = EAGAIN;
		}
        printf(TEST_ID " ipsec_test kernel module 2 is going to unload.\n");
        break;
    case MOD_SHUTDOWN:
		error = finish_test();
		if (error) {
			printf(TEST_ID " Cannot unload module 2 at this time!\n");
			retval = EAGAIN;
		}
		break;

    default:
        retval = EOPNOTSUPP;
        break;
    }

    return (retval);
}

static moduledata_t test_module_data = {
    "test_module_01",
    lkm_event_handler,
    NULL
};

DECLARE_MODULE(test_module_01, test_module_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(test_module_01, 1);
MODULE_DEPEND(test_module_01, kup_dev, 1, 1, 1);

