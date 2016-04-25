/*
   ROCm GDB breakpoint functions and interfacing with HSAIL debug facilities

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

/* GDB headers */
#include "defs.h"
#include "annotate.h"
#include "ui-out.h"
#include "format.h"
#include "gdb_assert.h"
#include "breakpoint.h"
#include "utils.h"
#include "observer.h" /* Added for MI notifications */

/* Added for memcpy */
#include <string.h>

/* strtoull */
#include <stdlib.h>
#include <ctype.h>

/* Headers for shared mem */
#include <sys/ipc.h>
#include <sys/shm.h>

/* This header is needed for FacilitiesInterface */
#include <stdbool.h>

/* HSAIL-GDB headers*/
#include "hsail-breakpoint.h"
#include "hsail-cmd.h"
#include "hsail-fifo-control.h"
#include "hsail-tdep.h"
#include "hsail-utils.h"
#include "CommunicationControl.h"

/* Include HwDbgFacilities C interface*/
#include "FacilitiesInterface.h"

void hsail_breakpoint_save_binary_to_file(size_t binary_size, void* binary_buffer);

/* This buffer is static and managed by HwDbgFacilities */
static HwDbgInfo_debug gs_DbgInfo = NULL;
static char* gs_hsail_source = NULL;
static const HsailWaveDim3 gs_unknown_wave_dim = {-1,-1,-1};

int is_hsail_breakpoint(char *arg)
{
  HsailBreakpointRequest req;

  /* If the user just enters 'break'  the arg is NULL*/
  if(arg == NULL)
    {
      return 0;
    }

  memset(&req, 0, sizeof(HsailBreakpointRequest));

  /* Try to parse as hsail request: */
  if (!hsail_breakpoint_parse_bp_request(arg, &req))
    {
      return 0;
    }

  /* Clear the dummy request: */
  hsail_breakpoint_clear_bp_request(&req);

  return 1;
}

/**
 * For duplicate HSAIL breakpoint checks
 * Will return true if the two breakpoint requests are the same.
 * The two requests need to be exactly the same in every parameter, two nulls counts as equal
 */
bool hsail_breakpoint_compare_bp_request(const HsailBreakpointRequest* request_1,
                                         const HsailBreakpointRequest* request_2)
{
  bool ret_code = false;
  gdb_assert(NULL != request_1);
  gdb_assert(NULL != request_2);

  if (NULL == request_2)
    return false;

  if (NULL == request_1)
    return false;

  /* If the types are different, we are done */
  if (request_2->type != request_1->type)
    {
      return false;
    }

  switch (request_1->type)
  {
  case HSAIL_BP_TYPE_KERNEL_FUNCTION:
    ret_code = false;
    /*okay to use strcmp here since we expect the strings to be properly terminated*/
    if( request_1->bp.source_location.file_name == NULL ||
        request_2->bp.source_location.file_name == NULL)
      {
        if(strcmp(request_1->bp.kernel_func.func_name,
                  request_2->bp.kernel_func.func_name) == 0)
          {
            ret_code = true;
          }
      }
    else
      {
        ret_code = false;
      }
    break;

  case HSAIL_BP_TYPE_SOURCE_LOCATION:
    ret_code = false;
    if( request_1->bp.source_location.file_name != NULL &&
        request_2->bp.source_location.file_name != NULL)
      {
        /*Do the compare*/
        if(strcmp(request_1->bp.source_location.file_name,
                  request_2->bp.source_location.file_name) == 0
            &&
            request_1->bp.source_location.line_num ==
            request_2->bp.source_location.line_num)
          {
            ret_code = true;
          }
      }
    /* A null is reasonable since the user will not have entered a filename */
    if( request_1->bp.source_location.file_name == NULL &&
        request_2->bp.source_location.file_name == NULL)
      {
        /*Do the compare*/
        if(strcmp(request_1->bp.source_location.file_name,
                  request_2->bp.source_location.file_name) == 0
            &&
            request_1->bp.source_location.line_num ==
            request_2->bp.source_location.line_num)
          {
            ret_code = true;
          }
      }
    break;

  case HSAIL_BP_TYPE_ANY_LOCATION:
    ret_code = true;
    break;
  case HSAIL_BP_TYPE_UNKNOWN:
    gdb_assert(0);
    break;
  default:
      break;
  }

  return ret_code;

}

/* TODO incomplete
 * */
static void hsail_breakpoint_copy_bp_condition(HsailBreakpointCondition* dest_condition,
                                               const HsailBreakpointCondition* src_condition)
{
  int str_len = 0;
  if (NULL == dest_condition)
      return;

  if (NULL == src_condition)
      return;

  if (src_condition->condition_string != NULL)
    {
      str_len = strlen(src_condition->condition_string)+1;

      dest_condition->condition_string = (char*)malloc(sizeof(char)*str_len);

      memset(dest_condition->condition_string,'\0', str_len);
      strcpy(dest_condition->condition_string,  src_condition->condition_string);
    }
  else
    {
      dest_condition->condition_string = NULL;
    }

  dest_condition->condition_code = src_condition->condition_code;
  hsail_utils_copy_wavedim3(&(dest_condition->work_group_id), &(src_condition->work_group_id));
  hsail_utils_copy_wavedim3(&(dest_condition->work_item_id), &(src_condition->work_item_id));
}

/* Useful function for copying hsail breakpoint requests to and from the hsail breakpoint cache
 * */
