/*
   ROCm GDB functions to print state of the HSAIL dispatches

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

#if !defined (HSAIL_KERNEL_H)
#define HSAIL_KERNEL_H 1

#include "CommunicationControl.h"

struct hsail_dispatch
{
  struct hsail_dispatch *next;

  /* The number of times the kernel has been dispatched on this queue */
  int dispatch_count;

  /* A queue idenitifier*/
  int hsa_queue_id;

  /* The size of each workgroup */
  HsailWaveDim3 work_groups_size;

  /* The total number of workitems - ie the grid size */
  HsailWaveDim3 work_items;
} ;


/* Note: this struct has been defined in the gdb 'C' style (just like the breakpoint struct)
 * so that we can use the next member object for the linked list rather than the "typedef struct _name" style.
 * When we use the latter style, the C compiler says that hsail_kernel has not been defined.
 * The only drawback is we need "struct" more
 */
struct hsail_kernel
{
  struct hsail_kernel *next;

  /* Kernel name */
  char* kernel_name;

  /* Kernel file name */
  char* kernel_source_file_name;

  /* A list of all possible dispatches*/
  struct hsail_dispatch* dispatch_list;

  /* active dispatch */
  struct hsail_dispatch* active_dispatch;
};


void hsail_kernel_clear_chain(void);

bool hsail_kernel_add_dispatch(const HsailNotificationPayload* fifo_payload);

void hsail_kernel_print_info (struct ui_out *uiout, int from_tty);

void hsail_kernel_print_specific_info(char *arg, struct ui_out *uiout, int from_tty);

struct hsail_dispatch* hsail_kernel_active_dispatch(void);


#endif // HSAIL_KERNEL_H
