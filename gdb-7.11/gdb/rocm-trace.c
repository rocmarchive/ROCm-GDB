/*
   ROCm GDB functions to trace kernel dispatches

   Copyright (c) 2016 ADVANCED MICRO DEVICES, INC.  All rights reserved.
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

#include "defs.h"
#include "format.h"
#include "gdb_assert.h"
#include "ui-out.h"
#include "utils.h"

#include <string.h>
#include <stdbool.h>
#include "CommunicationControl.h"

#include "rocm-breakpoint.h"
#include "rocm-kernel.h"
#include "rocm-trace.h"
#include "rocm-utils.h"

static const char delim[] =",";

struct hsail_trace_config
{
  bool is_kernel_tracing_enabled ;
  FILE* trace_file_handle ;
  char* trace_file_name ;
};

static struct hsail_trace_config config = {false, NULL, NULL};

static void hsail_trace_open_file(void)
{
  if (config.trace_file_handle == NULL)
    {
      if(config.trace_file_name != NULL)
        {
          config.trace_file_handle  = fopen(config.trace_file_name, "ab+");
          if (config.trace_file_handle == NULL)
            {
              rocm_printf_filtered("Unable to open file \"%s\", please verify the path is valid\n",
                                   config.trace_file_name);
            }
          else
            {
              rocm_printf_filtered("GPU kernel launch trace will be saved to \"%s\"\n", config.trace_file_name);
            }
        }
      else
        {
          rocm_printf_filtered("Invalid file name\n");
          config.is_kernel_tracing_enabled = false;
        }
    }
}

static void hsail_trace_close_file(void)
{
  if (config.trace_file_handle != NULL && config.is_kernel_tracing_enabled)
    {
      fprintf(config.trace_file_handle, "#End GPU kernel launch trace %s", hsail_utils_get_timestamp());
      fclose(config.trace_file_handle);
      config.trace_file_handle = NULL;
    }
}

static void hsail_trace_write_header(void)
{
  if (config.trace_file_handle != NULL && config.is_kernel_tracing_enabled)
    {
      fprintf(config.trace_file_handle, "#Start GPU kernel launch trace %s", hsail_utils_get_timestamp());
      fprintf(config.trace_file_handle, "index%s",  delim);
      /*fprintf(config.trace_file_handle, "timestamp%s", delim);*/
      fprintf(config.trace_file_handle, "queue_id%s", delim);
      fprintf(config.trace_file_handle, "packet_id%s", delim);
      fprintf(config.trace_file_handle, "kernel_name%s", delim);
      fprintf(config.trace_file_handle, "header%s", delim);
      fprintf(config.trace_file_handle, "setup%s", delim);
      fprintf(config.trace_file_handle, "workgroup_size%s", delim);
      fprintf(config.trace_file_handle, "reserved0%s", delim);
      fprintf(config.trace_file_handle, "grid_size%s", delim);
      fprintf(config.trace_file_handle, "private_segment_size%s", delim);
      fprintf(config.trace_file_handle, "group_segment_size%s", delim);
      fprintf(config.trace_file_handle, "kernel_object%s", delim);
      fprintf(config.trace_file_handle, "kernarg_address%s", delim);
      fprintf(config.trace_file_handle, "reserved2%s", delim);
      fprintf(config.trace_file_handle, "completion_signal");
      fprintf(config.trace_file_handle, "\n");

      fflush(config.trace_file_handle);
    }
}

/* If the set random is true, choose a random file name.
 * If some filename already exists, rename to the new input.
 * Else create a new file with input name
 * */
static void hsail_trace_set_file_name(const char* ip_option, const bool set_random)
{
  hsail_trace_close_file();

  if(set_random)
    {
      int len = 128;
      int random_no =0;

      config.trace_file_name = xmalloc(sizeof(char)*len);
      memset(config.trace_file_name, '\0', len);
      gdb_assert( config.trace_file_name != NULL);

      srand(time(NULL));
      random_no = rand()%1000;

      sprintf(config.trace_file_name, "kernel_trace_%d.csv", random_no );
    }
  else if (ip_option != NULL)
    {
      char* sanitized_ip_option = NULL;
      hsail_utils_sanitize_file_name(&sanitized_ip_option, ip_option);

      if (sanitized_ip_option != NULL)
        {
          /* if some file was already created rename it */
          if (config.trace_file_name != NULL)
            {
              /* if the rename fails, we just use the sanitized input */
              if (hsail_utils_rename_file(config.trace_file_name, sanitized_ip_option))
                {
                    rocm_printf_filtered("Rename the kernel launch file \"%s\" to \"%s\"\n",
                                         config.trace_file_name,
                                         sanitized_ip_option );
                }

              /* delete old name */
              xfree(config.trace_file_name);
            }

          config.trace_file_name = NULL;
          /* No file name was previously set, we just save the sanitized user input */
          hsail_utils_copy_string(&(config.trace_file_name), sanitized_ip_option);

          xfree(sanitized_ip_option);
        }
      else
        {
          /* if we couldn't sanitize, just copy the input as we know it's not NULL */
          hsail_utils_copy_string(&(config.trace_file_name), ip_option);
        }

    }

  hsail_trace_open_file();
}



