/*
   ROCm GDB debugging initialization and communication files

   Copyright (c) 2015-2016 ADVANCED MICRO DEVICES, INC.  All rights reserved.
   This file includes code originally published under

   Copyright (C) 1986-2014 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#include <ctype.h>
#include <stdbool.h>
#include "stdio.h"
#include <stdlib.h>
#include <string.h> /* For memset*/

/* Headers for signals */
#include <signal.h>
#include <sys/types.h>
#include <time.h>

/* Headers for shared mem */
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/* GDB headers */
#include "defs.h"
#include "event-loop.h"
#include "expression.h"
#include "format.h"
#include "gdb_assert.h"
#include "ui-out.h"
#include "valprint.h"
#include "value.h"

/*GDB headers - added for notifications of GPU solib*/
#include "breakpoint.h"
#include "observer.h"
#include "solib.h"
#include "solist.h"
#include "target.h"

/* ROCm-gdb headers */
#include "rocm-breakpoint.h"
#include "rocm-cmd.h"
#include "rocm-dbginfo.h"
#include "rocm-device.h"
#include "rocm-fifo-control.h"
#include "rocm-infcmd.h"
#include "rocm-kernel.h"
#include "rocm-print.h"
#include "rocm-segment-loader.h"
#include "rocm-thread.h"
#include "rocm-trace.h"
#include "rocm-tdep.h"
#include "rocm-utils.h"

/* Include HwDbgFacilities C interface*/
#include "FacilitiesInterface.h"

/* The header files shared with the agent, gdb has a shadow copy */
#include "CommunicationControl.h"
#include "CommunicationParams.h"

void hsail_tdep_print_notification_type(const HsailNotification notification);

/* The HSAIL agent should be closed down only once.
 *
 * If hsail is initialized then this variable is set to 1 */
static int gs_is_hsail_initialized = 0;

/* boolean flag that denotes whether we are now in dispatch or not */
static bool gs_is_hsail_focus_device = 0;

static HsailPredispatchState gs_hsail_predispatch_state = HSAIL_PREDISPATCH_STATE_UNKNOWN;

/* The FIFO's descriptor, used in rocm-fifo-control */
int g_hsail_fifo_descriptor = 0;
int g_hsail_fifo_read_descriptor = 0;

static int gs_num_handle_event_function_calls = 0;

static int gs_num_active_waves=-1;

static bool gs_stage2_has_run = false;


static int gs_gpuBkptCrtCount = 0;

/* Return the key for the shared mem location that has the binary*/
const int hsail_get_agent_binary_shmem_key(void)
{
  return g_DBEBINARY_SHMKEY;
}

/* Return the max size for the shared mem location that has the binary*/
const int hsail_get_agent_binary_shmem_max_size(void)
{
  return g_BINARY_BUFFER_MAXSIZE;
}


/* Return the key for the shared mem location that has the binary*/
const int hsail_get_wave_buffer_shmem_key(void)
{
  return g_WAVE_BUFFER_SHMKEY;
}

/* Return the max size for the shared mem location that has the wave info*/
const int hsail_get_wave_buffer_shmem_max_size(void)
{
  return g_WAVE_BUFFER_MAXSIZE;
}

/* Return the key for the shared mem location that has the momentary bp list*/
const int hsail_get_momentary_bp_buffer_shmem_key(void)
{
  return g_MOMENTARY_BP_BUFFER_SHMKEY;
}

/* Return the max size for the shared mem location that has momentary bp list*/
const int hsail_get_momentary_bp_buffer_shmem_max_size(void)
{
  return g_MOMENTARY_BP_BUFFER_MAXSIZE;
}


/* Return the key for the shared mem location that has the loaded segment list*/
static const int hsail_get_loadmap_buffer_shmem_key(void)
{
  return g_LOADMAP_SHMKEY;
}

/* Return the max size for the shared mem location that has the loaded segment list*/
static const int hsail_get_loadmap_buffer_shmem_max_size(void)
{
  return g_LOADMAP_MAXSIZE;
}