void hsail_breakpoint_copy_bp_request(HsailBreakpointRequest* dest_request,
                                      const HsailBreakpointRequest* src_request)
{
  int k = 0;

  /* A placeholder for when the filename is not present */
  const char unknown_file_name[] = "temp_source";

  gdb_assert(NULL != dest_request);
  gdb_assert(NULL != src_request);

  if (NULL == dest_request)
      return;

  if (NULL == src_request)
      return;

  hsail_breakpoint_clear_bp_request(dest_request);

  dest_request->type = src_request->type;
  dest_request->number = src_request->number;

  gdb_assert(NULL != src_request->gdb_bkpt);
  dest_request->gdb_bkpt = src_request->gdb_bkpt;

  hsail_breakpoint_copy_bp_condition(&dest_request->condition,
                                     &src_request->condition);

  switch (src_request->type)
  {
  case HSAIL_BP_TYPE_KERNEL_FUNCTION:
    /* Memory will be freed by the clear_request function when we flush the buffer */

    /* Copy the function name */
    hsail_utils_copy_string(&(dest_request->bp.kernel_func.func_name), src_request->bp.kernel_func.func_name);
    break;

  case HSAIL_BP_TYPE_SOURCE_LOCATION:
    dest_request->bp.source_location.line_num = src_request->bp.source_location.line_num;
    /* A null is reasonable since the user will not have entered a filename */
    if(src_request->bp.source_location.file_name == NULL)
      {
        hsail_utils_copy_string(&(dest_request->bp.source_location.file_name), unknown_file_name);
      }
    else
      {
        /* Copy the file name */
        hsail_utils_copy_string(&(dest_request->bp.source_location.file_name), src_request->bp.source_location.file_name);
      }

    if (src_request->bp.source_location.src_line != NULL)
      {
        /* Copy the the source line string */
        hsail_utils_copy_string(&(dest_request->bp.source_location.src_line), src_request->bp.source_location.src_line);
      }
    break;

  case HSAIL_BP_TYPE_ANY_LOCATION:
    /*We need to copy the wildcard too*/
    /* Copy the function name */
    hsail_utils_copy_string(&(dest_request->bp.kernel_func.func_name), src_request->bp.kernel_func.func_name);
    break;
  case HSAIL_BP_TYPE_UNKNOWN:
    gdb_assert(0);
    break;
  default:
      break;
  }
}

static void hsail_breakpoint_clear_bp_condition(HsailBreakpointCondition* bp_condition)
{
  if (NULL == bp_condition)
    {
      return;
    }
  bp_condition->condition_code = HSAIL_BREAKPOINT_CONDITION_UNKNOWN;
  free_current_contents(&bp_condition->condition_string);

  hsail_utils_copy_wavedim3(&bp_condition->work_group_id, &gs_unknown_wave_dim);
  hsail_utils_copy_wavedim3(&bp_condition->work_item_id,  &gs_unknown_wave_dim);
}

void hsail_breakpoint_clear_bp_request(HsailBreakpointRequest* bp_request)
{
    if (NULL == bp_request)
        return;

    /* Clear any type-specific members: */
    switch (bp_request->type)
    {
      case HSAIL_BP_TYPE_KERNEL_FUNCTION:
        if (bp_request->bp.kernel_func.func_name != NULL)
        {
            free(bp_request->bp.kernel_func.func_name);
            bp_request->bp.kernel_func.func_name = NULL;
        }
        break;

      case HSAIL_BP_TYPE_SOURCE_LOCATION:
        if (bp_request->bp.source_location.file_name != NULL)
          {
            free(bp_request->bp.source_location.file_name);
            bp_request->bp.source_location.file_name = NULL;
          }
        if (bp_request->bp.source_location.src_line != NULL)
          {
            free(bp_request->bp.source_location.src_line);
            bp_request->bp.source_location.src_line = NULL;
          }
        break;

      case HSAIL_BP_TYPE_ANY_LOCATION:
        if (bp_request->bp.kernel_func.func_name != NULL)
          {
            free(bp_request->bp.kernel_func.func_name);
            bp_request->bp.kernel_func.func_name = NULL;
          }
        break;
      case HSAIL_BP_TYPE_UNKNOWN:
        break;
      default:
        break;
    }

    hsail_breakpoint_clear_bp_condition(&(bp_request->condition));

    /* Resest the type field: */
    bp_request->type = HSAIL_BP_TYPE_UNKNOWN;
}

static bool hsail_breakpoint_encode_condition(HsailBreakpointCondition* condition)
{
  bool ret_code = false;
  HsailWaveDim3 wg_id = {0, 0, 0};
  HsailWaveDim3 wi_id = {0, 0, 0};
  int temp_str_len = 0;

  char *first_non_num = NULL;

  if (condition == NULL)
  {
    return ret_code;
  }

  hsail_utils_copy_wavedim3(&wg_id,&gs_unknown_wave_dim);
  hsail_utils_copy_wavedim3(&wi_id,&gs_unknown_wave_dim);

  /* No condition to evaluate, its not an error*/
  if (condition->condition_string == NULL)
    {
      condition->condition_code = HSAIL_BREAKPOINT_CONDITION_ANY;
      ret_code = true;
    }
  else
    {
      if (condition->condition_string[0] != ' ' ||
          condition->condition_string[1] != 'i' ||
          condition->condition_string[2] != 'f' ||
          condition->condition_string[3] != ' ')
        {
          printf("An invalid condition string\n");
          ret_code = false;
        }
      else
        {
          const char* condition_string = NULL;
          int str_len = 0;

          int num_items[1] = {0};
          unsigned int wg_op[3]={0,0,0};
          unsigned int wi_op[3]={0,0,0};

          /* The 4th location is the first valid location after "if "
           * However, to be more stable we to ignore all spaces after the " if "
           * */
          int first_valid_arg_location = 4;
          while (condition->condition_string[first_valid_arg_location] == ' ' &&
                 condition->condition_string[first_valid_arg_location] != '\0')
            {
              ++first_valid_arg_location;
            }

          /* condition_string start at the first location after "if" and its following spaces.
           * For instance, "if wg:0,0,0 wi:1,0,0", condition_string will point to the first "w"
           * of "wg", or the first "w" of "wi" for "if wi:1,0,0 wg:0,0,0".*/
          condition_string = &(condition->condition_string[first_valid_arg_location]);

          bool wg_ret_code = false;
          bool wi_ret_code = false;
          wg_ret_code = hsail_command_get_argument(
                                  condition_string,
                                  "wg",
                                  wg_op,
                                  num_items);

          /*printf("WG Op is\t %d\t %d \t %d \t %s \n ",wg_op[0], wg_op[1], wg_op[2], condition_string);*/

          wi_ret_code = hsail_command_get_argument(
                                  condition_string,
                                  "wi",
                                  wi_op,
                                  num_items);
          /*printf("WI Op is\t %d\t %d \t %d \t %s \n ",wi_op[0], wi_op[1], wi_op[2], condition_string);*/

          condition->condition_code = HSAIL_BREAKPOINT_CONDITION_EQUAL;
          condition->work_group_id.x = wg_op[0];
          condition->work_group_id.y = wg_op[1];
          condition->work_group_id.z = wg_op[2];

          condition->work_item_id.x = wi_op[0];
          condition->work_item_id.y = wi_op[1];
          condition->work_item_id.z = wi_op[2];

          ret_code = wg_ret_code && wi_ret_code;
        }

    }

  return ret_code;
}

