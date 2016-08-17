/*
   ROCm GDB commands for GNU debugger GDB.

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

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GDB headers */
#include "defs.h"
#include "gdbtypes.h"
#include "cli/cli-cmds.h"
#include "command.h"
#include "expression.h"
#include "gdb_assert.h"
#include "value.h"

/* rocm-gdb headers */
#include "rocm-cmd.h"
#include "rocm-fifo-control.h"
#include "rocm-help.h"
#include "rocm-kernel.h"
#include "rocm-print.h"
#include "rocm-thread.h"
#include "rocm-trace.h"
#include "rocm-tdep.h"
#include "rocm-utils.h"

/* The header files shared with the agent*/
#include "CommunicationControl.h"

/* Chain containing all defined hsail subcommands.  */
struct cmd_list_element *hsailcmdlist;

/* forward declaration of functions */
static void hsail_info_command(char *arg, int from_tty);
static void hsail_cmd_set_options_help(void);

void _initialize_hsailcmd (void);

/* A buffer that is used by gdb's set command processing logic to save the
 * arguments passed to the "set rocm" command
 * For e.g: for set rocm logging on, the "logging on" should be save to this buffer
 * */
char* g_hsail_argument_buff = NULL;

static const int gs_hsail_argument_buff_len = 256;

/* Bool option to "set rocm show-isa"*/
static bool gs_isa_show_isa_enabled = false;

/* Bool option to "set rocm logging"*/
static bool gs_internal_logging_enabled = false;

static void hsail_info_param_print_help(void)
{
  struct ui_out *uiout = current_uiout;
  ui_out_text(uiout,"Invalid parameter\n");
  ui_out_text(uiout, HSAIL_INFO_HELP_STRING());
}

/* Add a element to the hsail subcommands*/
static struct cmd_list_element *
add_hsail_cmd(const char *name, void (*fun) (char *, int), char *doc)
{
  return add_cmd (name, no_class, fun, doc, &hsailcmdlist);
}

/*
 argName:is an input string that the function looks for and starts to dig out the dim3 entry after
 */
bool hsail_command_get_argument(const char* cmdInfo, const char* argName, unsigned int* itemDim3, int* numItems)
{
  int cmdLen = 0;
  int argLen = 0;
  int i = 0;
  char digitBuf[256]="";
  int currentDigit = 0;

  gdb_assert(NULL != cmdInfo);
  gdb_assert(NULL != argName);
  gdb_assert(NULL != itemDim3);
  gdb_assert(NULL != numItems);

  argLen = strlen(argName);
  *numItems = 0;

  /* check if the argName exists */
  SKIP_LEADING_SPACES(argName, argLen);

  /* Find the first argName sub string in cmdInfo */
  cmdInfo = strstr(cmdInfo, argName);
  if (NULL == cmdInfo)
  {
    return false;
  }

  cmdLen = strlen(cmdInfo);

  if (argLen != 0)
  {
    cmdInfo += argLen;
    cmdLen -= argLen;
    /* check for ':' */
    SKIP_LEADING_SPACES(cmdInfo, cmdLen);
    if (cmdInfo[0] != ':')
      return false;

    cmdInfo += 1;
    cmdLen -= 1;
  }

  /* read up to three digits separated by comma */
  for (i = 0 ; i < 3 && cmdLen > 0; i++)
  {
    SKIP_LEADING_SPACES(cmdInfo, cmdLen);
    currentDigit = 0;
    /* if starting with a digit check the digit comma format if not exit assuming next section might be
       part of the next needed parameters
    */
    if (isdigit(cmdInfo[0]))
    {
      while (isdigit(cmdInfo[0]) && cmdLen > 0)
      {
        digitBuf[currentDigit] = cmdInfo[0];
        cmdLen--;
        currentDigit++;
        cmdInfo++;
      }
      digitBuf[currentDigit] = '\0';
      itemDim3[i] = atoi(digitBuf);

      /* at the end check it is the end of the data or it ends with comma */
      SKIP_LEADING_SPACES(cmdInfo, cmdLen);

      /* we don't return a false here since there could be a valid command still left
         over in the arg buffer.
         For example the original input could be wg:1,2,3 wi:4,5,6.
         as part of "b rocm:46 if wg:1,2,3 wi:4,5,6"
         In this case, the cmdInfo buffer at this stage includes the "wi: 4,5,6"

         We probably need to improve this logic to detect if a valid command follows
         in the buffer */
      if ((cmdLen != 0) && cmdInfo[0] != ',')
        {
          return true;
        }

      cmdInfo++;
      cmdLen--;
    }
    /* count the actual number of items that were added to the itemDim */
    *numItems = i + 1;
  }

  return true;
}