static void
gpu_solib_loaded (struct so_list *solib)
{
  gdb_assert(solib->so_original_name != NULL);
  if (strstr(solib->so_original_name, "libAMDHSADebugAgent") != NULL)
    {
      /* This name has to map the function in the agent */
      const char * hsaBreakpointLocation = "TriggerGPUBreakpointStop";

      solib_add(solib->so_name, 0, &current_target, 1);

      /* The internal agent breakpoint only need to be created once. */
      if (0 == gs_gpuBkptCrtCount)
        {
          /* Place the breakpoint in the GPUDebugSDK library
           * that will be hit when a gpu breakpoint is hit.
           */
          create_hsa_gpu_breakpoint_trigger(hsaBreakpointLocation);
          gs_gpuBkptCrtCount++;
        }
    }
}

/* Include all post create steps here */
void hsail_linux_post_inferior_create(void)
{
  /* add the observer to set a breakpoint in the GPU Debug Agent*/
  observer_attach_solib_loaded(gpu_solib_loaded);

  /* Do any loader setup steps*/
  hsail_segment_initialize_loader();
}

/*
 * The close down function hsail_linux_close resets the gs_stage2_has_run to false
 */
void hsail_linux_initialize_stg2(void)
{
   /* This static bool ensures that the initialization stage happens only once
   * Cleaner here than polluting linux-nat.c
   * */

  struct ui_out* uiout = current_uiout;
  int fd = 0;

  gdb_assert(NULL != uiout);

  if (!gs_stage2_has_run)
    {
      /*ui_out_text(uiout,"gdb: Initialize write fifo\n");*/
      fflush(stdout);

      fd = open(gs_GdbToAgentFifoName, O_WRONLY);

      if (fd <=  0)
        {
          printf( "ROCm-gdb: Error in Open Fifo...%d\n",errno);
          ui_out_text(uiout, "ROCm-gdb: Error in Open Fifo...Write End\n");
        }
      else
        {
          g_hsail_fifo_descriptor = fd;
        }

      /*
       * The shared memory for the dbe binary is created in the agent
       */

      gs_is_hsail_initialized = 1;
      ui_out_text(uiout,"[ROCm-gdb: GPU Debugging has been successfully initialized]\n");
      /*ui_out_text(uiout,"gdb: Finished Initialize write fifo\n");*/

      /* Clear kernel chain if there is any. */
      hsail_kernel_clear_chain();

      /* Initialize tracing if requested by the command line */
      hsail_trace_initialize();

      /* Re-enable the internal logging since HSAIL has been initialized. */
      hsail_cmd_reset_internal_logging();

      fflush(stdout);
      gs_stage2_has_run = true;
    }
}

static bool gs_stage1_has_run = false;

/*
 * The close down function hsail_linux_close resets the gs_stage1_has_run to false
 */
void hsail_linux_initialize_stg1(void)
{
  /* This static bool ensures that the initialization happens only once
   * Cleaner here than polluting linux-nat.c
   * */

  struct ui_out* uiout = current_uiout;
  int fd = 0;

  gdb_assert(NULL != uiout);

  if (!gs_stage1_has_run)
    {
      /*ui_out_text(uiout,"gdb: Initialize Nonblocking read fifo\n");*/
      fflush(stdout);

      fd = open(gs_AgentToGdbFifoName, O_RDONLY | O_NONBLOCK);

      if (fd <=  0)
        {
          printf( "GDB: Error in Open Fifo...%d\n",errno);
          ui_out_text(uiout, "GDB: Error in Open Fifo...Write End\n");
        }
      else
        {
          g_hsail_fifo_read_descriptor = fd;
        }

      /* We are going to not worry about event-loop's implementation
       * and rely on GDB to call the function below. */
      add_file_handler (hsail_get_read_fifo_handler(),
                        handle_hsail_event,
                        NULL);
      gs_stage1_has_run = true;
    }
}