static bool hsail_breakpoint_split_condition_string(char* hsail_bp_str,
                                                    HsailBreakpointCondition* condition)
{
  bool ret_code = false;
  char* if_location = NULL;
  int condition_len = 0;
  const char sub_string[] = " if ";

  if (hsail_bp_str == NULL || condition == NULL)
    {
      return ret_code;
    }

  /* we just need to get the first token */
  if_location = strstr(hsail_bp_str, sub_string);

  free_current_contents(&condition->condition_string);

  if (if_location != NULL)
    {
      condition_len = strlen(if_location)+1;
      condition->condition_string = xmalloc(sizeof(char)*condition_len);
      gdb_assert(condition->condition_string != NULL);

      if (condition->condition_string != NULL)
        {
          memset(condition->condition_string, '\0', condition_len);
          strcpy(condition->condition_string, if_location);

          /* We now smash the rest of the hsail bp string so that the rest of the
           * breakpoint parsing is not affected
           * */
          memset(if_location,'\0', strlen(if_location));
          ret_code = true;
        }
    }
  else
    {
      ret_code = true;
    }
  return ret_code;
}



/* This function accepts only the breakpoint command string, the condition string (if available)
 * is parsed before this function is called
 * */
static bool hsail_breakpoint_parse_bp_command(const char* hsail_bp_str, HsailBreakpointRequest* bp_request)
{
    /* Local variables: */
    size_t l = 0, ll = 0;
    int first_colon_loc = 0, i = 0;
    char *first_non_num = NULL, *file_str = NULL;
    unsigned long long req_num = 0;

    /* Input check: */
    if (NULL == hsail_bp_str || NULL == bp_request)
        return false;

    /* Allowed formats:
     * rocm / rocm:        Break on any GPU kernel dispatch.
     * rocm:kernel_name    Break on entry to kernel named kernel_name.
     * rocm:file_name:123  Break on the indicated line on the indicated file name.
     * rocm:123            Break on the indicated line in the currently debugged kernel's main source file. */
    /* Get the size: */
    l = strlen(hsail_bp_str);

    /* Skip whitespace: */
    SKIP_LEADING_SPACES(hsail_bp_str, l);

    /* Verify the prefix: */
    if (4 > l)
        return false;
    if ('r' != hsail_bp_str[0] ||
        'o' != hsail_bp_str[1] ||
        'c' != hsail_bp_str[2] ||
        'm' != hsail_bp_str[3])
      {
        return false;
      }

    /* Move up the pointer: */
    hsail_bp_str += 4;
    l -= 4;

    /* Skip whitespace: */
    SKIP_LEADING_SPACES(hsail_bp_str, l);

    /* If the new string is empty, this is an "any" breakpoint ("rocm"): */
    if (0 == l || NULL == hsail_bp_str || '\0' == hsail_bp_str[0])
    {
        bp_request->type = HSAIL_BP_TYPE_ANY_LOCATION;
        return true;
    }

    /* Else, verify the next character is a colon: */
    if (':' != hsail_bp_str[0])
        return false;

    /* Move up the pointer again: */
    ++hsail_bp_str;
    --l;

    /* Skip whitespace: */
    SKIP_LEADING_SPACES(hsail_bp_str, l);

    /* If the new string is empty, this is an "any" breakpoint ("rocm:"): */
    if (0 == l || NULL == hsail_bp_str || '\0' == hsail_bp_str[0])
    {
        bp_request->type = HSAIL_BP_TYPE_ANY_LOCATION;
        return true;
    }

    /* See if the remainder parses as a positive number. If it is, this is a generic line request: */
    first_non_num = NULL;
    req_num = strtoull(hsail_bp_str, &first_non_num, 0);
    if (NULL != first_non_num)
    {
        /* Check if nothing else remained: */
        ll = strlen(first_non_num);
        SKIP_LEADING_SPACES(first_non_num, ll);
        if (0 == ll)
        {
            /* Output the request (hsail:123): */
            bp_request->type = HSAIL_BP_TYPE_SOURCE_LOCATION;
            free_current_contents(&(bp_request->bp.source_location.file_name));
            bp_request->bp.source_location.line_num = (HwDbgInfo_linenum)req_num;
            return true;
        }
    }

    /* Else, find / count the number of colons in the remaining string. Note that a colon is illegal
     * both in a function name and a file path: */
#ifdef _WIN32
#pragma warning ("This part of the code was not designed for windows targets, and will require special handling to support Windows-style full paths 'X:\...' !")
#endif
    first_colon_loc = -1;
    for (i = 0; ((int)l > i) && ('\0' != hsail_bp_str[i]); i++)
    {
        /* Save the first colon location: */
        if (':' == hsail_bp_str[i])
        {
            if (-1 < first_colon_loc)
                /* Two or more colons, not a currently supported format. Fail the function: */
                return false;
            else
                first_colon_loc = i;
        }
    }

    if (-1 == first_colon_loc)
    {
        /* No colons in the string, treat it as a kernel function name request: */
        TRIM_TAILING_SPACES(hsail_bp_str, l);

        /* The case where l now equals 0 should have been handled previously, so no need to check it. */
        bp_request->bp.kernel_func.func_name = (char*)malloc(l + 1);
        if (NULL == bp_request->bp.kernel_func.func_name)
            return false;

        /* Output the request (rocm:kernel_name): */
        bp_request->type = HSAIL_BP_TYPE_KERNEL_FUNCTION;
        memcpy(bp_request->bp.kernel_func.func_name, hsail_bp_str, l);
        bp_request->bp.kernel_func.func_name[l] = '\0';
        return true;
    }

    /* See if the text after the colon correctly parses as a positive number: */
    first_non_num = NULL;
    req_num = strtoull(&hsail_bp_str[first_colon_loc + 1], &first_non_num, 0);
    if (NULL != first_non_num)
    {
        /* Check if nothing else remained: */
        ll = strlen(first_non_num);
        SKIP_LEADING_SPACES(first_non_num, ll);

        if (0 == ll)
        {
            /* "Default" file name: */
            file_str = NULL;

            /* Trim the file name potion: */
            ll = first_colon_loc - 1;
            TRIM_TAILING_SPACES(hsail_bp_str, ll);
            if (0 != ll)
            {
                if ((ll + 1) <= 0)
                    return false;

                file_str = malloc(ll + 1);
                if (NULL == file_str)
                    return false;
                memcpy(file_str, hsail_bp_str, ll);
                file_str[ll] = '\0';
            }

            /* Output the request (rocm:file_name:123 / rocm:123): */
            bp_request->bp.source_location.file_name = file_str;
            bp_request->type = HSAIL_BP_TYPE_SOURCE_LOCATION;
            bp_request->bp.source_location.line_num = (HwDbgInfo_linenum)req_num;
            return true;
        }
    }

    /* Unexpected / unhandled breakpoint type: */
    return false;
}

