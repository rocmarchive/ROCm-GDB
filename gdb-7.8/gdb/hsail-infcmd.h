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


/* bool type */
#include <stdbool.h>

#define HSAIL_STEP_OUT  0
#define HSAIL_STEP_OVER 1
#define HSAIL_STEP_IN   2

bool is_hsail_step(void);
void hsail_set_step_breakpoints(int steptype, int count);

void hsail_set_continue_dispatch(void);

/* Called from the handle_hsail_event to set the dispatch thread's id */
void hsail_infcmd_set_dispatch_thread_pid(int dispatch_thread_id);

/* Defined in infcmd.c */
bool hsail_infcmd_is_focus_thread_dispach_thread(void);