/**
 * This function deletes the shared memory from the gdb side too.
 * Even though we already do this in the agent, we need to do it here too so that
 * the command ipcs does not show the buffer
 */
static bool hsail_linux_delete_shmem(const key_t shm_key, const int max_buffer_size)
{
  int shm_status = 0;
  int shm_errno = 0;
  key_t key = shm_key;
  void* pShm = NULL;
  int shmid = shmget(key, max_buffer_size, 0666);
  shm_errno = errno;
  if (0 > shmid)
    {
      /* memory was already deleted already deleted by the other consumer
         so we shouldnt do much more. We will look for the memory and delete it
         if we can succesfully get a shmid
      */
      if (shmid == -1)
        {
          return true;
        }
      else
        {
          /* Some strange error, we should always get -1 */
          printf("shmget returned invalid id %d \n", shmid);
          printf("shm id is invalid %d \n", shm_errno);
          return false;
        }
    }

  // Get the pointer to the shmem segment,
  pShm = shmat(shmid, NULL, 0);
  shm_errno = errno;
  gdb_assert(NULL != pShm);
  if (pShm == (int*)-1)
    {
      printf("shm id is NULL %d \n", shm_errno);
      return false;
    }

  // Detach so we can be sure we dont use it anymore
  if (shmdt(pShm) == -1)
    {
      printf("shmdt error %d \n", shm_errno);
      return false;
    }

  shm_status = 0;
  // Detach and free it
  shm_status = shmctl(shmid, IPC_RMID, NULL);
  /* save errno to a local variable*/
  shm_errno = errno;

  if (0 > shm_status)
    {
      printf("shm status is %d\n",shm_status);
      printf("shm errno is %d\n",shm_errno);
      fflush(stdout);
      return false;
    }

  return true;
}

/* Number of active waves */
static void hsail_tdep_set_active_wave_count(const int num_active_waves)
{
  gs_num_active_waves = num_active_waves;
}

int hsail_tdep_get_active_wave_count(void)
{
  return gs_num_active_waves;
}

bool hsail_tdep_kill_all_waves(bool is_quit_command)
{
  char funcExp[128];
  struct ui_out *uiout;
  uiout = current_uiout;

  bool ret_code = false;

  struct expression* expr = NULL;
  struct value* retVal = NULL;
  memset(funcExp,'\0',128);

  if(is_hsail_linux_initialized())
    {
      if((hsail_is_focus_device() == true) &&
        (hsail_tdep_get_active_wave_count() > 0))
        {

          ui_out_text(uiout, "[ROCm-gdb: Waiting to kill dispatch]\n");

          if (is_quit_command)
            {
              sprintf(funcExp, "KillHsailDebug(1)");
            }
          else
            {
              sprintf(funcExp, "KillHsailDebug(0)");
            }

          /* Create the expression */
          expr = parse_expression (funcExp);

          /* Call the evaluator */
          retVal = evaluate_expression (expr);
          if (NULL != retVal)
            {
              ret_code = true;
            }
        }
    }
  else
    {
        /* If hsail already been kill */
        ret_code = true;
    }

  return ret_code;
}

