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

#include "defs.h"
#include "format.h"
#include "gdb_assert.h"
#include "ui-out.h"

#include "rocm-device.h"

static  int             g_devicesNum = 0;
static  RocmDeviceDesc  g_deviceDescriptors[AGENT_MAX_DEVICES_NUM];

bool rocm_set_devices(const HsailNotificationPayload* fifo_data)
{
    int i;
    gdb_assert(fifo_data->m_Notification == HSAIL_NOTIFY_DEVICES);
    g_devicesNum = fifo_data->payload.DevicesNotification.m_devicesNum;
    gdb_assert(g_devicesNum <= AGENT_MAX_DEVICES_NUM);

    if (g_devicesNum == 0)
    {
        return false;
    }
    for (i = 0; i < g_devicesNum; i++)
    {
        memcpy(&g_deviceDescriptors[i], &fifo_data->payload.DevicesNotification.m_deviceDescriptors[i], sizeof(RocmDeviceDesc));
    }

    return true;
}

void rocm_unset_active_device(void)
{
    int i;
    for (i = 0; i < g_devicesNum; i++)
    {
        g_deviceDescriptors[i].m_active = false;
    }
}

// Some of device info is not provided by the Runtime currently.
// Disable dumping this data until this is fixed in the Runtime.
#define  FULL_DEVICE_INFO   0

void rocm_print_devices_info(struct ui_out* uiout)
{
    int i;
    char  valuesStr[256];
    char  idxBuf[8] = "", nameBuf[30] = "", chipIDBuf[8] = "", numSEsBuf[8] = "", numCUsBuf[8] = "",
          numSIMDPerCUBuf[8] = "", numWavesPerCUBuf[8] = "", engineFreqBuf[8] = "", memFreqBuf[8] = "";

    if (g_devicesNum == 0)
    {
        ui_out_text(uiout, "No device info.\n");
        return;
    }

    printf_filtered("Devices info\n");

#if FULL_DEVICE_INFO
    printf_filtered("%5s%30s%12s%12s%12s%12s%12s%12s%12s\n", "Index", "Name",
                    "ChipID", "SEs", "CUs", "SIMDs/CU", "Waves/CU", "EngineFreq", "MemoryFreq");
#else
    printf_filtered("%5s%30s%12s%12s%12s%12s%12s\n", "Index", "Name",
                    "ChipID", "CUs", "Waves/CU", "EngineFreq", "MemoryFreq");
#endif

    for (i = 0; i < g_devicesNum; i++)
    {
        RocmDeviceDesc * pDesc = &g_deviceDescriptors[i];
        snprintf(idxBuf,     8, "%s%d", pDesc->m_active ? "*" : "", i);
        snprintf(nameBuf,   30,  "%s",  pDesc->m_deviceName);
        snprintf(chipIDBuf,  8,"0x%lx", pDesc->m_chipID);
#if FULL_DEVICE_INFO
        snprintf(numSEsBuf,  8,  "%d",  pDesc->m_numSEs);
#endif
        snprintf(numCUsBuf,  8,  "%d",  pDesc->m_numCUs);
#if FULL_DEVICE_INFO
        snprintf(numSIMDPerCUBuf,  8,  "%d",  pDesc->m_numSIMDsPerCU);
#endif
        snprintf(numWavesPerCUBuf, 8,  "%d",  pDesc->m_wavesPerCU);
        snprintf(engineFreqBuf,    8,  "%d",  pDesc->m_maxEngineFreq);
        snprintf(memFreqBuf, 8,  "%d",  pDesc->m_maxMemoryFreq);

#if FULL_DEVICE_INFO
        printf_filtered("%5s%30s%12s%12s%12s%12s%12s%12s%12s\n", idxBuf, nameBuf, chipIDBuf,
                        numSEsBuf, numCUsBuf, numSIMDPerCUBuf, numWavesPerCUBuf, engineFreqBuf, memFreqBuf);
#else
        printf_filtered("%5s%30s%12s%12s%12s%12s%12s\n", idxBuf, nameBuf, chipIDBuf,
                        numCUsBuf, numWavesPerCUBuf, engineFreqBuf, memFreqBuf);
#endif
    }
}
