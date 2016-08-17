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

#if !defined (HSAIL_TRACE_H)
#define HSAIL_TRACE_H 1

/* Configure the tracing form rocm-cmd */
void hsail_trace_configure(const char* ip_option);

void hsail_trace_initialize(void);

void hsail_trace_stop(void);

void hsail_trace_add_dispatch(const HsailNotificationPayload* fifo_data);

void hsail_trace_print_configuration(void);

#endif // HSAIL_TRACE_H