bool hsail_tdep_save_isa(bool is_disassemble_command, const char* hsail_isa_file_name)
{

  void* pIsaShm = NULL;
  FILE* temp_file_handle = NULL;
  char* isa_location = NULL;

  /* Valid if nothing in this function is called, it just means we didnt ask for ISA*/
  bool ret_code = true;
  gdb_assert(hsail_isa_file_name != NULL);


  if(hsail_cmd_get_show_isa_option() == true || is_disassemble_command)
    {
      size_t isa_size;

      if (!hsail_utils_read_file_to_array(gs_ISAFileNamePath,
                                          &isa_location,
                                          &isa_size))
        {
          return false;
        }

      /* No isa found*/
      if (isa_size ==  0 || isa_location == NULL)
        {
          return false;
        }

      /* set permissions */
      if (hsail_utils_check_file_exists(hsail_isa_file_name))
        {
          if (!hsail_utils_set_file_permission(hsail_isa_file_name, HSAIL_FILE_READ_WRITE))
            {
              printf("Could not make ISA file read-write");
            }
        }

      temp_file_handle = fopen(hsail_isa_file_name,"wb");
      gdb_assert(temp_file_handle != NULL);

      fprintf(temp_file_handle, "%s",isa_location);
      fprintf(temp_file_handle,"\n");

      rewind(temp_file_handle);
      fclose(temp_file_handle);

      if (!hsail_utils_set_file_permission(hsail_isa_file_name, HSAIL_FILE_READ_ONLY))
        {
          printf("Cold not make ISA file read-only");
          ret_code = false;
        }
      else
        {
          ret_code = true;
        }
    }

  if (hsail_cmd_get_show_isa_option() == true)
    {
      printf_filtered("GPU ISA disassembly saved to temp_isa\n");
    }
  return ret_code;
}


void* hsail_tdep_map_loadmap_buffer(void)
{
  struct ui_out* uiout = current_uiout;

  void* pShm = NULL;
  int shmid = -1;
  const int max_shared_mem_size = hsail_get_loadmap_buffer_shmem_max_size();

  gdb_assert(NULL != uiout);
  /*1M used is hard wired for now*/

  if (hsail_is_focus_device() == false || is_hsail_linux_initialized() == false)
    {
      return NULL;
    }

  shmid = shmget(hsail_get_loadmap_buffer_shmem_key(), max_shared_mem_size, 0666);

  if (shmid <= 0)
    {
      ui_out_text(uiout, "wave info buffer mapping: shmid is invalid\n");
    }

  gdb_assert(shmid > 0);

  /* Get shm pointer */
  pShm = (void*)shmat(shmid, NULL, 0);

  gdb_assert(pShm != NULL);

  return pShm;
}

void* hsail_tdep_map_momentary_bp_buffer(void)
{
  struct ui_out* uiout = current_uiout;

  void* pShm = NULL;
  int shmid = -1;
  int max_shared_mem_size = hsail_get_momentary_bp_buffer_shmem_max_size();

  gdb_assert(NULL != uiout);

  if (hsail_is_focus_device() == false || is_hsail_linux_initialized() == false)
    {
      return NULL;
    }

  shmid = shmget(hsail_get_momentary_bp_buffer_shmem_key(),
                 max_shared_mem_size, 0666);

  if (shmid <= 0)
    {
      ui_out_text(uiout, "momentary_bp_buffer mapping: shmid is invalid\n");
    }

  gdb_assert(shmid > 0);

  /* Get shm pointer */
  pShm = (void*)shmat(shmid, NULL, 0);

  gdb_assert(pShm != NULL);

  return pShm;
}

/* Map and unmap the wave buffer from the shared memory */
void* hsail_tdep_map_wave_buffer(void)
{
  struct ui_out* uiout = current_uiout;

  void* pShm = NULL;
  int shmid = -1;
  const int max_shared_mem_size = hsail_get_wave_buffer_shmem_max_size();

  gdb_assert(NULL != uiout);
  /*1M used is hard wired for now*/

  if (hsail_is_focus_device() == false || is_hsail_linux_initialized() == false)
    {
      return NULL;
    }

  shmid = shmget(hsail_get_wave_buffer_shmem_key(), max_shared_mem_size, 0666);

  if (shmid <= 0)
    {
      ui_out_text(uiout, "wave info buffer mapping: shmid is invalid\n");
    }

  gdb_assert(shmid > 0);

  /* Get shm pointer */
  pShm = (void*)shmat(shmid, NULL, 0);

  gdb_assert(pShm != NULL);

  return pShm;
}

void hsail_tdep_unmap_shm_buffer(void* pShm)
{
  struct ui_out* uiout = current_uiout;
  gdb_assert(NULL != uiout);
  gdb_assert(NULL != pShm);

  /* Detach shared memory */
  if (shmdt(pShm) == -1)
    {
      ui_out_text(uiout, "GDB: Error detaching buffer\n");
    }

}

