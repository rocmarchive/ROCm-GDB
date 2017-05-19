/*
   ROCm GDB commands for GNU debugger GDB.

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GDB headers */
#include "defs.h"
#include "gdbthread.h"
#include "gdbtypes.h"
#include "cli/cli-cmds.h"
#include "command.h"
#include "expression.h"
#include "gdb_assert.h"
#include "value.h"

/* rocm-gdb headers */
#include "rocm-cmd.h"
#include "rocm-help.h"
#include "rocm-thread.h"
#include "rocm-tdep.h"
#include "rocm-utils.h"

/* The header files shared with the agent*/
#include "CommunicationControl.h"

/* active work group and work item */
static const HsailWaveDim3 gs_unknown_wave_dim = {-1,-1,-1};
static HsailWaveDim3 gs_active_work_group = {-1,-1,-1};
static HsailWaveDim3 gs_active_work_item = {-1,-1,-1};

/* The ID of the thread that the present dispatch was started from.
   This is usually the main thread in a single threaded app that does dispatches
 */
static long gs_active_host_dispatch_thread_id = 0;

static bool hsail_thread_validate_thread_active(const unsigned int* workGroup, const unsigned int* workItem)
{
  /* get the waves info */
  int num_waves = hsail_tdep_get_active_wave_count();
  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();
  int nWave = 0;
  /* a single bit rotated left each time to compare whether each workitem in  a wave is active */
  uint64_t current_bit = 0;
  bool found = false;

  /* A NULL is possible if no dispatch is active*/
  if (NULL == wave_info_buffer)
    {
      return found;
    }

  gdb_assert(NULL != workGroup);
  gdb_assert(NULL != workItem);


  for (nWave = 0 ; nWave < num_waves ; nWave++)
    {
      /* check the work-group first */
      if (workGroup[0] == wave_info_buffer[nWave].workGroupId.x &&
          workGroup[1] == wave_info_buffer[nWave].workGroupId.y &&
          workGroup[2] == wave_info_buffer[nWave].workGroupId.z)
        {
          int wi_index = 0;
          /* check all exec flags for the work item */
          current_bit = 1;
          for (wi_index = 0 ; wi_index < 64 ; wi_index++)
            {
              /* if the work item's exec bit in the wave's execMask is true */
              if (wave_info_buffer[nWave].execMask & current_bit)
                {
                  if (workItem[0] == wave_info_buffer[nWave].workItemId[wi_index].x &&
                      workItem[1] == wave_info_buffer[nWave].workItemId[wi_index].y &&
                      workItem[2] == wave_info_buffer[nWave].workItemId[wi_index].z)
                    {
                      found = true;
                    }
                  current_bit = current_bit<<1;
                }
            }
        }
    }

  /* release the wave buffer */
  hsail_tdep_unmap_shm_buffer((void*)wave_info_buffer);

  return found;
}

static void hsail_thread_print_focus_change(void)
{
  struct ui_out *uiout = current_uiout;
  char focus_msg[256] = "";
  fflush(stdout);
  sprintf(focus_msg,
          "[ROCm-gdb]: Switching to work-group (%d,%d,%d) and work-item (%d,%d,%d)\n",
          gs_active_work_group.x ,gs_active_work_group.y, gs_active_work_group.z,
          gs_active_work_item.x,gs_active_work_item.y, gs_active_work_item.z);

  if (ui_out_is_mi_like_p(uiout))
    {
      printf_filtered("%s", focus_msg);
    }
  else
    {
      ui_out_text(uiout, focus_msg);
    }

  fflush(stdout);
}

/* Clear the focus wave and work item at the end of the dispatch */
void hsail_thread_clear_focus(void)
{
  hsail_utils_copy_wavedim3(&gs_active_work_group, &gs_unknown_wave_dim);
  hsail_utils_copy_wavedim3(&gs_active_work_item, &gs_unknown_wave_dim);
}

void hsail_thread_set_focus(const HsailWaveDim3 focusWg, const HsailWaveDim3 focusWi)
{
  /* To work with the available validate function, we need to
   * move the input parameters into an array. We don't want to
   * do C-casting which may cause silent failure (such as
   * HsailWaveDim3 internal data type change to long int).*/
  unsigned int wg_buff[3] = {-1, -1, -1};
  unsigned int wi_buff[3] = {-1, -1, -1};

  struct ui_out *uiout = current_uiout;

  wg_buff[0] = focusWg.x;
  wg_buff[1] = focusWg.y;
  wg_buff[2] = focusWg.z;

  wi_buff[0] = focusWi.x;
  wi_buff[1] = focusWi.y;
  wi_buff[2] = focusWi.z;

  /* If unknown, we cant validate it yet
   * */
  if (hsail_utils_compare_wavedim3(&gs_active_work_group, &gs_unknown_wave_dim) ||
      hsail_utils_compare_wavedim3(&gs_active_work_item, &gs_unknown_wave_dim))
    {
      hsail_utils_copy_wavedim3(&gs_active_work_group, &focusWg);
      hsail_utils_copy_wavedim3(&gs_active_work_item, &focusWi);

      hsail_thread_print_focus_change();
    }
  else if (hsail_thread_validate_thread_active(wg_buff, wi_buff))
    {
      /* print only if something changed */
      if (!(hsail_utils_compare_wavedim3(&focusWg, &gs_active_work_group) &&
          hsail_utils_compare_wavedim3(&focusWi, &gs_active_work_item)) )
        {
          hsail_utils_copy_wavedim3(&gs_active_work_group, &focusWg);
          hsail_utils_copy_wavedim3(&gs_active_work_item, &focusWi);

          hsail_thread_print_focus_change();
        }
    }

  /* We cant print an error message in the else case since in the initial case when the
   * dispatch starts and sends the focus info, the waveinfo buffer is not yet populated.
   *
   * We could special case the start dispatch case, but that would just be weird.
   * */
}



