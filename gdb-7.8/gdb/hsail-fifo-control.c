/*
   ROCm GDB functions to dispatch commands to inferior.

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

/* Added to remove a incompatible declaration warning with memset*/
#include <string.h>

/* Headers for signals */
#include <signal.h>
#include <sys/types.h>
#include <time.h>

/* GDB headers */
#include "defs.h"
#include "format.h"
#include "gdb_assert.h"
#include "ui-out.h"

/* The agent header file */
#include "CommunicationControl.h"

/* Hsail GDB headers */
#include "hsail-breakpoint.h"
#include "hsail-fifo-control.h"
#include "hsail-tdep.h"
#include "hsail-utils.h"


/* Maybe nicer to use a gdb structure like the breakpoint chain
 * but seems like overkill for now.
 * This type will need to change HsailBreakpointRequest
 * We presently have to traverse this whole array everytime since elements may get
 * removed in different order to added in
 */
static HsailBreakpointRequest* gs_hsail_request_buffer = NULL;
static int gs_hsail_breakpoint_request_buffer_len = 0;
static bool gs_hsail_is_command_buffer_initialized = false;

/* Const only relevant to this file, not in shared header since it does not need to be seen in the agent.
 * We will not have more than 1000 commands before hsail is even loaded.
 * */
static const int gs_hsail_max_command_buffer_len = 1024;

/* Helper to consistently clear each packet'ss memory.
 * That way each hsail_enqueue_* function only needs to update its own fields
 * */
static void hsail_fifo_initialize_packet(HsailCommandPacket* packet)
{
  int i = 0;
  gdb_assert(packet != NULL);
  if (packet == NULL)
    {
      return;
    }

  memset(packet, 0, sizeof(HsailCommandPacket));
  packet->m_command = HSAIL_COMMAND_UNKNOWN;
  packet->m_loggingInfo = HSAIL_LOGGING_UNKNOWN;
  packet->m_hitCount = -1;
  packet->m_gdbBreakpointID = -1;
  packet->m_pc = (uint64_t)HSAIL_ISA_PC_UNKOWN;
  packet->m_lineNum = -1;
  packet->m_numMomentaryBP =0;

  packet->m_conditionPacket.m_conditionCode = HSAIL_BREAKPOINT_CONDITION_UNKNOWN;
  packet->m_conditionPacket.m_workgroupID.x = -1;
  packet->m_conditionPacket.m_workgroupID.y = -1;
  packet->m_conditionPacket.m_workgroupID.z = -1;
  packet->m_conditionPacket.m_workitemID.x = -1;
  packet->m_conditionPacket.m_workitemID.y = -1;
  packet->m_conditionPacket.m_workitemID.z = -1;

  for (i=0; i < AGENT_MAX_SOURCE_LINE_LEN; i++)
    {
      packet->m_sourceLine[i] = '\0';
    }
  for (i=0; i < AGENT_MAX_FUNC_NAME_LEN; i++)
    {
      packet->m_kernelName[i] = '\0';
    }
}

/*
 * Simple sanity check for the hsail command packets
 * \todo verify that gdb_asssert works in release or any other modes
 */
static void hsail_validate_command_packet(const HsailCommandPacket packet)
{
  int valid = 0;
  switch (packet.m_command)
  {
    case HSAIL_COMMAND_CREATE_BREAKPOINT:
    case HSAIL_COMMAND_MOMENTARY_BREAKPOINT:
      if(packet.m_pc >= 0)
        {
          valid = 1 ;
        }
      break;
    case HSAIL_COMMAND_DELETE_BREAKPOINT:
      if(packet.m_gdbBreakpointID >= 0)
        {
          valid = 1 ;
        }
      break;
    case HSAIL_COMMAND_CONTINUE:
        valid = 1;
        break;
    case HSAIL_COMMAND_DISABLE_BREAKPOINT:
      if(packet.m_gdbBreakpointID >= 0)
        {
          valid = 1;
        }
      break;
    case HSAIL_COMMAND_SET_LOGGING:
      if(packet.m_loggingInfo > 0)
        {
          valid =1;
        }
      break;
    case HSAIL_COMMAND_UNKNOWN:
      valid = 0;
      break;
    default:
      valid = 0;
      break;
  }
  gdb_assert(valid == 1);
}

