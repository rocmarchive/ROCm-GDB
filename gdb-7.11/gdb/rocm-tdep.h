/*
   ROCm GDB debugging initialization and communication files

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

#if !defined (HSAIL_TDEP_H)
#define HSAIL_TDEP_H 1

#include <stdbool.h>

#include "event-loop.h"

#include "CommunicationControl.h"

/*
 * ROCm-GDB close-down function
 * This function closes the communication fifo to the hsail agent
 */
void hsail_linux_close(void);

/* Clean up all global HSAIL buffers and close any trace files: Done on gdb exit*/
void hsail_linux_do_final_cleanup(void);

/*
 * ROCm-GDB initialization functions
 * This function is called on every linux_nat_wait but will exit quickly once initialized
 * The variables evaluating hsail initialization state are within rocm-tdep.c
 */
void hsail_linux_initialize_stg1(void );
void hsail_linux_initialize_stg2(void );

int is_hsail_linux_initialized(void);

void hsail_linux_post_inferior_create(void);

/* Returns true if we are in device debug mode and some active waves
 * are present.
 * The return code is used by the linux_nat_kill function to delay the ptrace kill
 * and host kill steps till we get an acknowledgement from the agent
 * that the dispatch has been killed
 * */
bool hsail_tdep_kill_all_waves(bool is_quit_command);

bool hsail_is_focus_device(void);

bool hsail_is_focus_predispatch(void);

uint64_t hsail_tdep_get_current_pc(void);

bool hsail_is_signal_from_agent(const char* signal_name);

int hsail_get_fifo_handler(void);

int hsail_get_read_fifo_handler(void);

int hsail_tdep_get_active_wave_count(void);

void* hsail_tdep_map_isa_buffer(void);

void* hsail_tdep_map_loadmap_buffer(void);

void* hsail_tdep_map_momentary_bp_buffer(void);

void* hsail_tdep_map_wave_buffer(void);

void hsail_tdep_unmap_shm_buffer(void* pShm);

bool hsail_tdep_save_isa(bool is_disassemble_command, const char* hsail_isa_file_name);

/*
 * Get the keys and max sizes for all shared memory segments.
 *
 * We could include the shared header in the breakpoint resolving files,
 * However, the GNU linker complains about constants declared in multiple places,
 * The other alternative that may be better is that we would have to declare all
 * the constants in the shared header to be static
 */
const int hsail_get_agent_binary_shmem_key(void);

const int hsail_get_agent_binary_shmem_max_size(void);

const int hsail_get_wave_buffer_shmem_key(void);

const int hsail_get_wave_buffer_shmem_max_size(void);

const int hsail_get_momentary_bp_buffer_shmem_key(void);

const int hsail_get_momentary_bp_buffer_shmem_max_size(void);


/* Function to handle each hsail event */
void handle_hsail_event(int err, gdb_client_data client_data);

#endif // HSAIL_TDEP_H