/*
 * This function should be called only once, for each run of the inferior
 */

void hsail_linux_close(void)
{
  struct ui_out* uiout = current_uiout;

  gdb_assert(NULL != uiout);

  if(is_hsail_linux_initialized() == 1)
    {
      int status = 0;
      int fifo_descriptor = hsail_get_fifo_handler();
      bool is_shm_closed = false;
      /* fifo_descriptor can be 0 if close function is reached without execution of a debugged application
       * for example if the user supplied a wrong executable path. In this case assertion should not be raised
       */
      if (fifo_descriptor > 0)
        {
          /* \todo Add error checking of status */
          status = close(fifo_descriptor );

          /* Delete fifo from file system*/
          unlink(gs_GdbToAgentFifoName);
        }

      /* use the variable directly and not the function since there is an assertion check there that might happen
       * but actually might be valid this is a special case but in other cases hsail_get_read_fifo_handler() should be used
       */
      fifo_descriptor = g_hsail_fifo_read_descriptor;
      if (fifo_descriptor > 0)
        {
          /* Just deleting the file handler here does not seem to be enough.
           * The problem is that some events may already have been enqueued on this descriptor
           */
          delete_file_handler(fifo_descriptor);

          status = close(fifo_descriptor );

          /* Delete fifo from file system*/
          unlink(gs_AgentToGdbFifoName);
        }


      /* We can assert on is_shm_closed() since if the shared memory ID cannot be found,
       * we just dont do the deletion. The assertion catches what happened during the
       * actual deletion
       * */
      is_shm_closed = hsail_linux_delete_shmem(g_DBEBINARY_SHMKEY, g_BINARY_BUFFER_MAXSIZE);
      if (!is_shm_closed)
        {
          ui_out_text(uiout, "GDB: Binary buffer could not be detached\n");
        }
      gdb_assert(is_shm_closed == true);

      is_shm_closed = hsail_linux_delete_shmem(g_WAVE_BUFFER_SHMKEY, g_WAVE_BUFFER_MAXSIZE);
      if (!is_shm_closed)
        {
          ui_out_text(uiout, "GDB: Wave buffer could not be detached\n");
        }
      gdb_assert(is_shm_closed == true);

      is_shm_closed = hsail_linux_delete_shmem(g_MOMENTARY_BP_BUFFER_SHMKEY, g_MOMENTARY_BP_BUFFER_MAXSIZE);
      if (!is_shm_closed)
        {
          ui_out_text(uiout, "GDB: Momentary BP buffer could not be detached\n");
        }
      gdb_assert(is_shm_closed == true);

      /* Close tracing if it is on*/
      hsail_trace_stop();

      /* Explicitly set state that denotes that the hsail agent is no longer there
       * by setting the  global state for closed
       *
       * However this will help catch regressions if anything tries to do hsail debug
       * again since it will see that initialization is not present.
       */
      gs_is_hsail_initialized = 0;

      /* Anything trying to access the fifo should complain now */
      g_hsail_fifo_descriptor = 0;
      g_hsail_fifo_read_descriptor = 0;

      gs_stage1_has_run = false;
      gs_stage2_has_run = false;
      delete_hsa_agent_internal_breakpoint();
      gs_gpuBkptCrtCount = 0;

      /* Shut the loader */
      hsail_segment_shutdown_loader();
    }

  gdb_assert(is_hsail_linux_initialized() == 0);
}


int is_hsail_linux_initialized(void)
{
  /* We should not be handing out the fifos handler before setup
   * or after hsail_agent_close() is called.
   * However, we cannot assert "gdb_assert(g_hsail_fifo_descriptor > 0)"
   * in this check since a negative answer is perfectly reasonable
   */

  return gs_is_hsail_initialized;
}

bool hsail_is_focus_predispatch(void)
{
  return (gs_hsail_predispatch_state == HSAIL_PREDISPATCH_ENTERED_PREDISPATCH);
}

