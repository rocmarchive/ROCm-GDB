/*
   ROCm loaded segment functions

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
#include "annotate.h"
#include "ui-out.h"
#include "format.h"
#include "gdb_assert.h"

#include "CommunicationControl.h"

#include "rocm-dbginfo.h"
#include "rocm-segment-loader.h"
#include "rocm-tdep.h"
#include "rocm-utils.h"

void hsail_segment_print_loadmap(void);

static HsailSegmentDescriptor* gs_loaded_segments = NULL;
static size_t gs_num_loaded_segments = 0;
static size_t gs_executed_segment_index = SIZE_MAX;

/* Just a logging printer.
 * A quick easy feature maybe to add "info rocm shared"
 * */
void hsail_segment_print_loadmap(void)
{
  if (gs_loaded_segments != NULL)
    {
      size_t i=0;
      for (i=0; i < gs_num_loaded_segments; i++)
        {
          rocm_printf_filtered("Segment: %lu",i);
          printf_filtered(" device: %lu", gs_loaded_segments[i].device);
          printf_filtered(" executable: %lu",gs_loaded_segments[i].executable);
          printf_filtered(" codeObjectStorageType: %d",gs_loaded_segments[i].codeObjectStorageType);
          printf_filtered(" codeObjectStorageBase: %lx",gs_loaded_segments[i].codeObjectStorageBase);
          printf_filtered(" codeObjectStorageSize: %lu",gs_loaded_segments[i].codeObjectStorageSize);
          printf_filtered(" codeObjectStorageOffset: %lu",gs_loaded_segments[i].codeObjectStorageOffset);
          printf_filtered(" segmentBase: %lx",gs_loaded_segments[i].segmentBase);
          printf_filtered(" segmentSize: %lu",gs_loaded_segments[i].segmentSize);
          printf_filtered(" segmentBaseElfVA: %lu\n",gs_loaded_segments[i].segmentBaseElfVA);
          printf_filtered(" IsExecuted: %d\n",gs_loaded_segments[i].isSegmentExecuted);
        }
    }
}

static void hsail_segment_clear_loader_state(void)
{
  xfree(gs_loaded_segments);
  gs_loaded_segments = NULL;
  gs_num_loaded_segments = 0;
  gs_executed_segment_index = SIZE_MAX;
}

void hsail_segment_update_loadmap(void)
{
  if(hsail_is_debug_facilities_loaded())
    {
      bool is_executing_seg_found = false;
      void* segment_mem = NULL;
      size_t i=0;
      /* Clear state before we use shared memory and update the loadmap*/
      hsail_segment_clear_loader_state();

      segment_mem =  hsail_tdep_map_loadmap_buffer();

      gdb_assert(segment_mem != NULL);

      gs_num_loaded_segments = ((size_t*)segment_mem)[0];

      /* Clear existing and resize*/
      xfree(gs_loaded_segments);
      gs_loaded_segments = (HsailSegmentDescriptor*)xmalloc(sizeof(HsailSegmentDescriptor)*
                                                                  gs_num_loaded_segments);

      /* Loaded segments are stored after the size_t bytes */
      memcpy(gs_loaded_segments,
             (size_t*)segment_mem + 1,
             sizeof(HsailSegmentDescriptor)*gs_num_loaded_segments);

      for (i=0; i < gs_num_loaded_segments; i++)
        {
          if (gs_loaded_segments[i].isSegmentExecuted)
            {
              gs_executed_segment_index = i;
              is_executing_seg_found = true;
              break;
            }
        }

      /* If we do this when debug facilities is loaded only, that means
       * some AQL packet has been dispatched. This could be an assert*/
      if (is_executing_seg_found != true)
        {
          rocm_printf_filtered("No executing segment found");
        }

      /*hsail_segment_print_loadmap();*/
      hsail_tdep_unmap_shm_buffer(segment_mem);
    }
}


/* For now only clear any stale state */
void hsail_segment_initialize_loader(void)
{
  hsail_segment_clear_loader_state();
}

void hsail_segment_shutdown_loader(void)
{
  hsail_segment_clear_loader_state();
}


bool hsail_segment_resolve_elfva(const uint64_t elf_va, uint64_t* mem_addr_out)
{
  if (mem_addr_out == NULL)
    {
      return false;
    }
  if (gs_executed_segment_index != SIZE_MAX)
    {
      /* subtract elf va from segment base elf va */

      uint64_t diff = (elf_va - gs_loaded_segments[gs_executed_segment_index].segmentBaseElfVA);

      /* Get segment_base for  executed segment and add delta to it */
      mem_addr_out[0] = diff + (gs_loaded_segments[gs_executed_segment_index].segmentBase);
      /*rocm_printf_filtered("Diff: %ld New address is %lx ", diff, mem_addr_out[0]);*/

      return true;
    }

  return false;
}

bool hsail_segment_resolve_memva(const uint64_t mem_addr, uint64_t* elf_va_out )
{
  if (elf_va_out == NULL)
    {
      return false;
    }
  if (gs_executed_segment_index != SIZE_MAX)
    {
      /* subtract mem  va from segment base segemnt mem va */
      uint64_t diff = mem_addr -
                      gs_loaded_segments[gs_executed_segment_index].segmentBase ;

      /* Get segment_base for  executed segment and add delta to it */
      elf_va_out[0] = diff + gs_loaded_segments[gs_executed_segment_index].segmentBaseElfVA;

      /*rocm_printf_filtered("Diff: %ld New address is %lx ", diff, elf_va_out[0]);*/

      return true;
    }

  return false;
}
