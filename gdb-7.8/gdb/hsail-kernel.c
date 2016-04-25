/*
   ROCm GDB functions to print state of the HSAIL dispatches

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

#include "defs.h"
#include "format.h"
#include "gdb_assert.h"
#include "ui-out.h"
#include "utils.h"

#include <string.h>
#include <stdbool.h>
#include "CommunicationControl.h"

#include "hsail-breakpoint.h"
#include "hsail-kernel.h"
#include "hsail-utils.h"

static struct hsail_kernel* gs_hsail_kernel_chain = NULL;

/* active dispatch */
static struct hsail_dispatch* gs_active_dispatch = NULL;

#define ALL_HSAIL_KERNELS(k)  for (k = gs_hsail_kernel_chain; NULL != k; k = k->next)

/* mirror of add_to_breakpoint_chain*/
static void
add_to_hsail_kernel_chain (struct hsail_kernel* b)
{
  /* Add this kernel to the end of the chain*/
  struct hsail_kernel* b1 = gs_hsail_kernel_chain;

  if (b1 == NULL)
    gs_hsail_kernel_chain = b;
  else
    {
      while (b1->next)
        {
          b1 = b1->next;
        }
      b1->next = b;
    }
}

struct hsail_dispatch* hsail_kernel_active_dispatch(void)
{
  return gs_active_dispatch;
}

static bool
hsail_kernel_append_dispatch_to_kernel(struct hsail_kernel* k,
                                       struct hsail_dispatch* dispatch,
                                       HsailWaveDim3 workGroupSize,
                                       HsailWaveDim3 gridSize)
{
  bool dispatchFound = false;
  struct hsail_dispatch* pCurrentDispatch = NULL;
  struct hsail_dispatch* pLastDispatch = NULL;

  gdb_assert(k!= NULL);
  gdb_assert(dispatch != NULL);

  // mark active dispatch
  gs_active_dispatch = dispatch;

  // look in the current list of dispatched items
  // if same work group size is there and the dispatch count and release the dispatch
  // if not and the dispatch to the list

  if (k->dispatch_list != NULL)
  {
    pCurrentDispatch = k->dispatch_list;
  }

  // Check the current dispatch for the same size workgroup size
  while(pCurrentDispatch != NULL && !dispatchFound)
  {
    if (workGroupSize.x == pCurrentDispatch->work_groups_size.x &&
        workGroupSize.y == pCurrentDispatch->work_groups_size.y &&
        workGroupSize.z == pCurrentDispatch->work_groups_size.z)
    {
      dispatchFound = true;
    }
    else
    {
      // remember the last dispatch in the list so the new dispatch can be added after it
      if (pCurrentDispatch->next == NULL)
      {
        pLastDispatch = pCurrentDispatch;
      }
      pCurrentDispatch = pCurrentDispatch->next;
    }
  };

  // take the correct action based if it is a new dispatch or an old one
  if (dispatchFound)
  {
    if (pCurrentDispatch != NULL) // sanity check
    {
      pCurrentDispatch->dispatch_count++;
      xfree(dispatch);
    }
  }
  else
  {
    if (pLastDispatch != NULL)
    {
      pLastDispatch->next = dispatch;
    }
    else
    {
      k->dispatch_list = dispatch;
      k->dispatch_list->dispatch_count = 0;
    }
    dispatch->work_groups_size.x = workGroupSize.x;
    dispatch->work_groups_size.y = workGroupSize.y;
    dispatch->work_groups_size.z = workGroupSize.z;
    dispatch->work_items.x = gridSize.x;
    dispatch->work_items.y = gridSize.y;
    dispatch->work_items.z = gridSize.z;
    dispatch->dispatch_count++;
  }


  return true;
}

/* Updates the kernel_source_file_name and saves the kernel to the file
 * */