bool hsail_is_focus_device(void)
{
  return gs_is_hsail_focus_device;
}

/* \todo This needs to be fixed to consider the focus wave and return it's PC
 * */
uint64_t hsail_tdep_get_current_pc(void)
{
  uint64_t ret = 0;
  int num_waves = hsail_tdep_get_active_wave_count();
  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();

  /* The wave address can be NULL if the user enters print hsail:foo
   * when we are not in kernel debugging
   * */

  /* assuming the for this version the addr is taken from wave[0] addr
   * In future we need to get from the user which wave he wants the addr to be taken from (maybe print hsail:var:wave)
   */
  if (NULL == wave_info_buffer || 0 >= num_waves)
  {
    /* At this point we should have at least one wave */
    return 0;
  }

  ret = wave_info_buffer[0].pc;

  /* release the wave buffer */
  hsail_tdep_unmap_shm_buffer((void*)wave_info_buffer);

  return ret;
}

bool hsail_is_signal_from_agent(const char* signal_name)
{
  /* This is a strict check for SIGUSR2 only*/
  return (strcmp(signal_name, "User defined signal 2") == 0);

  /* This is a strict check for SIGCHLD only*/
  /* return (strcmp(signal_name, "Child status changed") == 0);*/
}

/* Return the FIFO's descriptor.
 * It is this function's caller's job to check the return value if the FIFO
 * is supposed to be open
 * */
int hsail_get_fifo_handler(void)
{
  /* We should not be handing out the fifos handler before setup
   * or after hsail_agent_close() is called.
   * However, we cannot assert "gdb_assert(g_hsail_fifo_descriptor > 0)"
   * in this check since a negative answer is perfectly reasonable
   */

  gdb_assert(g_hsail_fifo_descriptor > 0);

  return g_hsail_fifo_descriptor;
}

/* Return the agent-> gdb FIFO's read end descriptor */
int hsail_get_read_fifo_handler(void)
{
  /* We should not be handing out the fifos handler before setup
   * or after hsail_agent_close() is called */
  gdb_assert(g_hsail_fifo_read_descriptor > 0);

  return g_hsail_fifo_read_descriptor;
}



/* Just a debugging helper function */
void hsail_tdep_print_notification_type(const HsailNotification notification)
{
  switch (notification)
  {
    case HSAIL_NOTIFY_NEW_BINARY:
      {
        printf("Notification Type: HSAIL_NOTIFY_NEW_BINARY \n");
        break;
      }
    case HSAIL_NOTIFY_PREDISPATCH_STATE:
      {
        printf("Notification Type: HSAIL_NOTIFY_PREDISPATCH_STATE \n");
        break;
      }
    case HSAIL_NOTIFY_START_DEBUG_THREAD:
      {
        printf("Notification Type: HSAIL_NOTIFY_START_DEBUG_THREAD \n");
        break;
      }
    case HSAIL_NOTIFY_BREAKPOINT_HIT:
      {
        printf("Notification Type: HSAIL_NOTIFY_BREAKPOINT_HIT \n");
        break;
      }
    case HSAIL_NOTIFY_BEGIN_DEBUGGING:
      {
        printf("Notification Type: HSAIL_NOTIFY_BEGIN_DEBUGGING \n");
        break;
      }
    case HSAIL_NOTIFY_END_DEBUGGING:
      {
        printf("Notification Type: HSAIL_NOTIFY_END_DEBUGGING \n");
        break;
      }
    case HSAIL_NOTIFY_FOCUS_CHANGE:
      {
        printf("Notification Type: HSAIL_NOTIFY_FOCUS_CHANGE \n");
        break;
      }
    case HSAIL_NOTIFY_AGENT_ERROR:
      {
        printf("Notification Type: HSAIL_NOTIFY_AGENT_ERROR \n");
        break;
      }
    case HSAIL_NOTIFY_KILL_COMPLETE:
      {
        printf("Notification Type: HSAIL_NOTIFY_KILL_COMPLETE \n");
        break;
      }
    case HSAIL_NOTIFY_DEVICES:
      {
        printf("Notification Type: HSAIL_NOTIFY_DEVICES \n");
        break;
      }
    default:
      printf_filtered("Unsupported notification type");
  }
}