static bool hsail_trace_is_trace_initialized(void)
{
  if (config.trace_file_handle == NULL)
    {
      return false;
    }
  else if (config.is_kernel_tracing_enabled == true)
    {
      return true;
    }
  else
    {
      return false;
    }
}

void hsail_trace_add_dispatch(const HsailNotificationPayload* fifo_data)
{
  static unsigned int row_index = 0;
  HsailWaveDim3 grid_size;
  HsailWaveDim3 wg_size;
  const char* kernel_name;

  const HsailDispatchPacket* packet = &(fifo_data->payload.BinaryNotification.m_packet);

  if (!hsail_trace_is_trace_initialized())
    {
      return;
    }
  gdb_assert(fifo_data != NULL);
  gdb_assert(fifo_data->m_Notification == HSAIL_NOTIFY_NEW_BINARY);

  /* Local copies for readability */
  kernel_name = fifo_data->payload.BinaryNotification.m_KernelName;
  hsail_utils_copy_wavedim3(&grid_size, &(fifo_data->payload.BinaryNotification.m_packet.grid_size));
  hsail_utils_copy_wavedim3(&wg_size, &(fifo_data->payload.BinaryNotification.m_packet.workgroup_size));

  fprintf(config.trace_file_handle, "%u%s",             row_index, delim);
  /*fprintf(config.trace_file_handle, "%u%s",             (unsigned)time(NULL), delim);*/
  fprintf(config.trace_file_handle, "%lu%s",            packet->queue_id,delim);
  fprintf(config.trace_file_handle, "%lu%s",            packet->packet_id,delim);
  fprintf(config.trace_file_handle, "%s%s",             kernel_name,delim);
  fprintf(config.trace_file_handle, "%u%s",             packet->header,delim);
  fprintf(config.trace_file_handle, "%u%s",             packet->setup,delim);
  fprintf(config.trace_file_handle, "{%u %u %u}%s",     wg_size.x, wg_size.y, wg_size.z,delim);
  fprintf(config.trace_file_handle, "%u%s",             packet->reserved0,delim);
  fprintf(config.trace_file_handle, "{%u %u %u}%s",     grid_size.x, grid_size.y, grid_size.z,delim);
  fprintf(config.trace_file_handle, "%u%s",             packet->private_segment_size,delim);
  fprintf(config.trace_file_handle, "%u%s",             packet->group_segment_size,delim);
  fprintf(config.trace_file_handle, "%lu%s",            packet->kernel_object,delim);
  fprintf(config.trace_file_handle, "%p%s",             packet->kernarg_address,delim);
  fprintf(config.trace_file_handle, "%lu%s",            packet->reserved2,delim);
  fprintf(config.trace_file_handle, "%lu",              packet->completion_signal_handle);
  fprintf(config.trace_file_handle, "\n");

  fflush(config.trace_file_handle);

  row_index ++;

}

void hsail_trace_print_configuration(void)
{
  if(config.is_kernel_tracing_enabled)
    {
      printf_filtered("rocm trace: \t on \t Kernel tracing is enabled\n");
      printf_filtered("\t\t\t Trace is saved to %s\n", config.trace_file_name );
    }
  else
    {
      printf_filtered("rocm trace: \t off \n");
    }
}

void hsail_trace_configure(const char* ip_option)
{
  /* If input is
   * on         then enable tracing
   * off        then disable tracing
   * "anystr"   use the input as the trace filename
   * */
  if (ip_option == NULL)
    {
      printf_filtered("set rocm trace [on|off] \t   Enable/Disable tracing of GPU dispatches\n");
      printf_filtered("set rocm trace <filename> \t   Save GPU dispatch trace to <filename>\n");

      return;
    }
  if(strcmp(ip_option,"on") == 0)
    {
      config.is_kernel_tracing_enabled = true;
      hsail_trace_initialize();

      rocm_printf_filtered("GPU Kernel launch tracing has been enabled\n");

    }
  else if(strcmp(ip_option,"off") == 0)
    {
      hsail_trace_stop();
      config.is_kernel_tracing_enabled = false;
    }
  else
    {
      hsail_trace_set_file_name(ip_option, false);
      hsail_trace_write_header();
    }

}

/* Start tracing, open the trace file */
void hsail_trace_initialize(void)
{
  if (!config.is_kernel_tracing_enabled)
    {
      return;
    }

  /* if already open, get out of here */
  if (config.trace_file_handle != NULL)
    {
      return;
    }

  /* If we dont know a filename, use the random */
  if (config.trace_file_name == NULL)
    {
      hsail_trace_set_file_name(NULL, true);
      hsail_trace_write_header();
    }
  else
    {
      /* if we do know the filename, just open the file*/
      hsail_trace_open_file();
    }

}

/* Close the trace file */
void hsail_trace_stop(void)
{
  if (!config.is_kernel_tracing_enabled)
    {
      return;
    }
  hsail_trace_close_file();

  if (config.trace_file_name != NULL)
    {
      xfree(config.trace_file_name);
      config.trace_file_name = NULL;
    }
}
