/*
   ROCm GDB step inferior command implementation

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


/* GDB headers */
#include "defs.h"
#include "ui-out.h"
#include "gdb_assert.h"

/* Added for memset */
#include <string.h>

/* HSAIL-GDB headers*/
#include "hsail-breakpoint.h"
#include "hsail-fifo-control.h"
#include "hsail-infcmd.h"
#include "hsail-tdep.h"
#include "CommunicationControl.h"

/* HwDbgFacilities: */
#include "FacilitiesInterface.h"

/* This macro should never be defined publicly */

#ifdef DISABLE_HSAIL_STEP

/* Disable stepping by always returning false, the caller of this function
 * will always step only the CPU, the GPU will make forward progress when you
 * continue the dispatch by  "c"
 * */
bool is_hsail_step(void)
{
  printf("!!!!!!!!!! An Internal macro has disabled GPU stepping !!!!!!!!!\n ");
  return false;
}

#else

bool is_hsail_step(void)
{
  /* true if the focus thread is the debug thread */
  bool is_hsail_thread_focus_thread = hsail_infcmd_is_focus_thread_dispach_thread();

  /* currently only consider if hsail device is focused.
     TODO: check if the current frame is hsail code, etc.
  */


  /* We handle each known case of is_hsail_step individually by && all
   * required checks together to make it more cleaner
   * */

  /* GPU Stepping: Case#1
     Check if we are focused on the debug thread, we can step into
     GPU execution from here.
     Single step on the CPU happens when the debug thread is not the focus
  */
  if (  hsail_is_focus_device() &&
        is_hsail_thread_focus_thread &&
        (hsail_is_debug_facilities_loaded() == HSAIL_AGENT_BINARY_AVAILABLE)
      )
    {
      return true;
    }
  /* GPU Stepping: Case#2
     Check if we are "in the predispatch" callback state, we can step into
     GPU execution from here */
  else if (  hsail_is_focus_device() &&
             hsail_is_focus_predispatch() &&
             (hsail_is_debug_facilities_loaded() == HSAIL_AGENT_BINARY_AVAILABLE)
           )
    {
      return true;
    }

  return false;
}
#endif

static void hsail_step_write_momentary_breakpoints(HwDbgInfo_debug dbg,
                                                   size_t          step_addr_count,
                                                   HwDbgInfo_addr* step_addrs)
{
  int i = 0;
  HwDbgInfo_code_location loc = NULL;
  HwDbgInfo_linenum line_num = 0;

  HsailMomentaryBP* momentary_bp = NULL;

  int momentary_bp_data_size = 0;
  int momentary_bp_shmem_size = 0;

  gdb_assert(dbg != NULL);
  gdb_assert(step_addrs != NULL);
  gdb_assert(step_addr_count > 0);
  momentary_bp = NULL;

  /* Size checking for momentary breakpoint buffers */
  momentary_bp_data_size = sizeof(HsailMomentaryBP)*step_addr_count;
  momentary_bp_shmem_size = hsail_get_momentary_bp_buffer_shmem_max_size();
  if (momentary_bp_data_size >= momentary_bp_shmem_size)
    {
      printf("Momentary breakpoint buffer overflow\n");
      printf("No of Step Addresses: %lu\n", step_addr_count);
      printf("Step Addresses Memory Required: %d\n", momentary_bp_data_size);
    }

  gdb_assert(momentary_bp_data_size < momentary_bp_shmem_size);

  momentary_bp = (HsailMomentaryBP*)hsail_tdep_map_momentary_bp_buffer();
  gdb_assert(momentary_bp != NULL);

  memset(momentary_bp, 0, momentary_bp_shmem_size);

  for(i=0; i < step_addr_count; i++)
    {
      loc = NULL;
      line_num = 0;

      hwdbginfo_addr_to_line(dbg, step_addrs[i], &loc);
      hwdbginfo_code_location_details(loc, &line_num, 0, NULL, NULL);

      momentary_bp[i].m_pc      = step_addrs[i];
      momentary_bp[i].m_lineNum = line_num;
    }

  hsail_tdep_unmap_shm_buffer((void*)momentary_bp);
}

