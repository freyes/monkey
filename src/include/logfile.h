/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2001-2003, Eduardo Silva P.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* logfile.c */

struct log_info {
	char *ip;
	char *datetime;
	int   final_response; /* Ok: 200, Not Found 400, etc... */
	int   size;
	int   status; /* on/off : 301. */
	char  *error_msg;
};

char    *PutTime();
char    *PutIP();
char    *BaseName(char *name);
int	 log_main(struct request *sr);
int	 add_log_pid();
int	 remove_log_pid();
