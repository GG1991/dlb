/*********************************************************************************/
/*  Copyright 2009-2019 Barcelona Supercomputing Center                          */
/*                                                                               */
/*  This file is part of the DLB library.                                        */
/*                                                                               */
/*  DLB is free software: you can redistribute it and/or modify                  */
/*  it under the terms of the GNU Lesser General Public License as published by  */
/*  the Free Software Foundation, either version 3 of the License, or            */
/*  (at your option) any later version.                                          */
/*                                                                               */
/*  DLB is distributed in the hope that it will be useful,                       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of               */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                */
/*  GNU Lesser General Public License for more details.                          */
/*                                                                               */
/*  You should have received a copy of the GNU Lesser General Public License     */
/*  along with DLB.  If not, see <https://www.gnu.org/licenses/>.                */
/*********************************************************************************/

#include "LB_numThreads/ompt.h"

#include "LB_numThreads/omp_thread_manager.h"
#include "apis/dlb.h"
#include "LB_comm/shmem_cpuinfo.h"
#include "support/debug.h"
#include "support/mask_utils.h"
#include "support/tracing.h"
#include "apis/dlb_errors.h"

#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <string.h>

int omp_get_level(void) __attribute__((weak));

static pid_t pid;

/*********************************************************************************/
/*  OMPT callbacks                                                               */
/*********************************************************************************/

static void cb_parallel_begin(
        ompt_data_t *encountering_task_data,
        const ompt_frame_t *encountering_task_frame,
        ompt_data_t *parallel_data,
        unsigned int requested_parallelism,
        int flags,
        const void *codeptr_ra) {
    if (omp_get_level() == 0) {
        omp_thread_manager__borrow();
    }
}

static void cb_parallel_end(
        ompt_data_t *parallel_data,
        ompt_data_t *encountering_task_data,
        int flags,
        const void *codeptr_ra) {
    if (omp_get_level() == 0) {
        omp_thread_manager__lend();
    }
}

static void cb_implicit_task(
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *parallel_data,
        ompt_data_t *task_data,
        unsigned int actual_parallelism,
        unsigned int index,
        int flags) {
    if (endpoint == ompt_scope_begin) {
        int cpuid = shmem_cpuinfo__get_thread_binding(pid, index);
        int current_cpuid = sched_getcpu();
        if (cpuid >=0 && cpuid != current_cpuid) {
            cpu_set_t thread_mask;
            CPU_ZERO(&thread_mask);
            CPU_SET(cpuid, &thread_mask);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &thread_mask);
            add_event(REBIND_EVENT, cpuid+1);
            verbose(VB_OMPT, "Rebinding thread %d to CPU %d", index, cpuid);
        }
        add_event(BINDINGS_EVENT, sched_getcpu()+1);
    } else if (endpoint == ompt_scope_end) {
        add_event(REBIND_EVENT,   0);
        add_event(BINDINGS_EVENT, 0);
    }
}

static void cb_thread_begin(
        ompt_thread_t thread_type,
        ompt_data_t *thread_data) {
}

static void cb_thread_end(
        ompt_data_t *thread_data) {
}


/*********************************************************************************/
/*  OMPT start tool                                                              */
/*********************************************************************************/

static bool ompt_initialized = false;

static inline int set_ompt_callback(ompt_set_callback_t set_callback_fn,
        ompt_callbacks_t event, ompt_callback_t callback) {
    int error = 1;
    switch (set_callback_fn(event, callback)) {
        case ompt_set_error:
            verbose(VB_OMPT, "OMPT callback %d failed", event);
            break;
        case ompt_set_never:
            verbose(VB_OMPT, "OMPT callback %d registered but will never be called", event);
            break;
        default:
            error = 0;
    }
    return error;
}

static int ompt_initialize(ompt_function_lookup_t lookup, int initial_device_num,
        ompt_data_t *tool_data) {
    /* Parse options and get the required fields */
    options_t options;
    options_init(&options, NULL);
    pid = options.preinit_pid ? options.preinit_pid : getpid();

    /* Enable experimental OMPT only if requested */
    if (options.ompt) {
        /* Initialize DLB only if ompt is enabled, otherwise DLB_Finalize won't be called */
        int err = DLB_Init(0, NULL, NULL);
        if (err != DLB_SUCCESS) {
            warning("DLB_Init: %s", DLB_Strerror(err));
        }

        verbose(VB_OMPT, "Initializing OMPT module");

        ompt_set_callback_t set_callback_fn =
            (ompt_set_callback_t)lookup("ompt_set_callback");
        if (set_callback_fn) {
            err = 0;
            err += set_ompt_callback(set_callback_fn,
                    ompt_callback_thread_begin,   (ompt_callback_t)cb_thread_begin);
            err += set_ompt_callback(set_callback_fn,
                    ompt_callback_thread_end,     (ompt_callback_t)cb_thread_end);
            err += set_ompt_callback(set_callback_fn,
                    ompt_callback_parallel_begin, (ompt_callback_t)cb_parallel_begin);
            err += set_ompt_callback(set_callback_fn,
                    ompt_callback_parallel_end,   (ompt_callback_t)cb_parallel_end);
            err += set_ompt_callback(set_callback_fn,
                    ompt_callback_implicit_task,  (ompt_callback_t)cb_implicit_task);

            if (!err) {
                verbose(VB_OMPT, "OMPT callbacks succesfully registered");
            }

            omp_thread_manager__init(&options);
        } else {
            verbose(VB_OMPT, "Could not look up function \"ompt_set_callback\"");
        }

        ompt_initialized = true;

        // return a non-zero value to activate the tool
        return 1;
    }

    return 0;
}

static void ompt_finalize(ompt_data_t *tool_data) {
    if (ompt_initialized) {
        verbose(VB_OMPT, "Finalizing OMPT module");
        omp_thread_manager__finalize();
        DLB_Finalize();
    }
}


#pragma GCC visibility push(default)
ompt_start_tool_result_t* ompt_start_tool(unsigned int omp_version, const char *runtime_version) {
    static ompt_start_tool_result_t ompt_start_tool_result = {
        .initialize = ompt_initialize,
        .finalize   = ompt_finalize,
        .tool_data  = {0}
    };
    return &ompt_start_tool_result;
}
#pragma GCC visibility pop

/*********************************************************************************/

#ifdef DEBUG_VERSION
/* Static type checking */
static __attribute__((unused)) void ompt_type_checking(void) {
    { ompt_initialize_t                 __attribute__((unused)) fn = ompt_initialize; }
    { ompt_finalize_t                   __attribute__((unused)) fn = ompt_finalize; }
    { ompt_callback_thread_begin_t      __attribute__((unused)) fn = cb_thread_begin; }
    { ompt_callback_thread_end_t        __attribute__((unused)) fn = cb_thread_end; }
    { ompt_callback_parallel_begin_t    __attribute__((unused)) fn = cb_parallel_begin; }
    { ompt_callback_parallel_end_t      __attribute__((unused)) fn = cb_parallel_end; }
    { ompt_callback_implicit_task_t     __attribute__((unused)) fn = cb_implicit_task; }
}
#endif
