/*
   ROCm GDB functions to manage debug information using the debug facilities
   library.

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

#if !defined (HSAIL_DBGINFO_H)
#define HSAIL_DBGINFO_H 1

#include "CommunicationControl.h"
#include "FacilitiesInterface.h"

typedef enum _AgentBinaryNotification
{
    HSAIL_AGENT_BINARY_UNKNOWN,
    HSAIL_AGENT_BINARY_AVAILABLE

} AgentBinaryNotification;

char* hsail_dbginfo_get_srcline_from_buffer(const HwDbgInfo_debug dbg,
                                            const HwDbgInfo_linenum line_num);

/* Note: The input pc has to be in the elf va form. The segment loader API should
 * resolve this before calling this function */
bool hsail_dbginfo_get_pc_info(HwDbgInfo_addr pc,
                               HwDbgInfo_linenum* op_line_num,  char** op_file_name);

char* hsail_dbginfo_get_source_buffer(void);

const char* hsail_dbginfo_get_active_file_name(void);

void hsail_dbginfo_set_facilities_status(const AgentBinaryNotification);

/* A test function to only see if we can print a file name*/
int hsail_dbginfo_test_all_mapped_addrs(HwDbgInfo_debug dbg);

AgentBinaryNotification hsail_is_debug_facilities_loaded(void);

HwDbgInfo_debug hsail_init_hwdbginfo(HsailNotificationPayload* payload);

void hsail_free_hwdbginfo(void);

#endif
