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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/stat.h>


/* GDB headers */
#include "defs.h"
#include "gdb_assert.h"

/* ROCm GDB headers */
#include "rocm-utils.h"

void hsail_utils_copy_string(char** dest_str, const char* src_str)
{
  {
    int str_len = 0;
    char* l_str = NULL;
    if (NULL == dest_str || NULL == src_str)
    {
      gdb_assert(NULL != dest_str);
      gdb_assert(NULL != src_str);
      return;
    }

    /* +1 for the null-terminator */
    str_len = strlen(src_str) + 1;
    l_str = xmalloc(str_len);
    gdb_assert(NULL != l_str);

    if (NULL != l_str)
    {
      memset(l_str, '\0', str_len);
      strcpy(l_str, src_str);

      /* Free the old buffer before assigning the new one to it */
      if(NULL != *dest_str)
      {
        free(*dest_str);
        *dest_str = NULL;
      }
      *dest_str = l_str;
    }
  }
}

/* The caller needs to free memory */
void hsail_utils_sanitize_file_name(char** sanitized_op_file_name, const char* ip_file_name)
{
  /*op_file_name is what we work with and will write its addr to sanitized_op_file_name */
  char* op_file_name = NULL;
  size_t op_file_name_len = 0;
  if (sanitized_op_file_name != NULL)
    {
      *sanitized_op_file_name = NULL;
    }
  if (ip_file_name == NULL)
    {
      return;
    }

  /* If there is a home directory in the filename '~/foo.csv' */
  if (strlen (ip_file_name) > 2)
    {
      if (ip_file_name[0] == '~' && ip_file_name[1] =='/')
        {
          char* home_dir = hsail_utils_get_home_directory();
          if (home_dir != NULL)
            {
              op_file_name_len = strlen(home_dir) + strlen(ip_file_name) + 2;
              op_file_name = xmalloc(op_file_name_len*sizeof(char) );
              if (op_file_name != NULL)
                {
                  memset(op_file_name, '\0', op_file_name_len);
                  /* we need to skip the 1st character '~' in ip_file_name */
                  sprintf(op_file_name, "%s%s",hsail_utils_get_home_directory(), &ip_file_name[1] );
                }
            }
        }
    }
  else
    {
      op_file_name_len = strlen(ip_file_name) + 1;
      op_file_name = xmalloc((op_file_name_len)*sizeof(char));

      if (op_file_name != NULL)
        {
          memset(op_file_name, '\0', op_file_name_len);
          strcpy(op_file_name, ip_file_name);
        }
    }

  *sanitized_op_file_name = op_file_name;
}

char* hsail_utils_get_home_directory(void)
{
  char* home_dir = NULL;

  if ((home_dir = getenv("HOME")) == NULL)
    {
      home_dir = getpwuid(getuid())->pw_dir;
    }

  return home_dir;
}

char* hsail_utils_get_timestamp(void)
{
  char* op_string = NULL;
  time_t ltime; /* calendar time */
  ltime=time(NULL); /* get current cal time */
  op_string = asctime( localtime(&ltime) );

  gdb_assert(op_string != NULL);

  return op_string;
}

void hsail_utils_copy_wavedim3(HsailWaveDim3* dest_wavedim, const HsailWaveDim3* src_wavedim)
{
  gdb_assert(src_wavedim != NULL);
  gdb_assert(dest_wavedim != NULL);

  dest_wavedim->x = src_wavedim->x;
  dest_wavedim->y = src_wavedim->y;
  dest_wavedim->z = src_wavedim->z;
}

bool hsail_utils_compare_wavedim3(const HsailWaveDim3* op1, const HsailWaveDim3* op2)
{
  gdb_assert(op1 != NULL);
  gdb_assert(op2 != NULL);
  return ((op1->x == op2->x) && (op1->y == op2->y) && (op1->z == op2->z));
}