static void hsail_kernel_save_source_to_file(struct hsail_kernel* k)
{
  int filename_str_len = 0;
  char* hsail_source_buffer = NULL;
  FILE* temp_file_handle = NULL;

  const char hsail_ext[] = "temp_source";
  filename_str_len = strlen(hsail_ext) + 1;

  gdb_assert(NULL != k);
  free_current_contents(&k->kernel_source_file_name);

  k->kernel_source_file_name = (char*)xmalloc(sizeof(char)*filename_str_len);
  gdb_assert(NULL != k->kernel_source_file_name);

  memset(k->kernel_source_file_name, '\0', filename_str_len);

  strcpy(k->kernel_source_file_name, hsail_ext);


  /* Two other possible directions are below*/

  /* Case 1: Implementation where we generate BRIGModule_#Number#.hsail
   *
   * The better version for this would be to use an API in debug facilities that gets module name
   * from kernel name. That'd allow  gdb to be aware of "is this a new kernel"
   * or "is this a new module" and accordingly skip creating a text file.
   * The 64 below adds some space for a "index" and underscore.
  */

  /*
  const char hsail_prefix[] = "BrigModule";
  static int module_index = 0;
  filename_str_len = strlen(hsail_ext) + strlen(hsail_prefix) + 64;
  k->kernel_source_file_name = (char*)xmalloc(sizeof(char)*filename_str_len);
  memset(k->kernel_source_file_name, '\0',filename_str_len);

  snprintf(k->kernel_source_file_name,filename_str_len,
           "%s_%d%s",hsail_prefix, module_index, hsail_ext);
  module_index = module_index +1;
  */

  /* Case 2: Implementation where we generate kernel_#KernelName#.hsail
   *
   * The below logic can be used to save kernels based on the dispatched kernel name.
   * However, it is not great since the file we save is HSAIL text for a BRIG module
   * and each could have multiple kernels internally
   * */
  /*
  const char hsail_prefix[] = "Kernel_";
  gdb_assert(k!=NULL);
  gdb_assert(k->kernel_name != NULL);
  filename_str_len = strlen(k->kernel_name) + strlen(hsail_ext) + strlen(hsail_prefix);
  k->kernel_source_file_name = (char*)xmalloc(sizeof(char)*filename_str_len);

  memset(k->kernel_source_file_name, '\0',filename_str_len);

  strcpy(k->kernel_source_file_name, hsail_prefix);
  strcat(k->kernel_source_file_name, k->kernel_name);
  strcat(k->kernel_source_file_name, hsail_ext);
  */

  hsail_source_buffer = hsail_dbginfo_get_source_buffer();

  /* if no source available, dont mess with anything to do with the file, bail*/
  if (hsail_source_buffer == NULL)
    {
      return;
    }

  if (hsail_utils_check_file_exists(k->kernel_source_file_name))
    {
      if (!hsail_utils_set_file_permission(k->kernel_source_file_name, HSAIL_FILE_READ_WRITE))
        {
          printf_filtered("%s could not be made read-write\n",k->kernel_source_file_name);
        }
    }

  temp_file_handle = fopen(k->kernel_source_file_name,"wb");
  gdb_assert(temp_file_handle != NULL);

  fprintf(temp_file_handle, "%s",hsail_source_buffer);

  rewind(temp_file_handle);
  fclose(temp_file_handle);

  if (!hsail_utils_set_file_permission(k->kernel_source_file_name, HSAIL_FILE_READ_ONLY))
    {
      printf_filtered("%s could not be made read only\n",k->kernel_source_file_name);;
    }

  printf_filtered("GPU kernel saved to %s\n",k->kernel_source_file_name);

}