/*
 * This function is called from "handle_file_event (event_data data)"
 * in eventloop.c
 */
void handle_hsail_event(int err, gdb_client_data client_data)
{

  struct ui_out* uiout = current_uiout;
  int read_status = 0;
  int bytes_read = 0;
  bool ret_code = false;
  HsailNotificationPayload fifo_data;
  HwDbgInfo_debug dbg = NULL;

  gdb_assert(NULL != uiout);

  /* Just a debug counter to track how many times the
   * handle_hsail_event function is called, result printed in linux_nat_close()
   * */
  gs_num_handle_event_function_calls = gs_num_handle_event_function_calls + 1;

  memset(&fifo_data,0,sizeof(HsailNotificationPayload));
  bytes_read  = read(hsail_get_read_fifo_handler(),
          &fifo_data,
          sizeof(HsailNotificationPayload));

  /* save errno to a local variable*/
  read_status = errno;

  /* We need to figure out why the event-loop implementation is called multiple times
   * when the FIFO is not ready with new data
   * This is not treated as a error for now
   * */
  if (bytes_read == -1 && read_status == EAGAIN)
    {
      return;
    }

  /*
   * Since the read fifo is created in a nonblocking manner.
   * The read returns instantly if there is no data on the fifo
   * with bytesRead = 0
   * */
  if (bytes_read != sizeof(HsailNotificationPayload))
    {
      /* We have some unusual error code.
       * We should atleast print it for now
       * This commonly happens if the shared header used between GDB and the agent are not in sync
       * */
      if(bytes_read != 0)
      {
          printf_filtered("Handle_hsail_event error %d\t Bytes read %d\t ",err, bytes_read);
          printf_filtered("Read fifo errno:  %d \n", read_status);
      }
    }
  else
    {
      /* Based on the data the agent sent us, call the right function in rocm-* files */

      /* Logging function to view notifications */
      /* hsail_tdep_print_notification_type(fifo_data.m_Notification);*/

      switch (fifo_data.m_Notification)
      {
        case HSAIL_NOTIFY_NEW_BINARY:
          {
            /* On this event,
             * 1) Free any existing debug facilities objects. We can do this since we know that
             * only one binary is active at any point in time, so the new binary notification
             * should come after the previous kernel has ended debugging
             *
             * 2) initialize debug facilities with the new binary
             * 3) flush the command buffer if there is anything left
             * 4) Add the dispatch to the list of kernels, and if a new kernel save to a file
             * */
            hsail_free_hwdbginfo();

            /* We set to HSAIL_AGENT_BINARY_AVAILABLE just to let hsail_init_hwdbginfo
             * know about the new binary
             * */
            hsail_dbginfo_set_facilities_status(HSAIL_AGENT_BINARY_AVAILABLE);

            /* We set to HSAIL_AGENT_BINARY_AVAILABLE if hsail_init_hwdbginfo
             * can initialize debug facilities with the new binary available.
             *
             * If the initialization fails, we restore the status to HSAIL_AGENT_BINARY_UNKNOWN
             * */

            dbg = NULL;
            dbg = hsail_init_hwdbginfo(&fifo_data);

            /* Save the kernel to the temp_source and update statistics  for the dispatch.
             *
             * It is possible that if debug facilities didn't initialize correctly, then the
             * kernel source buffer may not be present.
             * */
            ret_code = hsail_kernel_add_dispatch(&fifo_data);
            gdb_assert(ret_code == true);

            if (dbg != NULL)
              {
                hsail_dbginfo_set_facilities_status(HSAIL_AGENT_BINARY_AVAILABLE);

                /* Save the kernel's ISA */
                hsail_tdep_save_isa(false, "temp_isa");

              }
            else
              {

                rocm_printf_filtered("HSAIL kernel source debugging will not occur\n");

                hsail_dbginfo_set_facilities_status(HSAIL_AGENT_BINARY_UNKNOWN);
              }

            /* update the kernel launch trace */
            hsail_trace_add_dispatch(&fifo_data);

            hsail_flush_breakpoint_command_buffer();

            adjust_breakpoint_all_hsail();

            break;
          }
        case HSAIL_NOTIFY_PREDISPATCH_STATE:
          {
            gdb_assert(fifo_data.payload.PredispatchNotification.m_predispatchState
                       != HSAIL_PREDISPATCH_STATE_UNKNOWN);
            gs_hsail_predispatch_state = fifo_data.payload.PredispatchNotification.m_predispatchState;

            hsail_thread_set_dispatch_host_thread_pid(
                fifo_data.payload.PredispatchNotification.m_HostDispatchTid);
            break;
          }
        case HSAIL_NOTIFY_START_DEBUG_THREAD:
          {
            hsail_infcmd_set_dispatch_thread_pid(fifo_data.payload.StartDebugThreadNotification.m_tid);
            break;
          }
        case HSAIL_NOTIFY_BREAKPOINT_HIT:
          {
            hsail_breakpoint_update_statistics(fifo_data.payload.BreakpointHit.m_breakpointId,
                                               fifo_data.payload.BreakpointHit.m_hitCount,
                                               HSAIL_MAX_REPORTABLE_BREAKPOINTS);

            hsail_tdep_set_active_wave_count(fifo_data.payload.BreakpointHit.m_numActiveWaves);

            break;
          }
        case HSAIL_NOTIFY_BEGIN_DEBUGGING:
          {
            gs_is_hsail_focus_device = true;
            hsail_tdep_set_active_wave_count(0);
            break;
          }
        case HSAIL_NOTIFY_END_DEBUGGING:
          {
            /*We now focus on the host*/
            gs_is_hsail_focus_device = false;

            /* The binary we have now is invalid if and only if the dispatch has completed.
             *
             * This check handles cases where we end debugging with the DISABLE_DISPATCH
             * behavior flag and then restart debugging within the callback.
             */
            if (fifo_data.payload.EndDebugNotification.hasDispatchCompleted)
              {
                hsail_dbginfo_set_facilities_status(HSAIL_AGENT_BINARY_UNKNOWN);
              }

            hsail_tdep_set_active_wave_count(0);
            hsail_thread_clear_focus();
            rocm_unset_active_device();
            break;
          }
        case HSAIL_NOTIFY_FOCUS_CHANGE:
          {
            hsail_thread_set_focus(fifo_data.payload.FocusChange.m_focusWorkGroup,
                                   fifo_data.payload.FocusChange.m_focusWorkItem);
            break;
          }
        case HSAIL_NOTIFY_AGENT_ERROR:
          {
            printf_filtered("Agent Error: %d \n", fifo_data.payload.AgentErrorNotification.m_errorCode);
            break;
          }
        case HSAIL_NOTIFY_KILL_COMPLETE:
          {
            if (fifo_data.payload.KillCompleteNotification.killSuccessful)
              {
                hsail_tdep_set_active_wave_count(0);
              }
            else
              {
                printf_filtered("Could not kill waves safely");
              }
            break;
          }
        case HSAIL_NOTIFY_NEW_ACTIVE_WAVES:
          {
            hsail_tdep_set_active_wave_count(fifo_data.payload.NewActiveWaveNotification.m_numActiveWaves);
            break;
          }
        case HSAIL_NOTIFY_DEVICES:
          {
            rocm_set_devices(&fifo_data);
            break;
          }
        default:
          printf_filtered("Unsupported notification type");
      }
    }

}

/* Called when gdb is shut down */
void hsail_linux_do_final_cleanup(void)
{

  hsail_trace_stop();

  hsail_kernel_clear_chain();

  hsail_command_clear_argument_buff();

  hsail_free_command_buffer();
}