/* This function copies the input string into a breakpoint condition string and
 * the breakpoint command string.
 * We need to separate out the condition string so that the breakpoint can be eva
 * */
bool hsail_breakpoint_parse_bp_request(const char* ip_hsail_bp_str,
                                       HsailBreakpointRequest* bp_request)
{
  bool status = false;
  char* hsail_bp_str = NULL;
  int ip_strlen = 0;
  if (ip_hsail_bp_str == NULL)
    {
      return status;
    }

  ip_strlen = strlen(ip_hsail_bp_str);

  /* Verify the prefix: */
  if (4 > ip_strlen)
    {
      return status;
    }

  if ('r' != ip_hsail_bp_str[0] ||
      'o' != ip_hsail_bp_str[1] ||
      'c' != ip_hsail_bp_str[2] ||
      'm' != ip_hsail_bp_str[3])
    {
      return status;
    }

  /* Copy the breakpoint string so we can smash the condition substring
   * for when we parse the brekapoint command
   * */
  hsail_bp_str = xmalloc(sizeof(char)*ip_strlen+1);
  if (hsail_bp_str == NULL || bp_request == NULL)
    {
      return false;
    }
  memset(hsail_bp_str, '\0', ip_strlen+1);
  strcpy(hsail_bp_str, ip_hsail_bp_str);

  hsail_breakpoint_clear_bp_request(bp_request);

  /* Separate out the breakpoint string and the condition string */
  status = hsail_breakpoint_split_condition_string(hsail_bp_str,
                                                   &(bp_request->condition));

  if(!status)
    {
      printf("Could not parse GPU breakpoint condition string\n");
      return status;
    }

  status = hsail_breakpoint_encode_condition(&bp_request->condition);
  if(!status)
    {
      printf("could not encode condition string\n");
      return status;
    }

  /* Populate the request structure */
  status = hsail_breakpoint_parse_bp_command(hsail_bp_str, bp_request);
  if(!status)
    {
      printf("Could not parse the GPU breakpoint request.\n");
      printf("ROCm-gdb will now parse \"%s\" as a host code breakpoint.\n", ip_hsail_bp_str);
      return status;
    }
  free(hsail_bp_str);

  return status;
}

AgentBinaryNotification g_binary_notification = HSAIL_AGENT_BINARY_UNKNOWN;


char* hsail_dbginfo_get_source_buffer(void)
{
  struct ui_out *uiout = current_uiout;
  if (gs_hsail_source == NULL)
    {
      ui_out_text(uiout, "HSAIL source buffer not available\n");
    }

  return gs_hsail_source;
}

/* This function takes in the debuginfo handle so that it can
 * get the source buffer for gdb.
 */