int hsail_utils_command_index(char* arg, struct ui_out *uiout)
{
  char index_buffer[256] = { 0 };
  int buffer_length = 0;
  int arg_length = 0;
  int ret_val = -1;

  gdb_assert(NULL != arg);
  gdb_assert(NULL != uiout);
  arg_length = strlen(arg);
  memset(index_buffer, '\0', sizeof(index_buffer));

  SKIP_LEADING_SPACES(arg,arg_length);

  while((buffer_length < arg_length) && isdigit(arg[buffer_length]) && buffer_length < 256)
  {
    index_buffer[buffer_length] = arg[buffer_length];
    buffer_length++;
  }

  /* validate the that index termiates in end of line of space, not alpha so it is valid (spaces)*/
  if (buffer_length >= arg_length || arg[buffer_length] == ' ' || arg[buffer_length] == '\t' || arg[buffer_length] == '\r' || arg[buffer_length] == '\n')
  {
    /* convert the buffer to value */
    index_buffer[buffer_length] = 0;
    ret_val = atoi(index_buffer);
  }

  /* if no conversion was made or fo some reason we got a negative value */
  if (ret_val < 0)
  {
    ui_out_text(uiout,"Invalid index provided for info command\n");
  }

  return ret_val;
}

bool hsail_utils_check_file_exists(const char* filename)
{
  if (filename == NULL)
    {
      return false;
    }

  if( access( filename, F_OK ) != -1 )
    {
      return true;
    }
  else
    {
      return false;
    }
}

bool hsail_utils_set_file_permission(const char* filename, const HsailFilePermission permission)
{
  int stat=1;
  if (filename == NULL)
    {
      return false;
    }
  if(!hsail_utils_check_file_exists(filename))
    {
      return false;
    }

  if (permission == HSAIL_FILE_READ_ONLY)
    {
      stat = chmod(filename, S_IREAD);
    }
  else if (permission == HSAIL_FILE_READ_WRITE)
    {
      stat = chmod(filename, S_IREAD | S_IWRITE);
    }
  else
    {
      printf("unhandled permission for %s \n", filename);
      gdb_assert(0);
    }

  if (stat)
    {
      printf("Couldn't change permission for %s\n", filename);
      return false;
    }
  else
    {
      return true;
    }
}

bool hsail_utils_read_file_to_array(const char* file_name, char** op_buffer, size_t* op_len)
{
  char *source = NULL;
  FILE *fp = NULL;
  bool ret_code = false;

  if (file_name == NULL || op_buffer == NULL || op_len == NULL)
    {
      return ret_code;
    }

  fp = fopen(file_name, "r");
  if (fp != NULL)
    {
      /* Go to the end of the file. */
      if (fseek(fp, 0L, SEEK_END) == 0)
        {
          /* Get the size of the file. */
          size_t read_length = 0;
          long bufsize = ftell(fp);
          if (bufsize == -1)
            {
              return ret_code;
            }

          source = malloc(sizeof(char) * (bufsize + 1));
          if (source == NULL)
            {
              return ret_code;
            }

          memset(source, '\0', sizeof(char) * (bufsize + 1));
          /* Go back to the start of the file. */
          if (fseek(fp, 0L, SEEK_SET) != 0)
            {
              return ret_code;
            }

          /* Read file */
          read_length = fread(source, sizeof(char), bufsize, fp);
          if (read_length == 0)
            {
              printf("Error reading file %s", file_name);
            }
          else
            {
              source[read_length++] = '\0'; /* Just to be safe. */
              *op_len = bufsize;
              *op_buffer = source;
              ret_code = true;
            }
        }
      fclose(fp);
  }

  return ret_code;
}

