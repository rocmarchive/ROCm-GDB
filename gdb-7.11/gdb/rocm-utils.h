/*
ROCm GDB utilities functions

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


#if !defined (HSAIL_UTILS_H)
#define HSAIL_UTILS_H 1

/* GDB headers */
#include "defs.h"

#include "CommunicationControl.h"
#include "FacilitiesInterface.h"

typedef enum _HsailFilePermission
{
    HSAIL_FILE_READ_ONLY,
    HSAIL_FILE_READ_WRITE

} HsailFilePermission;

/* Helper macros: */

/* helper printf_filtered, adds the ROCm prefix
 * Provides a single place for any future changes such as MI messages
 * */
#define rocm_printf_filtered(format,args...)     \
  printf_filtered("[ROCm-gdb]: "format, ##args);

#define SKIP_LEADING_SPACES(str, str_len) \
  while ((0 < str_len) && (' ' == *str || '\t' == *str || '\r' == *str || '\n' == *str)) \
  { ++str; --str_len; }

#define TRIM_TAILING_SPACES(str, str_len) \
  while ((0 < str_len) && (' ' == str[str_len - 1] || '\t' == str[str_len - 1] || '\r' == str[str_len - 1] || '\n' == str[str_len - 1])) \
  { --str_len; }


/* Return true if the file exists, false otherwise*/
bool hsail_utils_check_file_exists(const char* filename);

/* Allocate memory for *dest_str and copy src_str into it, and append a null-terminator. *dest_str cannot be char array.*/
void hsail_utils_copy_string(char** dest_str, const char* src_str);

/* Wavedim3 utils */
void hsail_utils_copy_wavedim3(HsailWaveDim3* dest_wavedim, const HsailWaveDim3* src_wavedim);

/* Wavedim3 utils */
bool hsail_utils_compare_wavedim3(const HsailWaveDim3* op1, const HsailWaveDim3* op2);

int hsail_utils_command_index(char* arg, struct ui_out *uiout);

/* Get a timestamp as a string*/
char* hsail_utils_get_timestamp(void);

/* Return the home directory for the user */
char* hsail_utils_get_home_directory(void);

char* hsail_utils_read_line_from_file(const char* file_name, HwDbgInfo_linenum line_num);

/* Read file into a array, allocates the array and returns the size. The caller has to free it. */
bool hsail_utils_read_file_to_array(const char* file_name, char** op_buffer, size_t* op_len);

bool hsail_utils_rename_file(const char* old_file_name, const char* new_file_name);

/* Return true if permission is changed*/
bool hsail_utils_set_file_permission(const char* filename, const HsailFilePermission permission);

/* Sanitize the input file names provided, expands "~/foo" into "/home/username/foo"
 * Allocates the op buffer and the caller has to free it.
 * */
void hsail_utils_sanitize_file_name(char** sanitized_op_file_name, const char* ip_file_name);

/* Save buffer to a file */
void hsail_utils_save_binary_buffer_to_file(size_t binary_size, void* binary_buffer);

#endif /* HSAIL_UTILS_H */