/* This function needs to be called really early but only once
 *
 * We have to initialize this once but it will need to be done from any of two places:
 *      Breakpoint subsystem: when we create a HSAIL breakpoint before the inferior starting
 *      HSAIL initialization: when we start the inferior
 *
 * The command cache is used in the pre hsail initialization stage when the fifo is not ready.
 * As soon as a fifo is open, we just write everything and then the cache is not used anymore.
 * */

void hsail_initialize_command_buffer(void)
{
  if (gs_hsail_is_command_buffer_initialized == false)
    {
      /* allocate and zero out buffer */

      int hsail_command_buffer_size = sizeof(HsailBreakpointRequest)*gs_hsail_max_command_buffer_len;

      gs_hsail_request_buffer = (HsailBreakpointRequest*)xmalloc(hsail_command_buffer_size );

      gdb_assert(gs_hsail_request_buffer != NULL);

      memset(gs_hsail_request_buffer,0,hsail_command_buffer_size );

      gs_hsail_is_command_buffer_initialized = true;
    }

}

void hsail_free_command_buffer(void)
{
  /* We cannot assert that the buffer is empty
   * If the user chooses to exit gdb prematurely.
   *
   * For this same reason, we cannot assert on the request_buffer being not null
   * We can only check its state and act accordingly
   * */
  if(gs_hsail_breakpoint_request_buffer_len != 0)
    {
      printf_filtered("The command buffer is not empty. Some ROCm commands may not have been applied\n");
    }

  if(gs_hsail_request_buffer != NULL)
    {
      xfree((void*)gs_hsail_request_buffer);
      gs_hsail_request_buffer = NULL;
    }

  gs_hsail_is_command_buffer_initialized = false;
}

/* Added to allow us to push the pending commands to the agent as soon as the fifo is open.
 * This will be called just after the hsail initialization is done.
 * */
void hsail_flush_breakpoint_command_buffer(void)
{
  int i = 0;
  int filedesc  = hsail_get_fifo_handler();

  if (gs_hsail_breakpoint_request_buffer_len == 0)
    {
      return;
    }

  if (!is_hsail_linux_initialized() || filedesc <= 0)
    {
      return;
    }

  /* We can assert for a valid handler now */
  gdb_assert(filedesc > 0);
  gdb_assert(gs_hsail_max_command_buffer_len > gs_hsail_breakpoint_request_buffer_len);
  gdb_assert(gs_hsail_request_buffer != NULL);
  /*We traverse the whole array for now*/
  for (i = 0; i < gs_hsail_max_command_buffer_len; i++)
    {
      HsailBreakpointType bp_type = gs_hsail_request_buffer[i].type;

      /*
       * We will save commands that the user gave us and send them to the agent, hoping
       * that the user knows the kernel names and so on
       * */
      switch (bp_type)
      {
        /* Checks that the location is zeroed out */
        case HSAIL_BP_TYPE_UNKNOWN:
          {
            break;
          }
        case HSAIL_BP_TYPE_KERNEL_FUNCTION:
          {
            /* This request is already in the buffer
             * We should do nothing if hsail isnt initialized since
             * this function will enqueue another request if we call it
             * when hsail is not initialized */
            gdb_assert(gs_hsail_request_buffer[i].bp.kernel_func.func_name != NULL);
            if (is_hsail_linux_initialized())
              {
                hsail_breakpoint_set_from_kernel_name(&gs_hsail_request_buffer[i]);

                /* We can clear out the element of the request buffer now */
                hsail_breakpoint_clear_bp_request(&gs_hsail_request_buffer[i]);
                gs_hsail_breakpoint_request_buffer_len--;
              }
            break;
          }
        case HSAIL_BP_TYPE_SOURCE_LOCATION:
          {
            if (is_hsail_linux_initialized()
                && hsail_is_debug_facilities_loaded())
              {
                hsail_breakpoint_set_from_line(&gs_hsail_request_buffer[i]);

                /* We can clear out the element of the request buffer now */
                hsail_breakpoint_clear_bp_request(&gs_hsail_request_buffer[i]);
                gs_hsail_breakpoint_request_buffer_len--;
              }
            /*We have not added support for this type yet*/
            break;
          }
        case HSAIL_BP_TYPE_ANY_LOCATION:
          {
            if (is_hsail_linux_initialized())
              {
                hsail_breakpoint_set_any(&gs_hsail_request_buffer[i]);

                /* We can clear out the element of the request buffer now */
                hsail_breakpoint_clear_bp_request(&gs_hsail_request_buffer[i]);
                gs_hsail_breakpoint_request_buffer_len--;
              }
            break;
          }
        default:
          {
            gdb_assert(0);
            break;
          }
        }
    }

  /* The buffer is now empty*/
//  gs_hsail_command_buffer_len = 0;
}

