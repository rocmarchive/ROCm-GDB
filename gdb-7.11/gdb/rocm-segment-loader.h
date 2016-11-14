/*
   ROCm loaded segment functions

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

#if !defined (HSAIL_SEGMENT_LOADER_H)
#define HSAIL_SEGMENT_LOADER_H 1

void hsail_segment_initialize_loader(void);

void hsail_segment_shutdown_loader(void);

void hsail_segment_update_loadmap(void);

bool hsail_segment_resolve_elfva(const uint64_t elf_va, uint64_t* mem_addr_out);

bool hsail_segment_resolve_memva(const uint64_t mem_addr, uint64_t* elf_va_out );

#endif
