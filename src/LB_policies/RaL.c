/*************************************************************************************/
/*      Copyright 2012 Barcelona Supercomputing Center                               */
/*                                                                                   */
/*      This file is part of the DLB library.                                        */
/*                                                                                   */
/*      DLB is free software: you can redistribute it and/or modify                  */
/*      it under the terms of the GNU Lesser General Public License as published by  */
/*      the Free Software Foundation, either version 3 of the License, or            */
/*      (at your option) any later version.                                          */
/*                                                                                   */
/*      DLB is distributed in the hope that it will be useful,                       */
/*      but WITHOUT ANY WARRANTY; without even the implied warranty of               */
/*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                */
/*      GNU Lesser General Public License for more details.                          */
/*                                                                                   */
/*      You should have received a copy of the GNU Lesser General Public License     */
/*      along with DLB.  If not, see <http://www.gnu.org/licenses/>.                 */
/*************************************************************************************/

#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <strings.h>

#include "LB_numThreads/numThreads.h"
#include "LB_comm/shmem_lewi_mask.h"
#include "support/debug.h"
#include "support/globals.h"
#include "support/tracing.h"
#include "support/utils.h"
#include "support/mask_utils.h"

static int nthreads;

/*static int iterNum;
static struct timespec iterCpuTime;
static struct timespec iterMPITime;*/


/******* Main Functions - LeWI Mask Balancing Policy ********/

void RaL_Init( void )
{
   debug_config ( "LeWI Mask Balancing Init\n" );

   nthreads = _default_nthreads;

   //Initialize iterative info
/*   iterNum=0;
   reset(&iterCpuTime);
   reset(&iterMPITime);   */

   //Initialize shared memory
   cpu_set_t default_mask;
   get_mask( &default_mask );
   shmem_lewi_mask_init( &default_mask );

   if ( _aggressive_init ) {
      cpu_set_t mask;
      CPU_ZERO( &mask );

      int i;
      for ( i = 0; i < mu_get_system_size(); i++ ) {
         CPU_SET( i, &mask );
      }
      set_mask( &mask );
      set_mask( &default_mask );
   }
}

void RaL_Finish( void )
{
   set_mask( shmem_lewi_mask_recover_defmask() );
   shmem_lewi_mask_finalize();
}

void RaL_IntoCommunication( void ) {}

void RaL_OutOfCommunication( void ) {}

/* Into Blocking Call - Lend the maximum number of threads */
void RaL_IntoBlockingCall(int is_iter)
{
   cpu_set_t mask;
   CPU_ZERO( &mask );
   get_mask( &mask );

   cpu_set_t cpu;
   CPU_ZERO( &cpu );
   sched_getaffinity( 0, sizeof(cpu_set_t), &cpu);

   if ( _blocking_mode == ONE_CPU ) {
      // Remove current cpu from the mask
      CPU_XOR( &mask, &mask, &cpu );
      debug_lend ( "LENDING %d threads\n", nthreads-1 );
      nthreads = 1;
   }
   else if ( _blocking_mode == BLOCK ) {
      debug_lend ( "LENDING %d threads\n", nthreads );
      nthreads = 0;
   }

   set_mask( &cpu );
   shmem_lewi_mask_add_mask( &mask );
   add_event( THREADS_USED_EVENT, nthreads );
}

/* Out of Blocking Call - Recover the default number of threads */
void RaL_OutOfBlockingCall(int is_iter )
{
   debug_lend ( "RECOVERING %d threads\n", _default_nthreads - nthreads );
   set_mask( shmem_lewi_mask_recover_defmask() );
   nthreads = _default_nthreads;

   add_event( THREADS_USED_EVENT, nthreads );
}

/* Update Resources - Try to acquire foreign threads */
void RaL_UpdateResources( int max_resources )
{
   cpu_set_t mask;
   CPU_ZERO( &mask );
   get_mask( &mask );

   int new_threads;
   bool dirty = shmem_lewi_mask_collect_mask( &mask, max_resources, &new_threads );

   if ( dirty ) {
      nthreads += new_threads;
      set_mask( &mask );
      debug_lend ( "ACQUIRING %d threads for a total of %d\n", new_threads, nthreads );
      add_event( THREADS_USED_EVENT, nthreads );
   }
}