static void hsail_push_command(HsailCommandPacket packet)
{
  int bytes_written = 0;

  int file_desc = hsail_get_fifo_handler();

  gdb_assert (file_desc > 0);

  /* It may be tempting to add the call to flush the command buffer here too
   * hsail_flush_command_buffer();
   * However that is logically wrong since the flush is triggered from linux_nat_wait
   * and adding this caused an infinite recursion.
   */
  /*
  printf_filtered("ROCm push command %d \n",packet.m_command);
  fflush(stdout);
  */

  hsail_validate_command_packet(packet);

  gdb_assert(file_desc > 0);
  bytes_written = 0;

  bytes_written = write(file_desc, &packet ,sizeof(HsailCommandPacket));
  gdb_assert(bytes_written == sizeof(HsailCommandPacket));
}

/*
 * Create a breakpoint packet and put it to the fifo.
 * We could pass the hsail breakpoint request here but we also need to send PC
 */
void hsail_enqueue_create_breakpoint_packet(const int pc,
                                            const int gdb_bkpt_num,
                                            const char* src_line,
                                            const int line_num,
                                            const HsailBreakpointCondition* condition)
{
  HsailCommandPacket breakpoint_packet;

  gdb_assert(pc >= 0);
  gdb_assert(condition != NULL);
  gdb_assert(NULL != src_line);

  hsail_fifo_initialize_packet(&breakpoint_packet);

  breakpoint_packet.m_command = HSAIL_COMMAND_CREATE_BREAKPOINT;
  breakpoint_packet.m_gdbBreakpointID = gdb_bkpt_num;
  breakpoint_packet.m_pc = (uint64_t)pc;
  breakpoint_packet.m_lineNum = line_num;

  breakpoint_packet.m_conditionPacket.m_conditionCode = condition->condition_code;
  hsail_utils_copy_wavedim3(&(breakpoint_packet.m_conditionPacket.m_workgroupID),
                           &(condition->work_group_id));
  hsail_utils_copy_wavedim3(&(breakpoint_packet.m_conditionPacket.m_workitemID),
                           &(condition->work_item_id));

  strncpy(breakpoint_packet.m_sourceLine, src_line, AGENT_MAX_SOURCE_LINE_LEN);

  hsail_push_command(breakpoint_packet);
}

void hsail_enqueue_continue_dispatch_packet(void)
{
  HsailCommandPacket continue_packet;
  hsail_fifo_initialize_packet(&continue_packet);
  continue_packet.m_command = HSAIL_COMMAND_CONTINUE;

  hsail_push_command(continue_packet);
}

/*
 * Create a kernel name breakpoint packet and put it to the fifo.
 * We could pass the hsail breakpoint request here but we also need to send the meta data
 */
void hsail_enqueue_create_kernel_name_breakpoint_packet(const char* kernel_name,
                                                        const int gdb_bkpt_num)
{
  HsailCommandPacket breakpoint_packet;
  gdb_assert(NULL != kernel_name);

  hsail_fifo_initialize_packet(&breakpoint_packet);

  breakpoint_packet.m_command = HSAIL_COMMAND_CREATE_BREAKPOINT;
  breakpoint_packet.m_gdbBreakpointID = gdb_bkpt_num;

  strncpy(breakpoint_packet.m_kernelName, kernel_name, AGENT_MAX_FUNC_NAME_LEN);
  hsail_push_command(breakpoint_packet);
}

/* We can only delete from the cache in the general case by referencing
 * the gdb breakpoint numbers.
 *
 * Other fields like source / line / PC may not be available since
 * the breakpoint may  not have been resolved yet
 * */
