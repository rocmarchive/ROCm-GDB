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


#if !defined (_HSAILFIFO_CONTROL_H)
#define _HSAILFIFO_CONTROL_H 1

#include "rocm-breakpoint.h"

/* The agent header file */
#include "CommunicationControl.h"

void hsail_initialize_command_buffer(void);

void hsail_free_command_buffer(void);

void hsail_flush_breakpoint_command_buffer(void);

void hsail_enqueue_continue_dispatch_packet(void);

void hsail_enqueue_create_breakpoint_request_buffer(const HsailBreakpointRequest* request);

void hsail_enqueue_create_breakpoint_packet(const int pc,
                                            const int gdb_bkpt_num,
                                            const char* src_line,
                                            const int line_num,
                                            const HsailBreakpointCondition* condition);

void hsail_enqueue_create_kernel_name_breakpoint_packet(const char* kernel_name,
                                                        const int gdb_bkpt_num);

void hsail_enqueue_delete_breakpoint_packet(int gdbBkptNum);

void hsail_enqueue_delete_breakpoint_request_buffer(const int gdb_id_number);

void hsail_enqueue_disable_breakpoint_packet(int gdbBktptNum);

void hsail_enqueue_momentary_breakpoint_packet(const int num_bp);

void hsail_enqueue_set_logging(const HsailLogCommand loggingConfig);

#endif // _HSAILFIFO_CONTROL_H