static bool hsail_info_parameter_check(char* arg)
{
  /* We need to copy the input buffer since strtok changes arg*/
  char* arg_copy = NULL;
  int arg_len = 0;

  char* token = NULL;
  bool ret_code = false;
  const char s[2] = " ";

  gdb_assert(NULL != arg);

  arg_len = strlen(arg)+1;
  arg_copy = (char*)xmalloc(sizeof(char)*(arg_len));
  gdb_assert(NULL != arg_copy);

  if (arg_copy == NULL)
    {
      return ret_code;
    }
  memset(arg_copy,'\0',arg_len);
  strcpy(arg_copy, arg);

  /* we just need to get the first token */
  token = strtok(arg_copy, s);
  ret_code = false;

  if (token != NULL)
    {
      if (strcmp(token, "kernel") != 0 && strcmp(token, "kernels") != 0 &&
          strcmp(token, "wgs") != 0 && strcmp(token, "wg") != 0 &&
          strcmp(token, "work-group") != 0 && strcmp(token, "work-groups") != 0 &&
          strcmp(token, "wis") != 0  && strcmp(token, "wi") != 0 &&
          strcmp(token, "work-item") != 0  && strcmp(token, "work-items") != 0
          )
        {
          ret_code = false;
        }
      else
        {
          ret_code = true;
        }
    }

  xfree(arg_copy);
  return ret_code;
}

static void hsail_info_command(char *arg, int from_tty)
{
  struct ui_out *uiout = current_uiout;
  int index = 0;
  int numItems = 0;
  unsigned int workGroup[3] = {0,0,0};
  unsigned int workItem[3] = {0,0,0};
  bool foundParam = true;
  HsailWaveDim3 temp_work_item = {0,0,0};

  HsailWaveDim3 active_wg;
  HsailWaveDim3 active_wi;
  hsail_thread_get_current_focus(&active_wg, &active_wi);

  if (arg == NULL)
    {
      hsail_info_param_print_help();
      return ;
    }
  if (!hsail_info_parameter_check(arg))
    {
      hsail_info_param_print_help();
      return ;
    }

  if (strcmp(arg,"wavefronts") == 0)
  {
    /* todo: FB 11279 Disable for Alpha
    * hsail_print_wave_info (current_uiout, -1);
    * */
    hsail_info_param_print_help();
  }
  else if (strncmp(arg,"kernels", 7) == 0)
  {
    hsail_kernel_print_info (current_uiout, -1);
  }
  else if (strncmp(arg,"kernel ", 7) == 0)
  {
    arg += 7;
    hsail_kernel_print_specific_info(arg, current_uiout, -1);
  }
  else if (strncmp(arg,"work-groups", 11) == 0 || strncmp(arg,"wgs", 3) == 0)
  {

    hsail_print_workgroups_info (active_wg, current_uiout, -1);

  }
  else if (strncmp(arg,"work-group ", 11) == 0 || strncmp(arg,"wg ", 3) == 0)
  {
    if (strncmp(arg,"work-group ", 11) == 0)
    {
      arg += 11;
    }
    else
    {
      arg += 3;
    }
    foundParam = hsail_command_get_argument(arg, "", workGroup, &numItems);
    if (foundParam && (numItems == 1 || numItems == 3))
    {
      /* numItems = 1 assume it is flattened id */
      if (numItems == 1)
      {
        if (workGroup[0] >= 0)
        {
          hsail_print_specific_workgroup_by_id_info(workGroup[0], current_uiout, -1);
        }
      }
      else if (numItems == 3)
      {
          hsail_print_specific_workgroup_info(workGroup, current_uiout, -1);
      }
    }
    else
    {
      ui_out_text(uiout,"use 'info rocm work-group' with flattened id or x,y,z identifier:\n");
      ui_out_text(uiout,"'info rocm work-group ID' or 'info rocm work-group x,y,z'\n");
    }

  }
  else if (strncmp(arg,"work-item", 9) == 0 || strncmp(arg,"wi", 2) == 0)
  {
    if (strncmp(arg,"work-item", 9) == 0)
    {
      arg += 9;
    }
    else
    {
      arg += 2;
    }

    /* If it is "work-item" or "wis", we need to skip the "s" so that it won't take the "s" as
       another input parameter*/
    if(strncmp(arg, "s", 1) == 0)
    {
      arg += 1;
    }

    foundParam = hsail_command_get_argument(arg, "", workItem, &numItems);
    /* if no param was entered then use active work item, else use work item x,y,z. Any other
       formats are not allowed and explain to the user the allowed formats */
    if (!foundParam || numItems == 0)
    {
      hsail_print_workitem_info (active_wg, active_wi, true, current_uiout, -1);
    }
    else if (numItems == 3)
    {
      foundParam = false;
      if (workItem[0] == active_wi.x && workItem[1] == active_wi.y && workItem[2] == active_wi.z)
      {
        foundParam = true;
      }
      temp_work_item.x = workItem[0];
      temp_work_item.y = workItem[1];
      temp_work_item.z = workItem[2];

      hsail_print_workitem_info (active_wg, temp_work_item, foundParam, current_uiout, -1);
    }
    else
    {
      ui_out_text(uiout,"'info rocm work-item' with no arguments will print info for current work-item\n");
      ui_out_text(uiout,"'info rocm work-item x,y,z'  will print info for work-item x,y,z\n");
    }
  }
  else
  {
    hsail_info_param_print_help();
  }
}