void hsail_thread_set_focus_command(char *arg, int from_tty)
{
  /* arg refers to the text after "rocm thread" */
  struct ui_out *uiout = current_uiout;
  unsigned int workGroup[3] = {-1,-1,-1};
  unsigned int workItem[3] = {-1,-1,-1};
  int l = 0;
  int dummynumItems = 0;
  bool foundParam = true;
  if (arg == NULL)
  {
    ui_out_text(uiout,"Unsupported parameters\n");
    ui_out_text(uiout,HSAIL_THREAD_HELP());
    return ;
  }

  /* Get the size: */
  l = strlen(arg);

  /* Verify some reasonable length. We would need atleast 8 characters
   * to have some chance of success in subsequent calls to get_argument.
   * For eg: "wg:0,0,0" makes 8 characters
   * */
  if (l < 8)
   {
      foundParam = false;
   }

  /* check for the wg section */
  if (foundParam)
  {
    foundParam = hsail_command_get_argument(arg, "wg", workGroup, &dummynumItems);
  }

  if (foundParam)
  {
    foundParam = hsail_command_get_argument(arg, "wi", workItem, &dummynumItems);
  }
  /*
   *  printf("IP: work-group (%d,%d,%d) and work-item (%d,%d,%d)]\n",
   *  workGroup[0],workGroup[1],workGroup[2],
   *  workItem[0],workItem[1],workItem[2]);
   */

  if (!foundParam)
  {
    ui_out_text(uiout,"Could not parse \"rocm thread\"\n");
    ui_out_text(uiout,HSAIL_THREAD_HELP());
  }
  else
  {
    /* validate that the wg and wi are active */
    if (hsail_thread_validate_thread_active(workGroup, workItem))
    {
      char funcExp[256] = "";
      struct expression *expr = NULL;
      HsailWaveDim3 wg_input;
      HsailWaveDim3 wi_input;
      hsail_utils_copy_wavedim3(&wg_input, &gs_unknown_wave_dim);
      hsail_utils_copy_wavedim3(&wi_input, &gs_unknown_wave_dim);

      sprintf(funcExp,
              "SetHsailThreadCmdInfo(%d,%d,%d,%d,%d,%d)",
              workGroup[0],workGroup[1],workGroup[2],workItem[0],workItem[1],workItem[2]);

      /* Create the expression */
      expr = parse_expression (funcExp);

      /* Call the evaluator */
      evaluate_expression (expr);

      wg_input.x = workGroup[0];
      wg_input.y = workGroup[1];
      wg_input.z = workGroup[2];
      wi_input.x = workItem[0];
      wi_input.y = workItem[1];
      wi_input.z = workItem[2];
      hsail_thread_set_focus(wg_input, wi_input);

      /* We force the focus to be "set"  as shown above even though
       * it is not necessary. This atleast gaurantees that the output
       * message for the user is printed instantly.
       *
       * We do it since the acknowledgment of the change which is done in the
       * handle_hsail_event() may come at a later time once the expression is
       * evaluated.
       * */
    }
    else
    {
      ui_out_text(uiout,"work-group and work-item provided not active\n");
    }
  }
}

void hsail_thread_get_current_focus(HsailWaveDim3* focus_wg, HsailWaveDim3* focus_wi)
{
  if (focus_wg != NULL)
    {
      hsail_utils_copy_wavedim3(focus_wg, &gs_active_work_group);
    }
  if (focus_wi != NULL)
    {
      hsail_utils_copy_wavedim3(focus_wi, &gs_active_work_item);
    }
}

void hsail_thread_set_dispatch_host_thread_pid(int dispatch_host_thread_id)
{
  gs_active_host_dispatch_thread_id = (long)dispatch_host_thread_id;
}

void hsail_thread_switch_rocm_context(char *arg, int from_tty)
{
  /* We can do this only if we know what host thread the
   * dispatch is starting from*/
  if (gs_active_host_dispatch_thread_id != 0)
    {
      size_t len = 1024;
      struct thread_info* host_thread_handle = NULL;

      char* dispatch_thread_pid_str = (char*)xmalloc(len*sizeof(char));
      gdb_assert(dispatch_thread_pid_str != NULL);
      memset(dispatch_thread_pid_str, '\0', len);

      sprintf(dispatch_thread_pid_str, "%ld",gs_active_host_dispatch_thread_id);

      host_thread_handle =  thread_find(dispatch_thread_pid_str, from_tty);

      if(host_thread_handle != NULL)
        {
          char* dispatch_thread_inf_num_str = (char*)xmalloc(len*sizeof(char));
          gdb_assert(dispatch_thread_inf_num_str != NULL);

          /* We use the inf->num to get the id of the thread
           * We Get the number for the thread as a string, and then switch focus.
           *
           * This is similar to doing "thread 1" */
          memset(dispatch_thread_inf_num_str, '\0', len);
          sprintf(dispatch_thread_inf_num_str, "%d",  host_thread_handle->inf->num);

          thread_command(dispatch_thread_inf_num_str, from_tty);

          xfree(dispatch_thread_inf_num_str);
        }

      xfree(dispatch_thread_pid_str);
    }
}
