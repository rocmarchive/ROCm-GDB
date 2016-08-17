/*
   ROCm help message strings.

   Copyright (c) 2015 ADVANCED MICRO DEVICES, INC.  All rights reserved.
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

#if !defined (HSAIL_HELP_H)
#define HSAIL_HELP_H 1

#define HSAIL_PRINT_HELP_ARGS()\
"ROCm variable print commands:\n"\
"print rocm:<variable>\t\t   Print value of <variable> for the focus work-item\n"

#define HSAIL_LINE_SEPARATOR()\
"--------------------------------------------------------------------------\n"

#define HSAIL_PRINT_HELP()\
"When debugging a GPU kernel, GPU registers and variables can be printed.\n"\
HSAIL_PRINT_HELP_ARGS()

#define HSAIL_SET_LOGGING_HELP()\
  "Internal ROCm logging options\n"\
  "set rocm logging [on|off] \n"

#define HSAIL_SET_CMD_HELP()\
"ROCm specific configuration commands: \n"\
"set rocm trace [on|off] \t   Enable/Disable tracing of GPU dispatches\n"\
"set rocm trace <filename> \t   Save GPU dispatch trace to <filename>\n"\
"set rocm logging [on|off] \t   Enable/Disable internal logging\n"\
"set rocm show-isa [on|off] \t   Enable/Disable saving ISA to a temp_isa file when in GPU dispatches\n"

#define HSAIL_SHOW_CMD_HELP()\
"Show the current ROCm specific configuration options: \n"\
"show rocm \t\t\t   Prints the current state of ROCm configuration options\n"

#define HSAIL_INFO_HELP_STRING()\
"ROCm info commands:\n"\
"The info rocm work-group and work-item output includes the hardware slot ids for the Shader Engine (SE), "\
"the Shader Array (SH), the Compute Unit (CU), the SIMD and the Wavefront slot within each SIMD (Wave) for "\
"each executing wave\n"\
"\n"\
"info rocm kernels \t\t   Print all GPU kernel dispatches\n"\
"info rocm kernel <kernel_name> \t   Print all GPU kernel dispatches with a specific <kernel_name>\n"\
"info rocm [work-groups|wgs] \t   Print all GPU work-group items\n"\
"info rocm [work-group|wg] [<flattened_id>|<x,y,z>]  Print a specific GPU work-group item\n"\
"info rocm [work-item|wi|work-items|wis] \t    Print the focus GPU work-item\n"\
"info rocm [work-item|wi] <x,y,z>   Print a specific GPU work-item\n"


#define HSAIL_BREAK_HELP_ARGS()\
"ROCm breakpoint commands:\n"\
"break rocm\t\t\t   Break on every GPU dispatch \n"\
"break rocm:<kernel_name>\t   Break when kernel <kernel_name> is about to begin execution \n"\
"break rocm:<line_number>\t   Break when execution hits line <line_number> in temp_source \n"

#define HSAIL_BREAK_HELP()\
"For HSA applications: ROCm-gdb supports function breakpoints \n"\
"and kernel source breakpoints. \n"\
"Function breakpoints: Break execution when a dispatch with a certain name \n"\
"is about to start execution on the GPU agent.\n"\
"Kernel source breakpoints: Break execution when execution hits an GPU dispatch \n"\
"source line \n"\
HSAIL_BREAK_HELP_ARGS()


#define HSAIL_THREAD_HELP_ARGS()\
"ROCm focus thread command:\n"\
"rocm thread wg:<x,y,z> wi:<x,y,z>  Switch focus to a specific active GPU work-item\n"

#define HSAIL_THREAD_HELP()\
"For HSA applications, ROCm-gdb can switch focus between different work-items when debugging a GPU kernel dispatch\n"\
HSAIL_THREAD_HELP_ARGS()

#define HSAIL_DISASSEMBLE_HELP_COMMAND()\
"To disassemble a GPU kernel:\n"\
"disassemble \t\t\t   Show the GPU ISA disassembly text when at a GPU breakpoint\n"

#define HSAIL_DISASSEMBLE_HELP()\
"This command has been enhanced to disassemble GPU kernels.\n"\
"ROCm-gdb can disassemble a GPU kernel when at a GPU breakpoint or a GPU function breakpoint\n"\
HSAIL_DISASSEMBLE_HELP_COMMAND()\
"\n"

#define HSAIL_GLOBAL_HELP()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_THREAD_HELP_ARGS()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_BREAK_HELP_ARGS()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_INFO_HELP_STRING()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_SET_CMD_HELP()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_SHOW_CMD_HELP()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_PRINT_HELP_ARGS()\
  HSAIL_LINE_SEPARATOR()\
  HSAIL_DISASSEMBLE_HELP_COMMAND()\
  HSAIL_LINE_SEPARATOR()

#endif
