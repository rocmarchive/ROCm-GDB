/*
   ROCm GDB functions print HSAIL variable and work item information

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


#if !defined (HSAIL_PRINT_H)
#define HSAIL_PRINT_H 1

#include <stdbool.h>
#include "CommunicationControl.h"

typedef enum
{
  HSAIL_PRINT_SUCCESS = 0,
  HSAIL_PRINT_WRONG_FORMAT,           /* print command is not in the print rocm:arg format */
  HSAIL_PRINT_VAR_NOT_FOUND,          /* var requested not found */
  HSAIL_PRINT_NO_WAVES,               /* no waves were found probably breakpoint at start of kernel */
  HSAIL_PRINT_UNKNOWN                 /* An unknown error code */
} HsailPrintStatus;

bool hsail_parse_print_request(const char* expr, const char** varname);

struct value* hsail_print_expression(const char *exp, char* printFormat, size_t* format_size);

HsailPrintStatus hsail_print_get_last_error(void);

void hsail_print_cleanup(void* pData);

int hsail_print_is_hsail_expression(const char *arg);

void hsail_print_wave_info (struct ui_out *uiout, int from_tty);

void hsail_print_workgroups_info (HsailWaveDim3 active_work_group, struct ui_out *uiout, int from_tty);

void hsail_print_specific_workgroup_by_id_info (int index, struct ui_out *uiout, int from_tty);

void hsail_print_specific_workgroup_info (unsigned int* workgroupid, struct ui_out *uiout, int from_tty);

void hsail_print_workitem_info (HsailWaveDim3 active_work_group, HsailWaveDim3 active_work_item, bool mark_active_item, struct ui_out *uiout, int from_tty);

bool hsail_print_gpu_disassembly(const char* arg);

#endif
