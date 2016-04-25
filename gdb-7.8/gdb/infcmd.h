/* Memory-access and commands for "inferior" process, for GDB and HSAIL stepping

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


/* This function is called by the HSAIL event handler when the kill completion
 * notification is received
 * */
void kill_command_complete(void);
