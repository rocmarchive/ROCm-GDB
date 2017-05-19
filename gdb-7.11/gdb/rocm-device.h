/*
   ROCm GDB functions related to GPU devices.

   Copyright (c) 2017 ADVANCED MICRO DEVICES, INC.  All rights reserved.
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

#if !defined (ROCM_DEVICE_H)
#define ROCM_DEVICE_H 1

#include "CommunicationControl.h"

// Add devices info received from the Agent.
bool  rocm_set_devices(const HsailNotificationPayload* fifo_data);

// Mark all devices as not active. This function is called
// when the GPU debugging is done.
void  rocm_unset_active_device(void);

// Print the devices info.
void  rocm_print_devices_info (struct ui_out* uiout);

#endif // ROCM_DEVICE_H
