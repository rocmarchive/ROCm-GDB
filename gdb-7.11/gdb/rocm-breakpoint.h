/*
   ROCm GDB breakpoint functions and interfacing with HSAIL debug facilities

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

#if !defined (HSAIL_BREAKPOINT_H)
#define HSAIL_BREAKPOINT_H 1

/* This header is needed for FacilitiesInterface */
#include <stdbool.h>

#include "CommunicationControl.h"
#include "FacilitiesInterface.h"

typedef struct _HsailNotificationPayload HsailNotificationPayload;

/* Information used for parsing and setting breakpoints */
typedef enum _HsailBreakpointType
{
    HSAIL_BP_TYPE_UNKNOWN,

    HSAIL_BP_TYPE_KERNEL_FUNCTION,
    HSAIL_BP_TYPE_SOURCE_LOCATION,
    HSAIL_BP_TYPE_ANY_LOCATION
} HsailBreakpointType;

struct breakpoint;

typedef struct _HsailBreakpointCondition
{
  char* condition_string;
  HsailConditionCode condition_code;
  HsailWaveDim3 work_group_id;
  HsailWaveDim3 work_item_id;

}HsailBreakpointCondition;

typedef struct _HsailBreakpointRequest
{
    HsailBreakpointType type;
    struct breakpoint* gdb_bkpt;
    /* The GDB breakpoint number ID */
    int number;
    HsailBreakpointCondition condition;
    union
    {
        /* HSAIL_BP_TYPE_KERNEL_FUNCTION */
        struct
        {
            char* func_name;
        } kernel_func;

        /* HSAIL_BP_TYPE_SOURCE_LOCATION */
        struct
        {
            char* file_name;
            HwDbgInfo_linenum line_num;
            char* src_line;
        } source_location;

        /* HSAIL_BP_TYPE_ANY_LOCATION */
        /* No additional data */
    } bp;
} HsailBreakpointRequest;

/* Breakpoint requests: */
void hsail_breakpoint_clear_bp_request(HsailBreakpointRequest* bp_request);

bool hsail_breakpoint_parse_bp_request(const char* hsail_bp_str, const char* extra_str, HsailBreakpointRequest* bp_request);

/* Adjust the breakpoint's memory address based on the presently loaded segment */
void hsail_breakpoint_adjust_location(struct breakpoint* p_bp);

/* Parse the hsail breakpoint command string and check if the "rocm:" identifier is present */
int is_hsail_breakpoint(const char *arg);

int hsail_breakpoint_set_from_kernel_name(const HsailBreakpointRequest* hsail_bp_req);

int hsail_breakpoint_set_from_line(const HsailBreakpointRequest* hsail_bp_req);

int hsail_breakpoint_set_any(const HsailBreakpointRequest* hsail_bp_req);

void hsail_breakpoint_update_statistics(const int* breakpoint_id, const int* hit_count, const int array_len);

void hsail_breakpoint_print_location(const struct breakpoint* p_bp);

bool hsail_breakpoint_is_real_condition(const HsailBreakpointCondition* p_condition);

void hsail_breakpoint_print_condition_string(const struct breakpoint* p_bp);

void hsail_breakpoint_print_stopped_reason(void);

bool hsail_breakpoint_compare_bp_request(const HsailBreakpointRequest* request_1,
                                         const HsailBreakpointRequest* request_2);

void hsail_breakpoint_copy_bp_request(HsailBreakpointRequest* dest_request,
                                      const HsailBreakpointRequest* src_request);

#endif // HSAIL_BREAKPOINT_H