char* hsail_utils_read_line_from_file(const char* file_name, HwDbgInfo_linenum line_num)
{
  FILE* file_handle = NULL;
  char* raw_line = NULL;
  char* get_line_array = NULL;
  char* return_ptr = NULL;
  HwDbgInfo_linenum count = 1;

  if (file_name == NULL)
    {
      return NULL;
    }

  file_handle = fopen(file_name,"rt");
  gdb_assert(file_handle != NULL);

  raw_line = xmalloc(sizeof(char)*AGENT_MAX_SOURCE_LINE_LEN);
  gdb_assert(NULL != raw_line);
  memset(raw_line, '\0', AGENT_MAX_SOURCE_LINE_LEN);

  while (!feof(file_handle))
    {
      size_t len =0;

      /* We copy the get_line_array into the raw_line array since we
       * want to keep the line limited to AGENT_MAX_SOURCE_LINE_LEN
       * */
      if (getline(&get_line_array, &len, file_handle) == -1)
        {
          break;
        }
      if (count == line_num)
        {
          /* AGENT_MAX_SOURCE_LINE_LEN-1 since we don't want smash the \0 */
          strncpy(raw_line, get_line_array, AGENT_MAX_SOURCE_LINE_LEN-1);
          break;
        }
      count++;
    }

  /* note: getline reallocates the array if not null, so we only need to free it at the end*/
  if (get_line_array != NULL)
    {
      free_current_contents(&get_line_array);
    }

  /* Build the output only if the line number matched, otherwise return NULL*/
  if (count == line_num)
    {
      int raw_index = 0;
      int i = 0;
      char* op_line = NULL;

      op_line = xmalloc(sizeof(char)*AGENT_MAX_SOURCE_LINE_LEN);
      gdb_assert(NULL != op_line);
      memset(op_line, '\0', AGENT_MAX_SOURCE_LINE_LEN);

      /* We need to remove the last newline character and the leading space */
      while(raw_line[raw_index] != '\0')
        {
          if(isspace(raw_line[raw_index]))
            {
              raw_index++;
            }
          else
            {
              break;
            }
        }

      /*Move string to beginning since we want to use same pointer to delete*/
      for(i=0; raw_line[i] != '\0';i++)
        {
          /* We dont want to copy a new line character over,
           * but we do want a semi-colon if present so we don't check for semi-colon
           * before the copy */
          if (raw_line[raw_index+i] == '\n')
            {
              break;
            }
          op_line[i]=raw_line[raw_index+i];

          /*If we see a semicolon, end it*/
          if (op_line[i] == ';')
            {
              break;
            }
        }

      op_line[i+1]='\0';

      /* It is possible that the op_line string is now empty if the input line
       * number had only space or line feeds.
       * However we should still send some valid characters to the agent since
       * the breakpoint could resolve to a nearby PC just fine.
       * In the future this could be improved to send a neighboring line or something.
       * For now just add a space to the line.
       * */
      if (strlen(op_line) == 0)
        {
          op_line[0]=' ';
          op_line[1]='\0';
        }

      return_ptr = op_line;
    }


  fclose(file_handle);
  free_current_contents(&raw_line);
  return return_ptr;
}

void hsail_utils_save_binary_buffer_to_file(size_t binary_size, void* binary_buffer)
{
  static int call_count = 0;
  FILE* handle;
  char filename[128];

  call_count = call_count+1;
  gdb_assert(binary_buffer !=  NULL);

  memset((void*)filename,'\0', 128*sizeof(char));
  sprintf(filename, "kernel_buffer_%d.bin",call_count);

  handle = fopen(filename,"wb");

  /* Write your buffer to disk. */
  if (handle)
    {
      fwrite(binary_buffer, binary_size, 1, handle);
      printf("Wrote to file %s \n", filename);
    }
  else
    {
      printf("Something wrong writing to File.");
    }
  fclose(handle);
}

/* Rename or move files, fails if the new file already exists */
bool hsail_utils_rename_file(const char* old_file_name, const char* new_file_name)
{
  int err_no = 0;
  bool ret_code = false;
  if (!hsail_utils_check_file_exists(new_file_name))
    {
      err_no = rename(old_file_name, new_file_name);
      if( err_no == 0)
        {
          ret_code = true;
        }
      else
        {
          printf("Could not rename \"%s\" to \"%s\" \n",old_file_name, new_file_name);
        }
    }
  return ret_code;
}