void hsail_enqueue_delete_breakpoint_request_buffer(const int gdb_bkpt_id)
{
  int i = 0;
  bool is_found = false;

  /* It is possible for this function to be called if the hsail_request_buffer
   * has been deleted, such as when the inferior has terminated.
   * In that case GDB will delete the breakpoint structure and we dont need
   * to do anything more since subsequent runs of the inferior will not
   * enqueue anything to the buffer
   * */
  if (NULL == gs_hsail_request_buffer)
    {
      return;
    }

  gdb_assert(NULL != gs_hsail_request_buffer);

  for (i = 0; i < gs_hsail_max_command_buffer_len; i++)
    {

      if (gs_hsail_request_buffer[i].number == gdb_bkpt_id)
        {
          hsail_breakpoint_clear_bp_request(&gs_hsail_request_buffer[i]);
          gs_hsail_breakpoint_request_buffer_len --;

          is_found = true;
          break;
        }
    }
  if (!is_found)
    {
      printf("Could not find breakpoint %d in the breakpoint cache\n", gdb_bkpt_id);
    }
}

/* If the Fifo is not ready, we save the command to the pending hsail commands
 * for later. The commands will written once the fifo is initialized.
 * Save the request to the command bufffer */

void hsail_enqueue_create_breakpoint_request_buffer(const HsailBreakpointRequest* request)
{
  /* Make a copy of the request structure to save in the buffer */
  int kernel_function_name_len = 0;
  int command_buffer_position = gs_hsail_breakpoint_request_buffer_len;

  gdb_assert(NULL != request);
  gdb_assert(NULL != gs_hsail_request_buffer);

  /* Do a deep copy of the HSAIL BP request passed to this function
   * since its member arrays will be deallocated before we flush the request to
   * the agent*/
  switch (request->type)
  {
    case HSAIL_BP_TYPE_KERNEL_FUNCTION:
      {
        /*Save request to cache*/
        hsail_breakpoint_copy_bp_request(&gs_hsail_request_buffer[command_buffer_position],
                                         request);
        /* This printf is helpful to debug the caching, leaving this around for now */
        /*
        printf("Enqueue Message %s \t %s\n",
               request->bp.kernel_func.func_name,
               gs_hsail_request_buffer[command_buffer_position].bp.kernel_func.func_name
               );

        printf("Strlen %d \t %d\n",
               strlen(request->bp.kernel_func.func_name),
               strlen(gs_hsail_request_buffer[command_buffer_position].bp.kernel_func.func_name)
               );
         */
        break;
      }
    case HSAIL_BP_TYPE_SOURCE_LOCATION:
    case HSAIL_BP_TYPE_ANY_LOCATION:
      {
        hsail_breakpoint_copy_bp_request(&gs_hsail_request_buffer[command_buffer_position],
                                         request);
        break;
      }
    default:
      gdb_assert(0);
      break;
  }

  gs_hsail_breakpoint_request_buffer_len = gs_hsail_breakpoint_request_buffer_len + 1;

}

void hsail_enqueue_delete_breakpoint_packet(int gdb_bkpt_num)
{
  HsailCommandPacket breakpoint_packet;
  gdb_assert(gdb_bkpt_num >= 0);

  hsail_fifo_initialize_packet(&breakpoint_packet);
  breakpoint_packet.m_command = HSAIL_COMMAND_DELETE_BREAKPOINT;
  breakpoint_packet.m_gdbBreakpointID = gdb_bkpt_num ;

  hsail_push_command(breakpoint_packet);
}

void hsail_enqueue_disable_breakpoint_packet(int gdb_bkpt_num)
{
    HsailCommandPacket disable_packet;
    gdb_assert(gdb_bkpt_num >= 0);

    hsail_fifo_initialize_packet(&disable_packet);

    disable_packet.m_command = HSAIL_COMMAND_DISABLE_BREAKPOINT;
    disable_packet.m_gdbBreakpointID = gdb_bkpt_num ;

    hsail_push_command(disable_packet);
}

/*
 * Create a momentary breakpoint packet and put it to the fifo.
 */
void hsail_enqueue_momentary_breakpoint_packet(const int num_momentary_bp)
{
  HsailCommandPacket breakpoint_packet;
  hsail_fifo_initialize_packet(&breakpoint_packet);

  breakpoint_packet.m_command = HSAIL_COMMAND_MOMENTARY_BREAKPOINT;
  breakpoint_packet.m_numMomentaryBP = num_momentary_bp;

  hsail_push_command(breakpoint_packet);
}

void hsail_enqueue_set_logging(const HsailLogCommand logging_command)
{
  HsailCommandPacket logging_packet;
  hsail_fifo_initialize_packet(&logging_packet);

  logging_packet.m_command = HSAIL_COMMAND_SET_LOGGING;
  logging_packet.m_loggingInfo = logging_command;

  hsail_push_command(logging_packet);
}