static void hsail_cmd_set_internal_logging(const char* ip_option)
{

  if (ip_option!= NULL)
    {
      if(strcmp(ip_option,"on") == 0)
        {
          printf_filtered("Enable internal ROCm logging\n");
          gs_internal_logging_enabled = true;
          hsail_cmd_reset_internal_logging();
        }
      else if(strcmp(ip_option,"off") == 0)
        {
          printf_filtered("Disable internal ROCm logging\n");
          gs_internal_logging_enabled = false;
          hsail_cmd_reset_internal_logging();
        }
      else
        {
          printf_filtered(HSAIL_SET_LOGGING_HELP());
        }
    }
  else
    {
      printf_filtered(HSAIL_SET_LOGGING_HELP());
    }

}

/* Set the internal logging based on the user's input.
 *
 * If this function is called when initialization is not complete, nothing will be done.
 * This function will be called again as part of initialization (stage 2) when the FIFOs
 * have been created.*/
void hsail_cmd_reset_internal_logging(void)
{
  if(is_hsail_linux_initialized())
    {
      if (gs_internal_logging_enabled)
        {
          hsail_enqueue_set_logging(HSAIL_LOGGING_ENABLE_ALL);
        }
      else
        {
          if (getenv("ROCM_GDB_ENABLE_LOG") != NULL)
            {
              rocm_printf_filtered("Internal logging is still enabled using the "
                                   "\"ROCM_GDB_ENABLE_LOG\" environment variable\n");
            }
          else
            {
              hsail_enqueue_set_logging(HSAIL_LOGGING_DISABLE_ALL);
            }
        }
    }
}

bool hsail_cmd_get_show_isa_option(void)
{
  return gs_isa_show_isa_enabled;
}

static void hsail_cmd_set_isa_dump(const char* ip_option)
{
  if (ip_option == NULL)
    {
      printf_filtered("Show ISA options\n");
      printf_filtered("set rocm show-isa [on|off] \n");
      return;
    }
  if(strcmp(ip_option,"on") == 0)
    {
      printf_filtered("ISA will be saved to temp_isa when debugging a dispatch\n");
      gs_isa_show_isa_enabled = true;
    }
  else if(strcmp(ip_option,"off") == 0)
    {
      printf_filtered("ISA saving has been disabled\n");
      gs_isa_show_isa_enabled = false;
    }
  else
    {
      printf_filtered("Show ISA options\n");
      printf_filtered("set rocm show-isa [on|off] \n");
    }

}

static void hsail_cmd_parse_set_config_command (char *args, int from_tty, struct cmd_list_element *c)
{
  /* Note that args is NULL always, the real args that we care for
   * this command are in g_hsail_argument_buffer
   * */
  char* pch = NULL;
  char* temp_hsail_argument_buff = NULL;
  struct ui_out *uiout = current_uiout;

  temp_hsail_argument_buff = (char*)xmalloc(gs_hsail_argument_buff_len*sizeof(char));
  gdb_assert(temp_hsail_argument_buff != NULL);

  memset(temp_hsail_argument_buff, '\0', gs_hsail_argument_buff_len);

  /*g_hsail_argument_buff is populated by the caller of this function*/
  strcpy(temp_hsail_argument_buff, g_hsail_argument_buff);


  pch = strtok (temp_hsail_argument_buff, " ");

  if (pch != NULL)
    {
      /*set rocm logging*/
      if (strcmp(pch,"logging") == 0)
        {
          pch = strtok(NULL, " ");
          hsail_cmd_set_internal_logging(pch);
        }
      /*set hsail trace*/
      else if (strcmp(pch, "trace") == 0)
        {
          pch = strtok(NULL, " ");
          hsail_trace_configure(pch);
        }
      else if (strcmp(pch, "show-isa") == 0)
        {
          pch = strtok(NULL, " ");
          hsail_cmd_set_isa_dump(pch);
        }
      else
        {
          ui_out_text(uiout,"Invalid parameter\n");
          hsail_cmd_set_options_help();
        }
    }
  else
    {
      ui_out_text(uiout,"Invalid parameter\n");
      hsail_cmd_set_options_help();
    }
  xfree(temp_hsail_argument_buff);
}

