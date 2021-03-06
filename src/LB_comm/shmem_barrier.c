/*********************************************************************************/
/*  Copyright 2009-2018 Barcelona Supercomputing Center                          */
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

#include "LB_comm/shmem_barrier.h"

#include "LB_core/DLB_kernel.h"
#include "LB_comm/shmem.h"
#include "support/debug.h"
#include "support/mask_utils.h"

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    bool initialized;
    int count;
    int participants;
    sem_t sem;
} barrier_t;

typedef struct {
    barrier_t barriers[0];
} shdata_t;

enum { SHMEM_BARRIER_VERSION = 1 };

static int global_barrier_ids[2] = {0, 1};
static int local_barrier_id = 0;
static int max_barriers;
static shmem_handler_t *shm_handler = NULL;
static shdata_t *shdata = NULL;
static const char *shmem_name = "barrier";

static barrier_t* get_barrier() {
    int bid = global_barrier_ids[local_barrier_id];
    return &shdata->barriers[bid];
}
static void advance_barrier() {
    local_barrier_id = (local_barrier_id+1)%2;
}

void shmem_barrier_init(const char *shmem_key) {
    // Protect double initialization
    if (shm_handler != NULL) {
        warning("Shared Memory is being initialized more than once");
        print_backtrace();
        return;
    }

    // more error checks to be done if the option is implemented
    global_barrier_ids[0] = getenv("LB_BARRIER_ID") ? atoi(getenv("LB_BARRIER_ID")) : 0;
    global_barrier_ids[1] = global_barrier_ids[0]+1;
//    int qty = getenv("LB_BARRIER_QTY") ? atoi(getenv("LB_BARRIER_QTY")) : 0;

    max_barriers = mu_get_system_size()*2;

    shmem_handler_t *init_handler = shmem_init((void**)&shdata,
            sizeof(shdata_t) + sizeof(barrier_t)*max_barriers,
            shmem_name, shmem_key, SHMEM_BARRIER_VERSION);

    shmem_lock(init_handler);
    {
        // Initialize both barriers, only first process to arrive
        int i;
        for (i=0; i<2; ++i) {
            barrier_t *barrier = get_barrier();
            if (!barrier->initialized) {
                /* Create Unnamed Semaphore(0): Barrier */
                int error = sem_init(&barrier->sem, /*shared*/ 1, /*value*/ 0 );
                if (error) {
                    perror( "DLB_PANIC: Process unable to create/attach to semaphore (barrier)" );
                    exit( EXIT_FAILURE );
                }

                barrier->count = 0;
                barrier->participants = 0;
                barrier->initialized = true;
            }
            barrier->participants++;
            warning("barrier participants: %d", barrier->participants);
            advance_barrier();
        }
    }
    shmem_unlock(init_handler);

    // Global variable is only assigned after the initialization
    shm_handler = init_handler;
}

void shmem_barrier_finalize(void) {
    if (shm_handler == NULL) return;

    shmem_lock(shm_handler);
    {
        int i;
        for (i=0; i<2; ++i) {
            barrier_t *barrier = get_barrier();
            if (barrier->initialized) {
                // TODO check if sempahore is not being used

                // Decrement participants
                --barrier->participants;

                // Nulllify barrier only if last participant
                if (barrier->participants == 0) {
                    if (sem_destroy(&barrier->sem)) {
                        perror("DLB ERROR: sem_close");
                    }
                    barrier->initialized = false;
                }
            }
            advance_barrier();
        }
    }
    shmem_unlock(shm_handler);

    shmem_finalize(shm_handler, SHMEM_DELETE);
    shm_handler = NULL;
}

void shmem_barrier(void) {
    if (shm_handler == NULL) return;

    barrier_t *barrier = get_barrier();

    if (!barrier->initialized) {
        warning("Trying to use a non initialized barrier");
        return;
    }

    bool last_in;
    shmem_lock(shm_handler);
    {
        last_in = ++barrier->count == barrier->participants;
    }
    shmem_unlock(shm_handler);

    if (last_in) {
        // Last process entering the barrier must signal someone
        sem_post(&barrier->sem);
    } else {
        // Only if this process is not the last one, act as a blocking call
        IntoBlockingCall(0, 0);
    }

    // Wait until everyone is in here
    sem_wait(&barrier->sem);

    if (!last_in) {
        // Recover resources for those processes that simulated a blocking call
        OutOfBlockingCall(0);
    }

    bool last_out;
    shmem_lock(shm_handler);
    {
        last_out = --barrier->count  == 0;
    }
    shmem_unlock(shm_handler);

    if (!last_out) {
        // Everyone except the last process out must signal the next one
        sem_post(&barrier->sem);
    }

    advance_barrier();
}

int shmem_barrier__version(void) {
    return SHMEM_BARRIER_VERSION;
}

size_t shmem_barrier__size(void) {
    return sizeof(shdata_t) + sizeof(barrier_t)*mu_get_system_size();
}