char* hsail_dbginfo_get_srcline_from_buffer(const HwDbgInfo_debug dbg,
                                            const HwDbgInfo_linenum line_num)
{

  char* op_str = NULL;
  HwDbgInfo_linenum count = 0;
  int temp_file_desc = -1;
  char temp_file_name[] = "/tmp/rocm_dbg_XXXXXX";

  int status = 0;
  FILE* temp_file_handle = NULL;
  int raw_index = 0;
  char* raw_line = NULL;
  char* get_line_array = NULL;
  char* op_line = NULL;
  int i = 0;
  size_t len =0;
  gdb_assert(dbg != NULL);

  /* Get a temp file name, mkstemp will populate the XXXXXX in temp_file_name
   *  we dont use the desc, we will open the file using fopen*/
  temp_file_desc = mkstemp(temp_file_name);
  gdb_assert(temp_file_desc  != -1);

  temp_file_handle = fopen(temp_file_name,"wb");
  gdb_assert(temp_file_handle != NULL);

  fprintf(temp_file_handle, "%s",gs_hsail_source);
  rewind(temp_file_handle);
  fclose(temp_file_handle);

  temp_file_handle = fopen(temp_file_name,"rt");
  gdb_assert(temp_file_handle != NULL);
  count = 1;

  raw_line = xmalloc(sizeof(char)*AGENT_MAX_SOURCE_LINE_LEN);
  op_line = xmalloc(sizeof(char)*AGENT_MAX_SOURCE_LINE_LEN);

  gdb_assert(NULL != raw_line);
  gdb_assert(NULL != op_line);

  memset(raw_line, '\0', AGENT_MAX_SOURCE_LINE_LEN);
  memset(op_line, '\0', AGENT_MAX_SOURCE_LINE_LEN);

  while (!feof(temp_file_handle))
    {
      /* We copy the get_line_array into the raw_line array since we
       * want to keep the line limited to AGENT_MAX_SOURCE_LINE_LEN
       * */
      if (getline(&get_line_array, &len, temp_file_handle) == -1)
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

  /* We need to remove the last newline character and the leading space */
  raw_index=0;
  i=0;
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

  free_current_contents(&raw_line);

  status = remove(temp_file_name);
  gdb_assert(status == 0);

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

  /*op_line will be free'd by the breakpoint request that for the source line */
  return op_line;
}


AgentBinaryNotification hsail_is_debug_facilities_loaded(void)
{
  return g_binary_notification;
}

/*
 * These functions assume that we will only get one binary from the agent once
 * This function is called from the SIGHSAIL handler.
 * The SIGHSAIL handler is called when the inferior signals it's parent (gdb) to
 * tell it that we have a new binary
 */
void hsail_dbginfo_set_facilities_status(const AgentBinaryNotification notification)
{
  g_binary_notification = notification;
  if (notification == HSAIL_AGENT_BINARY_UNKNOWN)
    {
      hsail_free_hwdbginfo();
    }
}

void hsail_free_hwdbginfo(void)
{
  if (gs_DbgInfo == NULL)
    {
      return;
    }
  else
    {
      gdb_assert(gs_DbgInfo!= NULL);

      /* Release the debug info handle: */
      hwdbginfo_release_debug_info(&gs_DbgInfo);

      gs_DbgInfo = NULL;
    }
}

void hsail_breakpoint_save_binary_to_file(size_t binary_size, void* binary_buffer)
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

/*
 * Initialize the hwdbginfo handle by calling the debug Facilities API
 * with the binary found in a shmem segment.
 *
 * The HwDbgInfo_debug object is cached. If there is already a binary loaded,
 * then this function will return a copy of the HwDbgInfo_debug object.
 * If you expect that a binary has already been loaded, then this function can
 * be called with NULL and the cached object will be returned
 */
HwDbgInfo_debug hsail_init_hwdbginfo(HsailNotificationPayload* payload)
{
  struct ui_out *uiout = NULL;
  /* pointer to shared memory*/
  void* pShm = NULL;

  /* HwDbgFacilities objects */
  HwDbgInfo_err errOut = 0;
  HwDbgInfo_debug dbg_op = NULL;

  const char* temp_hsail_src = NULL;

  uint64_t hsail_source_len = 0;
  const int max_shared_mem_size = hsail_get_agent_binary_shmem_max_size();

  if (payload == NULL)
    {
      /* return the cached dbgInfo there exists */
      return gs_DbgInfo;
    }

  gdb_assert(payload->m_Notification == HSAIL_NOTIFY_NEW_BINARY);

  uiout = current_uiout;
  /* Shared memory buffer pointer*/

  dbg_op = NULL;
  if(hsail_is_debug_facilities_loaded())
    {

      /* A copy of the shared memory segment */
      void* dbe_binary = NULL;
      size_t dbe_binary_size = 0;

      int shmid = -1;

      /*1M used is hard wired for now*/
      shmid = shmget(hsail_get_agent_binary_shmem_key(), max_shared_mem_size, 0666);

      if (shmid <= 0)
        {
          ui_out_text(uiout, "GDB: HwDbgFacilities init: shmid is invalid\n");
        }

      gdb_assert(shmid > 0);

      /* Get shm pointer */
      pShm = (int*)shmat(shmid, NULL, 0);

      if (pShm == NULL)
        {
          ui_out_text(uiout, "GDB: HwDbgFacilities init: pShm is NULL\n");
        }

      gdb_assert(pShm != NULL);

      dbe_binary_size = ((size_t*)pShm)[0];

      gdb_assert(dbe_binary_size > 0 && dbe_binary_size < max_shared_mem_size);

      dbe_binary = xmalloc(dbe_binary_size);

      gdb_assert(dbe_binary != NULL);

      memcpy(dbe_binary,(size_t*)pShm+1,dbe_binary_size);

      /* Uncomment this call if you need to save the binary to the file
      hsail_breakpoint_save_binary_to_file(dbe_binary_size, dbe_binary);
      */

      /* Granite (HSA 0.95) needs a hard-wired kernel name: */
      /* dbg_op = hwdbginfo_init_with_hsa_1_0_binary(dbe_binary, dbe_binary_size, "&__Gdt_vectoradd_kernel", &errOut); */
      /* Use Obsidian (HSA 1.0) binary format (May. 2015): */
      dbg_op = hwdbginfo_init_with_hsa_1_0_binary(dbe_binary,
                                                  dbe_binary_size,
                                                  &errOut);

      /* Keep this printf here as a reminder for a
       * quick way to check that the IPC happened correctly*/

      /*
      int i = 0;
      for(i = 0; i<10;i++)
        {
          printf("%d \t %d\n",i,*((int*)dbe_binary + i));
        } */

      if (errOut != HWDBGINFO_E_SUCCESS)
        {
          /* HwDbgFacilities init: Called DebugFacilities Incorrectly.
           * We can add more detailed messages such as low-level dwarf or high level dwarf missing in the future
           * */
          ui_out_text(uiout, "[ROCm-gdb]: The code object for the current dispatch does not contain debug information\n");

          fflush(stdout);

          free_current_contents(&dbe_binary);
          return NULL;
        }

      gdb_assert(errOut == HWDBGINFO_E_SUCCESS);

      /* We can clear the dbe_binary buffer once we have initialized HWDbgFacilities */
      free_current_contents(&dbe_binary);

      /* Get the kernel source */
      hsail_source_len = 0;
      errOut = hwdbginfo_get_hsail_text(dbg_op, &temp_hsail_src, &hsail_source_len);
      if (errOut != HWDBGINFO_E_SUCCESS)
        {
          ui_out_text(uiout, "[ROCm-gdb]: HwDbgFacilities get hsail text error\n");
        }

      gdb_assert(temp_hsail_src != NULL);
      gdb_assert(hsail_source_len != 0);

      if (gs_hsail_source == NULL)
        {
          gs_hsail_source = xmalloc(hsail_source_len*sizeof(char)+1);
        }
      else
        {
          /* We need to resize the kernel source buffer for the new hwdbginfo object */
          free_current_contents(&gs_hsail_source);
          gs_hsail_source = xmalloc(hsail_source_len*sizeof(char)+1);
        }

      gdb_assert(gs_hsail_source != NULL);
      memset(gs_hsail_source, '\0', hsail_source_len*sizeof(char) + 1);
      memcpy(gs_hsail_source, temp_hsail_src, hsail_source_len);
      /*printf("====Static HSAIL Source len %d\n===\n %s\n",hsail_source_len, gs_hsail_source);*/

      /* Detach shared memory */
      if (shmdt(pShm) == -1)
        {
          ui_out_text(uiout, "GDB: HwDbgFacilities init: Error detaching shm\n");
        }


    }

  /* cache the dgbInfo */
  gs_DbgInfo = dbg_op;

  return gs_DbgInfo;
  /*
   * This function's caller will now call the Facilities API to get the pc;
   * */
}

/* This is a utility function to print the filename information */
int hsail_dbginfo_test_all_mapped_addrs(HwDbgInfo_debug dbg)
{
  HwDbgInfo_addr* addrs = NULL;
  HwDbgInfo_err err = 0;
  char* filename_buff = NULL;
  HwDbgInfo_code_location loc = NULL;

  size_t returned_filename_len = 0;
  size_t returned_addrCount = 0;

  /* Just a large buffer size */
  const size_t max_filename_len=10240;
  const size_t max_addrCount=10240;

  addrs = (HwDbgInfo_addr*)xmalloc(max_addrCount * sizeof(HwDbgInfo_addr));

  if (NULL == addrs)
    return -1;

  memset(addrs, 0, max_addrCount * sizeof(HwDbgInfo_addr));

  hwdbginfo_all_mapped_addrs(dbg,max_addrCount,addrs,&returned_addrCount);

  err = hwdbginfo_addr_to_line(dbg,addrs[0],&loc);

  if (HWDBGINFO_E_SUCCESS != err)
    {
      printf("Error \n");
      return -1; /* unexpected error */
    }

  /* +1 to append null-terminator '\0' */
  filename_buff = (char*)xmalloc(max_filename_len*sizeof(char) + 1);
  gdb_assert(NULL != filename_buff);

  if(NULL == filename_buff)
    return -1;

  memset(filename_buff, '\0', max_filename_len*sizeof(char) + 1);

  hwdbginfo_code_location_details(loc,&addrs[0],max_filename_len,filename_buff, &returned_filename_len);

  printf("Filename is %s \n",filename_buff);

  xfree(addrs);
  xfree(filename_buff);
  return 0;

}

/*
 * This function is called exactly *once* for each kernel function breakpoint created.
 * Based on the appropriate state, the request goes to the buffer or to the FIFO
 */
int hsail_breakpoint_set_from_kernel_name(const HsailBreakpointRequest* hsail_bp_req)
{
  gdb_assert(NULL != hsail_bp_req);
  if (is_hsail_linux_initialized())
    {
      /**
       * Enqueue to FIFO
       */
      char* kernel_name = NULL;
      struct breakpoint* gdb_handle = hsail_bp_req->gdb_bkpt;
      gdb_assert(NULL != gdb_handle);

      kernel_name = hsail_bp_req->bp.kernel_func.func_name;
      gdb_assert(kernel_name != NULL);

      hsail_breakpoint_copy_bp_request(gdb_handle->hsail_bp_request,
                                       hsail_bp_req);

      hsail_enqueue_create_kernel_name_breakpoint_packet(kernel_name, hsail_bp_req->number);

    }
  else
    {
      /**
       * Will copy this request to request buffer, we do not have hsail initialized.
       */
      hsail_enqueue_create_breakpoint_request_buffer(hsail_bp_req);

    }
  return 1;
}

/*
* This function is called exactly *once* for each kernel function breakpoint created.
* Based on the appropriate state, the request goes to the buffer or to the FIFO
*/
int hsail_breakpoint_set_any(const HsailBreakpointRequest* hsail_bp_req)
{
    gdb_assert(NULL != hsail_bp_req);
    if (is_hsail_linux_initialized())
    {
        /**
        * Enqueue to FIFO
        */
        struct breakpoint* gdb_handle = hsail_bp_req->gdb_bkpt;
        gdb_assert(NULL != gdb_handle);

        hsail_breakpoint_copy_bp_request(gdb_handle->hsail_bp_request,
            hsail_bp_req);

        hsail_enqueue_create_kernel_name_breakpoint_packet(hsail_bp_req->bp.kernel_func.func_name,
                                                           hsail_bp_req->number);
    }
    else
    {
        /**
        * Will copy this request to request buffer, we do not have hsail initialized.
        */
        hsail_enqueue_create_breakpoint_request_buffer(hsail_bp_req);
    }
    return 1;
}

/* This function does the actual resolving of the breakpoint, it tries to get the
 * cached debug facilities, gets the PC and then enqueues the breakpoint
 * */
static int hsail_breakpoint_from_line_resolve(const HsailBreakpointRequest* hsail_bp_req)
{

  HwDbgInfo_err err = 0;
  /* The code location: based on the input from the command line */
  HwDbgInfo_code_location loc = NULL;
  HwDbgInfo_code_location resolvedLoc = NULL;

  HwDbgInfo_addr* addrs = NULL;
  size_t addrCount = 0;

  size_t i = 0;

  char* src_line = NULL;
  HwDbgInfo_debug hsail_facilities = NULL;

  char* target_file_name = NULL;
  HwDbgInfo_linenum line_num = 0;
  struct breakpoint *gdb_bkpt_handle = NULL;

  gdb_assert(is_hsail_linux_initialized());
  gdb_assert(hsail_is_debug_facilities_loaded());
  gdb_assert(hsail_bp_req != NULL);
  gdb_assert(hsail_bp_req->type == HSAIL_BP_TYPE_SOURCE_LOCATION);

  /* dig out stuff we know from the structure for readability here */
  target_file_name      = hsail_bp_req->bp.source_location.file_name;
  line_num              = hsail_bp_req->bp.source_location.line_num;
  gdb_bkpt_handle       = hsail_bp_req->gdb_bkpt;
  gdb_assert(gdb_bkpt_handle != NULL);

  hsail_facilities = hsail_init_hwdbginfo(NULL);

  gdb_assert(hsail_facilities != NULL);

  loc = hwdbginfo_make_code_location(NULL, line_num);
  if (NULL == loc)
    {
      printf_filtered("Could not make a code location %s \t %llu\n", target_file_name, line_num);
      return -1; /* error */
    }

  /* Get the closest legal code location: */
  err = hwdbginfo_nearest_mapped_line(hsail_facilities, loc, &resolvedLoc);
  hwdbginfo_release_code_locations(&loc, 1);

  if (0 == resolvedLoc && HWDBGINFO_E_NOTFOUND == err)
    {
      if (target_file_name == NULL)
        {
          printf_filtered("[rocm-gdb: No line %llu in temp_source]\n", line_num);
        }
      else
        {
          printf_filtered("[rocm-gdb: No line %llu in %s]\n", line_num, target_file_name);
        }
      return 0; /* file / line combination not mapped */
    }
  else if (HWDBGINFO_E_SUCCESS != err)
    {
      printf_filtered("Could not get the nearest mapped line \t Error %d\n",err);
      printf_filtered("Nearest mapped line to  %s \t %llu\n", target_file_name, line_num);

      return -1; /* unexpected error */
    }

  /* Get the number of ISA addresses for this location: */
  addrCount = 0;
  err = hwdbginfo_line_to_addrs(hsail_facilities, resolvedLoc, 0, NULL, &addrCount);

  if (0 == addrCount && HWDBGINFO_E_NOTFOUND == err)
    {
      hwdbginfo_release_code_locations(&resolvedLoc, 1);
      return 0; /* file / line combination not mapped */
    }
  else if (HWDBGINFO_E_SUCCESS != err)
    {
      hwdbginfo_release_code_locations(&resolvedLoc, 1);
      gdb_assert(HWDBGINFO_E_SUCCESS == err);
      return -1; /* unexpected error */
    }


  addrs = (HwDbgInfo_addr*)xmalloc(addrCount * sizeof(HwDbgInfo_addr));
  gdb_assert(NULL != addrs);

  memset(addrs, 0, addrCount * sizeof(HwDbgInfo_addr));

  /* Get the ISA addresses for this location: */
  err = hwdbginfo_line_to_addrs(hsail_facilities, resolvedLoc, addrCount, addrs, NULL);
  hwdbginfo_release_code_locations(&resolvedLoc, 1);
  if (HWDBGINFO_E_SUCCESS != err)
    {
      gdb_assert(HWDBGINFO_E_SUCCESS == err);
      return -1; /* error */
    }

  gdb_bkpt_handle->hsail_pc = addrs[0];

  /* Mechanism to update the breakpoint's HSAIL request structure with source line */
  src_line = hsail_dbginfo_get_srcline_from_buffer(hsail_facilities,
                                                   line_num);

  /* We only need to send the 1st one */
  hsail_enqueue_create_breakpoint_packet(addrs[0],
                                         gdb_bkpt_handle->number,
                                         src_line,
                                         line_num,
                                         &(hsail_bp_req->condition));


  /*
   * We need to check if this location is a NOP
   * How do I know that Location[addr[i]] == NOP ?
   * We have commented out the loop below since we know in hsail
   * the first address in a NOP and so we only need to send the 1st one
   */

  /*
  for ( i = 0; i < addrCount; i++)
    {
      printf("Addresses are %llud \t %llxx \n", addrs[i], addrs[i]);
      hsail_enqueue_create_breakpoint_packet(addrs[i],b->number);
    }
  */

  /*
   *  We can also pass the code location to the agent, that way we dont need to
   *  move data back from gdb to the agent.
   *  For now we enqueue the line number to the agent, which saves the
   *  line from the disassembled hsail text
   */

  hsail_breakpoint_copy_bp_request(gdb_bkpt_handle->hsail_bp_request,
                                   hsail_bp_req);



  gdb_assert(src_line != NULL);

  gdb_bkpt_handle->hsail_bp_request->bp.source_location.src_line = src_line;

  observer_notify_breakpoint_modified(gdb_bkpt_handle);

  xfree(addrs);

  return 0;
}

int hsail_breakpoint_set_from_line(const HsailBreakpointRequest* hsail_bp_req)
{
  int ret_code = 0 ;
  gdb_assert(NULL != hsail_bp_req);

  if (is_hsail_linux_initialized() &&
      hsail_is_debug_facilities_loaded())
    {
      ret_code = hsail_breakpoint_from_line_resolve(hsail_bp_req);

      /* We should not assert since the user can enter the wrong line */
      if(ret_code == -1)
        {
          printf_filtered("Invalid breakpoint line number\n");
        }
      return ret_code;
    }
  else
    {

      hsail_enqueue_create_breakpoint_request_buffer(hsail_bp_req);

    }

  return ret_code;
}


void hsail_breakpoint_update_statistics(const int* breakpoint_id, const int* hit_count, const int array_len)
{
  int i = 0;
  int bp_posn = 0;
  struct breakpoint* p_bkpt = NULL;
  gdb_assert(breakpoint_id != NULL);
  gdb_assert(hit_count != NULL);

  /*
   * Utility in breakpoint.h to query the breakpoint chain
   * */
  for (i=0; i < array_len; i++)
    {
      /* The GDB ID of the breakpoint sent by the agent*/
      bp_posn = breakpoint_id[i];

      if (bp_posn != -1)
        {
          p_bkpt = get_breakpoint(bp_posn );

          /* We dont assert for NULL since it is possible that GDB may delete
           * the breakpoint but the Agent may still report it has not processed
           * the delete command from GDB yet, not updating any statistics is
           * enough correct behavior
           *
           * gdb_assert(p_bkpt != NULL);
           * */
          if (p_bkpt != NULL)
            {
              if (p_bkpt->type != bp_hsail)
                {
                  printf_filtered("A non GPU type breakpoint was reported by the agent\n");
                }
              gdb_assert(p_bkpt->type == bp_hsail);

              /* We overwrite the counter here directly since the agent knows how many times
               * the breakpoint was hit
               * */
              p_bkpt->hit_count = hit_count[i];
            }
        }
    }

}

/* Return true if this is a condition worth printing */
bool hsail_breakpoint_is_real_condition(const HsailBreakpointCondition* p_condition)
{
  bool ret_code = false;
  if (p_condition != NULL)
    {
      switch (p_condition->condition_code)
      {
      case HSAIL_BREAKPOINT_CONDITION_EQUAL:
        ret_code = true;
        break;
      default:
        break;
      }
    }

  return ret_code;
}

/* Print condition string to the ui_out. The caller of the function needs
 * to handle the spacing or line feeds before and after */
void hsail_breakpoint_print_condition_string(const struct breakpoint* p_bp)
{
  struct ui_out *uiout = current_uiout;
  char hsail_cond_str[1024] = {0};
  gdb_assert(hsail_cond_str != NULL);
  memset(hsail_cond_str, '\0', sizeof(hsail_cond_str));

  gdb_assert(p_bp != NULL);
  gdb_assert(p_bp->type == bp_hsail);
  gdb_assert(p_bp->hsail_bp_request != NULL);

  switch (p_bp->hsail_bp_request->condition.condition_code)
  {
  case HSAIL_BREAKPOINT_CONDITION_EQUAL:
    {
      sprintf(hsail_cond_str, "Stop only if: WG: %d,%d,%d and WI: %d,%d,%d  Active",
              p_bp->hsail_bp_request->condition.work_group_id.x,
              p_bp->hsail_bp_request->condition.work_group_id.y,
              p_bp->hsail_bp_request->condition.work_group_id.z,
              p_bp->hsail_bp_request->condition.work_item_id.x,
              p_bp->hsail_bp_request->condition.work_item_id.y,
              p_bp->hsail_bp_request->condition.work_item_id.z);
      ui_out_field_string (uiout, "cond", hsail_cond_str);
      break;
    }
  default:
    break;
  }
}


void hsail_breakpoint_print_location(const struct breakpoint* p_bp)
{
  struct ui_out *uiout = current_uiout;
  char hsail_pc_str[15] = {0};
  char hsail_src_str[1024] = {0};
  char hsail_any_bp_str[64] = {0};

  memset(hsail_pc_str, '\0', sizeof(hsail_pc_str));
  memset(hsail_src_str, '\0', sizeof(hsail_src_str));
  memset(hsail_any_bp_str, '\0', sizeof(hsail_any_bp_str));

  gdb_assert(uiout != NULL);
  gdb_assert(p_bp != NULL);
  gdb_assert(p_bp->type == bp_hsail);
  gdb_assert(p_bp->hsail_bp_request != NULL);

  switch (p_bp->hsail_bp_request->type)
  {
  case HSAIL_BP_TYPE_KERNEL_FUNCTION:
    {
      ui_out_field_string (uiout, "addr", "---    ");
      if (p_bp->hsail_bp_request->bp.kernel_func.func_name != NULL)
        {
          /* If it is the wild-card character, add the prefix of "every dispatch",
           * else just print the function name
           * */
          if(p_bp->hsail_bp_request->bp.kernel_func.func_name[0] == (char)'*' )
            {
              sprintf(hsail_any_bp_str, "Every GPU dispatch(%s)",
                      p_bp->hsail_bp_request->bp.kernel_func.func_name);
              ui_out_field_string (uiout, "What", hsail_any_bp_str);
            }
          else
            {
              ui_out_field_string (uiout, "What", p_bp->hsail_bp_request->bp.kernel_func.func_name);
            }
        }
      else
        {
          ui_out_field_string (uiout, "What", "Unknown GPU kernel breakpoint");
        }
      break;
    }
  case HSAIL_BP_TYPE_SOURCE_LOCATION:
    {
      sprintf(hsail_pc_str, "PC:0x%04x ", p_bp->hsail_pc);
      ui_out_field_string (uiout, "addr", hsail_pc_str);

      if (p_bp->hsail_bp_request->bp.source_location.src_line != NULL)
        {
          memset(hsail_src_str,'\0',sizeof(hsail_src_str));
          snprintf(hsail_src_str, 1024,"%s temp_source@line %llu", p_bp->hsail_bp_request->bp.source_location.src_line,
                  p_bp->hsail_bp_request->bp.source_location.line_num);
          ui_out_field_string (uiout, "What", hsail_src_str);
        }
      else
        {
          ui_out_field_string (uiout, "What", "Unknown HSAIL source line");
        }

      break;
    }
  case HSAIL_BP_TYPE_ANY_LOCATION:
    {
      ui_out_field_string (uiout, "addr", "---    ");
      sprintf(hsail_any_bp_str, "Every GPU dispatch(%s)",
              p_bp->hsail_bp_request->bp.kernel_func.func_name);

      /* The func_name will contain the special character*/
      if (p_bp->hsail_bp_request->bp.kernel_func.func_name != NULL)
        {
          ui_out_field_string (uiout, "What", hsail_any_bp_str);
        }
    }
    break;
  default:
    ui_out_field_string (uiout, "addr", "Unknown GPU breakpoint type");
  }

}