static void hsail_cmd_show_config_command (struct ui_file *file, int from_tty,
                                       struct cmd_list_element *c, const char *value)
{
  gdb_assert(NULL != value);
  gdb_assert(NULL != g_hsail_argument_buff);
  printf_filtered("Present state of ROCm configuration commands\n");
  if (hsail_cmd_get_show_isa_option() == true)
    printf_filtered("rocm show-isa: \t on \t GPU ISA will be saved to temp_isa\n");
  else
    printf_filtered("rocm show-isa: \t off\n");

  if (gs_internal_logging_enabled == true)
    printf_filtered("rocm logging: \t on \t Internal logging has been enabled\n");
  else
    printf_filtered("rocm logging: \t off\n");

  hsail_trace_print_configuration();
}


void hsail_command_clear_argument_buff(void)
{
  if(g_hsail_argument_buff != NULL)
    {
      xfree(g_hsail_argument_buff);
      g_hsail_argument_buff = NULL;
    }
}



/* Command for printing hsail source within gdb's terminal
 * */
void hsail_cmd_list_command(char* arg, int from_tty)
{
  printf_filtered("ROCm list command is not presently supported when debugging a GPU dispatch\n");
}


static void hsail_cmd_set_options_help(void)
{
  struct ui_out *uiout = current_uiout;

  ui_out_text(uiout,  _("Set ROCm configuration options.\n"HSAIL_SET_CMD_HELP()));
}

static void hsail_cmd_do_nothing(char *arg, int from_tty)
{
  struct ui_out *uiout = current_uiout;
  ui_out_text(uiout, _("ROCm specific features in ROCm-gdb.\n"HSAIL_GLOBAL_HELP()));
}

static void hsail_cmd_set_focus_command(char *arg, int from_tty)
{
  hsail_thread_set_focus_command(arg, from_tty);
}


void
_initialize_hsailcmd (void)
{
  const char *hsail_set_name = "rocm";
  struct cmd_list_element *c = NULL;

  /* info hsail
   *
   * When help info is printed only the "Display ROCm related information" is printed.
   * When you print help info hsail, the full string is printed.
   * */
  add_info ("rocm", hsail_info_command,
            _("Display information about GPU dispatches.\n"HSAIL_INFO_HELP_STRING()));

  /* the prefix for rocm-gdb commands, subcommands such as hsail thread will be added
   * to the hsailcmdlist */
  add_prefix_cmd ("rocm", no_class, hsail_cmd_do_nothing,
                  _("ROCm specific features in ROCm-gdb.\n"HSAIL_GLOBAL_HELP()),
                  &hsailcmdlist, "rocm ", 1, &cmdlist);

  /* You can add new command in the style below
   * add_hsail_cmd("newcommand", hsail_newcommand_command, _("HSAIL new command\n"));
   * */

  /* hsail thread  */
  add_hsail_cmd("thread", hsail_cmd_set_focus_command, _("ROCm switching work-item command.\n"HSAIL_THREAD_HELP()));


#if 0
  /* This the previous mechanism where we had defined a single top level command.
   * left over for reference
   * */
  add_com ("hsail", class_stack, hsail_command, _("HSAIL specific features in ROCm-gdb.\n"HSAIL_GLOBAL_HELP()));
  add_com_alias ("hl", "hsail", class_stack, 1);

  /* I am leaving this here to show another way of how boolean parameters can be passed*/
  int hsail_logging_param = 0; /*this declaration would need to be global*/
  /* set hsailbool on   -> result will be stored in hsail_logging_param
   * set hsailbool off  -> result will be stored in hsail_logging_param
   * */
  add_setshow_boolean_cmd("hsailbool",
                          class_run,
                           &hsail_logging_param,
                           _("Set ROCm configuration options."),
                           _("Show ROCm configuration options."),
                           _("Possible ROCm configuration options."),
                           hsail_set_config_command,
                           hsail_show_config_command,
                           &setlist, &showlist);
#endif

  /* set ROCm logging */
  /* Buffer freed in final hsail cleanup on gdb exit
   * */
  g_hsail_argument_buff = (char*)malloc(gs_hsail_argument_buff_len*sizeof(char));
  gdb_assert(g_hsail_argument_buff != NULL);

  memset(g_hsail_argument_buff, '\0', gs_hsail_argument_buff_len);

  add_setshow_string_noescape_cmd("rocm",
                                  class_run,
                                   &g_hsail_argument_buff,
                                   _("Set ROCm configuration options.\n"HSAIL_SET_CMD_HELP()),
                                   _(HSAIL_SHOW_CMD_HELP()),
                                   _(""),
                                   hsail_cmd_parse_set_config_command,
                                   hsail_cmd_show_config_command,
                                   &setlist, &showlist);

  c = lookup_cmd (&hsail_set_name, setlist, "", -1, 1);
  gdb_assert (c != NULL);

}