bool hsail_kernel_add_dispatch(const HsailNotificationPayload* fifo_data)
{
  struct hsail_kernel* k = NULL;
  struct hsail_dispatch* active_dispatch = NULL;

  bool dispatch_added = false;
  bool kernel_found = false;

  gdb_assert(NULL != fifo_data);

  ALL_HSAIL_KERNELS(k)
  {
    gdb_assert(NULL != k->kernel_name);
    if (strcmp(k->kernel_name,
               fifo_data->payload.BinaryNotification.m_KernelName) == 0)
      {
        gdb_assert(k->kernel_source_file_name != NULL);

        /* We dont print this since the source is saved on a per module basis
         * printf("GPU kernel in %s\n",k->kernel_source_file_name);
         * */

        kernel_found = true;

        active_dispatch = XNEW(struct hsail_dispatch);
        gdb_assert(active_dispatch != NULL);
        active_dispatch->next = NULL;

        dispatch_added = hsail_kernel_append_dispatch_to_kernel(k,
                                                                active_dispatch,
                                                                fifo_data->payload.BinaryNotification.m_packet.workgroup_size,
                                                                fifo_data->payload.BinaryNotification.m_packet.grid_size);
        gdb_assert(dispatch_added == true);
        break;
      }
  }

  if (kernel_found == false)
    {
      k = xmalloc(sizeof(struct hsail_kernel));
      gdb_assert(NULL != k);
      memset(k, 0, sizeof(struct hsail_kernel));

      k->next = NULL;
      add_to_hsail_kernel_chain(k);
      kernel_found = true;

      k->dispatch_list = NULL;

      k->kernel_name = xmalloc(sizeof(char)*AGENT_MAX_FUNC_NAME_LEN);
      gdb_assert(k->kernel_name != NULL);
      strcpy(k->kernel_name, fifo_data->payload.BinaryNotification.m_KernelName);

      hsail_kernel_save_source_to_file(k);

      active_dispatch = XNEW(struct hsail_dispatch);
      gdb_assert(active_dispatch != NULL);
      active_dispatch->next = NULL;

      dispatch_added = hsail_kernel_append_dispatch_to_kernel(k,
                                                              active_dispatch,
                                                              fifo_data->payload.BinaryNotification.m_packet.workgroup_size,
                                                              fifo_data->payload.BinaryNotification.m_packet.grid_size);

    }

  if (kernel_found && dispatch_added )
    {
      return true;
    }

  return false;
}


void hsail_kernel_clear_chain(void)
{
  struct hsail_kernel* current = NULL;
  struct hsail_dispatch* currentDispatch = NULL;
  struct hsail_dispatch* freeDispatch = NULL;

  if (gs_hsail_kernel_chain == NULL)
    {
      return;
    }
  current = gs_hsail_kernel_chain;

  while (current != NULL)
    {
      /*remove the head of the list*/
      gs_hsail_kernel_chain = current->next;

      /* free the dispatch list */
      currentDispatch = current->dispatch_list;
      while (currentDispatch != NULL)
      {
        freeDispatch = currentDispatch;
        currentDispatch = currentDispatch->next;
        xfree(freeDispatch);
      }

      /* Free the filename and the kernel name buffers too */
      if (current->kernel_source_file_name != NULL)
        {
          xfree(current->kernel_source_file_name);
          current->kernel_source_file_name = NULL;
        }
      if (current->kernel_name != NULL)
        {
          xfree(current->kernel_name);
          current->kernel_name = NULL;
        }

      /* free the element */
      xfree(current);

      /* next node to free*/
      current = gs_hsail_kernel_chain;
    }
}

static uint32_t hsail_kernel_compute_num_wg(const uint32_t work_items, const uint32_t work_group_size)
{
    uint32_t wi = (work_items == 0 ? 1 : work_items);
    uint32_t wg_size = (work_group_size == 0 ? 1 : work_group_size);
    /* In case wi and wg_size are invalid input, e.g, work_items == 15 but work_group_size == 64*/
    if (wi < wg_size)
    {
        printf_filtered("Warning: Input grid size (%d) is less than work-group size (%d). Grid size should at least be work-group size.\n", wi, wg_size);
    }

    /* Round up */
    return (wi / wg_size  + (wi % wg_size == 0 ? 0 : 1));
}

