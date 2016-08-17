/*
   ROCm GDB functions to handle the focus work-group and work-item.

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

#if !defined (HSAIL_THREAD_H)
#define HSAIL_THREAD_H 1

/* The header files shared with the agent*/
#include "CommunicationControl.h"

void hsail_thread_clear_focus(void);

void hsail_thread_set_focus(HsailWaveDim3 focusWg, HsailWaveDim3 focusWi);

void hsail_thread_set_focus_command(char *arg, int from_tty);

void hsail_thread_get_current_focus(HsailWaveDim3* focus_wg, HsailWaveDim3* focus_wi);

#endif // HSAIL_THREAD_H