void hsail_set_step_breakpoints(int step_type, int count)
{
  struct ui_out* uiout = current_uiout;
  HwDbgInfo_debug dbg = hsail_init_hwdbginfo(NULL);
  HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;

  uint64_t addr = hsail_tdep_get_current_pc();

  HwDbgInfo_addr* step_addrs = NULL;
  size_t step_addr_count = 0;

  /* At the initial breakpoint, we are emulating the behavior of having the PC at the
   * opening brace. Thus, a "step over" should behave like a "step in".
   * */

  if ((0 == addr) && (HSAIL_STEP_OVER == step_type))
    step_type = HSAIL_STEP_IN;

  gdb_assert(NULL != dbg);
  gdb_assert((HSAIL_STEP_IN == step_type) || 0 != addr);

  /* Allow "step in" even if we don't have an address yet (i.e. in the pre-dispatch) */
  if (NULL == dbg || ((HSAIL_STEP_IN != step_type) && (0 == addr)))
    {
      ui_out_text(uiout, "[ROCm-gdb]: could not perform GPU step\nContinuing execution...\n");
      return;
    }

  /* TODO: support step "n" command */
  if (1 != count)
    {
      ui_out_text(uiout, "[ROCm-gdb]: Only single steps are currently supported in hsail\nConverting to single step\n");
      count = 1;
    }

  if (HSAIL_STEP_IN == step_type)
    {
      err = hwdbginfo_all_mapped_addrs(dbg, 0, NULL, &step_addr_count);
      gdb_assert(HWDBGINFO_E_SUCCESS == err);
      gdb_assert(0 != step_addr_count);
      step_addrs = step_addr_count > 0 ? (HwDbgInfo_addr*)malloc(step_addr_count * sizeof(HwDbgInfo_addr)) : NULL;
      gdb_assert(NULL != step_addrs);

      if ((HWDBGINFO_E_SUCCESS != err) || 0 == step_addr_count || NULL == step_addrs)
        {
          ui_out_text(uiout, "[ROCm-gdb]: Could not perform GPU step-in\nContinuing execution...\n");
          return;
        }

      memset(step_addrs, 0, step_addr_count * sizeof(HwDbgInfo_addr));

      err = hwdbginfo_all_mapped_addrs(dbg, step_addr_count, step_addrs, NULL);
      gdb_assert(HWDBGINFO_E_SUCCESS == err);
      if (HWDBGINFO_E_SUCCESS != err)
        {
          ui_out_text(uiout, "[ROCm-gdb]: Could not perform GPU step-in\nContinuing execution...\n");
          free(step_addrs);
          return;
        }
    }
  else
    {
      /* Get the number of step addresses */
      err = hwdbginfo_step_addresses(dbg, addr, (HSAIL_STEP_OUT == step_type), 0, NULL, &step_addr_count);
      gdb_assert(HWDBGINFO_E_SUCCESS == err);
      gdb_assert(0 != step_addr_count);
      step_addrs = step_addr_count > 0 ? (HwDbgInfo_addr*)malloc(step_addr_count * sizeof(HwDbgInfo_addr)) : NULL;
      gdb_assert(NULL != step_addrs);

      if ((HWDBGINFO_E_SUCCESS != err) || 0 == step_addr_count || NULL == step_addrs)
        {
          ui_out_text(uiout, "[ROCm-GDB]: Could not perform GPU step-over\nContinuing execution...\n");
          return;
        }

      memset(step_addrs, 0, step_addr_count * sizeof(HwDbgInfo_addr));

      /* Get the step addresses */
      err = hwdbginfo_step_addresses(dbg, addr, (HSAIL_STEP_OUT == step_type), step_addr_count, step_addrs, NULL);
      gdb_assert(HWDBGINFO_E_SUCCESS == err);
      if (HWDBGINFO_E_SUCCESS != err)
        {
          ui_out_text(uiout, "[ROCm-gdb]: Could not perform GPU step-over\nContinuing execution...\n");
          free(step_addrs);
          return;
        }
    }

  /* Write momentary breakpoints to shared memory */
  hsail_step_write_momentary_breakpoints(dbg, step_addr_count,step_addrs);

  /* Notify the agent */
  hsail_enqueue_momentary_breakpoint_packet(step_addr_count);

  /* Prepare the agent to have the "continue" issued: */
  hsail_set_continue_dispatch();
}

/* This command is needed to be sure that we are actually starting the inferior
 * We need it so that we know when to call continuedebugging in the DBE
 *
 * When we had continuedebugging in the command loop, the problem was that whenever
 * we evaluated an expression, there was a pretty good chance that the event loop
 * thread would call "ContinueDebugging" even though it should not happen since we are
 * still evaluating expressions
 * */
void hsail_set_continue_dispatch(void)
{
  gdb_assert(is_hsail_linux_initialized());
  gdb_assert(hsail_is_focus_device());

  hsail_enqueue_continue_dispatch_packet();

}