void hsail_kernel_print_info (struct ui_out* uiout, int from_tty)
{
  int index = 0;
  struct hsail_kernel* k = NULL;
  struct hsail_dispatch* currentDispatch = NULL;
  char index_buffer[10] = "";
  char count_buffer[10] = "";
  char wg_buffer[30] = "";
  char wg_dim_buffer[30] = "";

  gdb_assert(NULL != uiout);

  ui_out_text(uiout,"Kernels info\n");
  printf_filtered("%5s%30s%15s%18s%23s\n","Index","KernelName","DispatchCount","# of Work-groups","Work-group Dimensions");
  index = 0;
  ALL_HSAIL_KERNELS(k)
    {
      gdb_assert(k!=NULL);
      gdb_assert(k->dispatch_list != NULL);

      currentDispatch = k->dispatch_list;
      while (currentDispatch)
      {
        sprintf(index_buffer,"%s%d",currentDispatch == gs_active_dispatch ? "*" : "",index);
        sprintf(count_buffer,"%d",currentDispatch->dispatch_count);
        sprintf(wg_buffer,"%d,%d,%d",
                        hsail_kernel_compute_num_wg(currentDispatch->work_items.x, currentDispatch->work_groups_size.x),
                        hsail_kernel_compute_num_wg(currentDispatch->work_items.y, currentDispatch->work_groups_size.y),
                        hsail_kernel_compute_num_wg(currentDispatch->work_items.z, currentDispatch->work_groups_size.z));
        sprintf(wg_dim_buffer,"%d,%d,%d",
                        currentDispatch->work_groups_size.x,
                        currentDispatch->work_groups_size.y,
                        currentDispatch->work_groups_size.z);
        printf_filtered("%5s%30s%15s%18s%23s\n",index_buffer, k->kernel_name, count_buffer, wg_buffer, wg_dim_buffer);

        index = index+1;
        currentDispatch = currentDispatch->next;
      }
    }
}

void hsail_kernel_print_specific_info(char* arg, struct ui_out* uiout, int from_tty)
{
  struct hsail_kernel* current_kernel = gs_hsail_kernel_chain;
  struct hsail_dispatch* currentDispatch = NULL;
  int index_counter = 0;
  int arg_len = 0;
  int kernel_name_len = 0;
  char kernel_name[256] = "";

  char index_buffer[10] = "";
  char wg_buffer[30] = "";
  char wg_dim_buffer[30] = "";

  bool kernel_found = false;

  gdb_assert(NULL != arg);
  gdb_assert(NULL != uiout);
  arg_len = strlen(arg);

  if (NULL == current_kernel)
  {
    ui_out_text(uiout, "No kernels are executed\n");
    return;
  }

  /* from arg copy the kernel name (all chars until the end or until the first space character */
  while (kernel_name_len < arg_len && arg[kernel_name_len] != ' ' && arg[kernel_name_len] != '\n' && arg[kernel_name_len] != '\r' && arg[kernel_name_len] != '\t')
  {
    kernel_name[kernel_name_len] = arg[kernel_name_len];
    kernel_name_len++;
  }
  kernel_name[kernel_name_len] = '\0';

  /* pass through the chain */
  while (current_kernel != NULL)
  {
    gdb_assert(NULL != current_kernel->kernel_name);
    /* check if it is the kernel if the same name then print the information */
    if (strcmp(current_kernel->kernel_name, kernel_name) == 0)
    {
      kernel_found = true;
      /* print the information of the kernel */
      currentDispatch = current_kernel->dispatch_list;
      printf_filtered("Kernel %s info\n", current_kernel->kernel_name);
      printf_filtered("%5s%25s%25s\n","Index","# of Work-groups","Work-group Dimensions");
      index_counter = 0;
      while (currentDispatch)
      {
        sprintf(index_buffer,"%s%d",currentDispatch == gs_active_dispatch ? "*" : "",index_counter);
        sprintf(wg_buffer,"%d,%d,%d",
                        hsail_kernel_compute_num_wg(currentDispatch->work_items.x, currentDispatch->work_groups_size.x),
                        hsail_kernel_compute_num_wg(currentDispatch->work_items.y, currentDispatch->work_groups_size.y),
                        hsail_kernel_compute_num_wg(currentDispatch->work_items.z, currentDispatch->work_groups_size.z));
        sprintf(wg_dim_buffer,"%d,%d,%d",
                        currentDispatch->work_groups_size.x,
                        currentDispatch->work_groups_size.y,
                        currentDispatch->work_groups_size.z);
        printf_filtered("%5s%25s%25s\n",index_buffer, wg_buffer, wg_dim_buffer);
        index_counter = index_counter+1;
        currentDispatch = currentDispatch->next;
      }
    }
    current_kernel = current_kernel->next;
  }

  if (!kernel_found)
  {
    printf_filtered("'%s' kernel not found. Please enter a kernel from executed kernels.\n",kernel_name);
    printf_filtered("To list the executed GPU kernels use 'info rocm kernels'.\n");
  }
}
