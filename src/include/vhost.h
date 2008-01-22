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

/* vhost.c */
struct vhost {
	char *servername;
	char *documentroot;
	char *cgi_alias;
	char *cgi_path;
	int	forcegetdir;
	struct vhost *next;
} *first_vhost;

void	VHOST_Read_Config(char *path_conf, char *file_conf);
void	VHOST_Config_Error(char *path);

struct vhost	*VHOST_Find(char *host);
int	 VHOST_Config_Add(char *vhost_servername, char *vhost_documentroot, \
	char *vhost_cgi_alias, char *vhost_cgi_path, int forcegetdir);
