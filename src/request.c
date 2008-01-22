/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2001-2007, Eduardo Silva P.
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

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netdb.h>
#include <sys/wait.h>
#include <signal.h>

#include <arpa/inet.h> 
#include <netinet/in.h>
#include <sys/types.h>

#include "monkey.h"

#define MAX_TIMES 10000
#define POST_TIMEOUT 10

struct request *parse_client_request(struct client_request *cr, char *buf)
{
    int i, init_block=0, n_blocks=0, offset=0;
    int length_buf, length_string_end;
    int pipelining=FALSE;
    const char *normal_string_end = "\r\n\r\n";
    const char *old_string_end = "\n\n";
    char *string_end=0, *check_normal_string=0, *check_old_string=0;
    char *block=0;
    struct request *cr_buf=0, *cr_search=0;

    check_normal_string = strstr(buf, normal_string_end);
    check_old_string = strstr(buf, old_string_end);
    if(check_normal_string)
    {
        if(check_old_string)
        {
            return FALSE;
        }
        else
        {
            string_end = (char *)normal_string_end;
        }
    }
    else if(check_old_string)
    {
            string_end = (char *)old_string_end;
    }

    /* DEBUG 
    if(strcmp(string_end, "\r\n\r\n")==0)
    {
        printf("\n*** STRING_END: RNRN");
        fflush(stdout);
    }
    */

    cr->body = buf;

    length_buf = strlen(buf);
    length_string_end = strlen(string_end);

    for(i=0; i< length_buf ; i++)
    {
        if(strncmp(buf+i, string_end, length_string_end)==0)
        {
            if(n_blocks==0){
                offset = 0;
            }
            else
            {
                offset = length_string_end;
            }

            /* Allocating request block */
            block = m_copy_string(buf, init_block+offset, i);

            cr_buf = M_malloc(sizeof(struct request));
            cr_buf->body = block;
            cr_buf->next = NULL;

            if(!cr->request)
            {
                cr->request = cr_buf;
            }
            else{
                cr_search = cr->request;
                while(cr_search)
                {
                    if(cr_search->next==NULL)
                    {
                        cr_search->next = cr_buf;
                        break;
                    }
                    else
                    {
                        cr_search = cr_search->next;
                    }
                }
            }

            init_block = i;
            n_blocks++;
            //printf("\n**** BLOCK ****:\n%s\n*** END BLOCK ***\n", block);
            //fflush(stdout);
        }
    }


    /* Checking pipelining connection */
    if(n_blocks>1)
    {
        pipelining = TRUE;

        while(cr_search){
            if(Get_method_from_request(cr_search->body)!=GET_METHOD)
                pipelining = FALSE;

            //printf("---BLOCK---:\n%s\n---END BLOCK---\n\n", cr_search->request);
            //fflush(stdout);
            cr_search = cr_search->next;
        }
    }

    printf("\n\n*** BLOCKS FOUND: %i", n_blocks);
    if(pipelining){
        printf("\nPIPELINING: TRUE\n");
        cr_search = cr->request;
        while(cr_search){
            printf("---BLOCK---:\n%s\n---END BLOCK---\n\n", cr_search->body);
            fflush(stdout);
            cr_search = cr_search->next;
        }
    }
    else{
        printf("\n\nPIPELINING: FALSE");
    }
    fflush(stdout);

    /* DEBUG 
    printf("\n\n*** BLOCKS FOUND: %i\n\n", n_blocks);
    cr_search = cr;
    while(cr_search){
        printf("---BLOCK---:\n%s\n---END BLOCK---\n\n", cr_search->request);
        fflush(stdout);
        cr_search = cr_search->next;
    }

    fflush(stdout);
    */
    return cr->request;
}

int Request_Main(struct client_request *cr)
{
	int num_bytes=0, recv_timeout, times=0, status=0, limit_time;
    int process_request = TRUE, length_remote_request;
    char *request_end = NULL;
	static char remote_request[MAX_REQUEST_BODY];
	struct vhost *vhost;
    struct request *s_request;

	s_request = cr->request;

	if(cr->counter_connections>0)
		recv_timeout=config->keep_alive_timeout; 
	else
		recv_timeout=config->timeout;

	memset(remote_request, '\0', sizeof(remote_request));
	limit_time = (int) time(NULL) + recv_timeout;

    printf("\n******************** NEW REQUEST *******************\n");
    fflush(stdout);

	/* Getting Request */
	do {		
		times++;

		num_bytes=Socket_Timeout(cr->socket, remote_request+strlen(remote_request), \
								MAX_REQUEST_BODY - strlen(remote_request) - 1, recv_timeout, ST_RECV);

        printf("\nNBYTES: %i\n%s", num_bytes, remote_request);
        fflush(stdout);

		if((int) time(NULL) >= limit_time) {
			if(cr->counter_connections>0){
				return 2; /* Exit: timeout of persistent connection */
			}
			else{
                printf("\nCONNECTION: GOT TIMEOUT");
                fflush(stdout);
				
                Request_Error(M_CLIENT_REQUEST_TIMEOUT, cr, 
                                                s_request, 1, s_request->log);
				return -1;
			}
		}

		/* Timeout ? */
		if(num_bytes==-2){
            printf("\nCONNECTION: GOT TIMEOUT");
            fflush(stdout);
			if(cr->counter_connections > 0){ /* persistent connection ? */
				return EXIT_PCONNECTION;    /* Exit : timeout of persistent connection */
			}
			else{
				if(recv_timeout==config->timeout){
					Request_Error(M_CLIENT_REQUEST_TIMEOUT, cr, 
                                                s_request, 1,s_request->log);
					return EXIT_NORMAL; /* Exit: Normal timeout */
				}
				else
					continue;
			}
		}
		/* Exit: Persistent connection */
		if(num_bytes<=0){
			return EXIT_NORMAL;
		}

		if(times==1) {
			 /* Validating format of first header */
			int i, count=0;
			for(i=0; i<strlen(remote_request) && count<2; i++){
				if(remote_request[i]==' ') count++;
			}
			if(count<2){
				Request_Error(M_CLIENT_BAD_REQUEST, cr, 
                                                    s_request,1,s_request->log);
				return EXIT_NORMAL;
			}
			if(Get_method_from_request(remote_request)==POST_METHOD)
				recv_timeout=POST_TIMEOUT;
		}
        printf("\nRECV/times: %i", times);
        fflush(stdout);

        request_end = get_end_position(remote_request);
        if(!request_end){
            printf("\nREQUEST END IS NULL");
            fflush(stdout);
        }

        if(request_end && times<RECV_MAX_TIMES)
        {
            process_request = FALSE;
            /* We need to detect if we have a Pipelined connection:
            A pipelined connection means that more than 1 request are sent by 
            the  client in the same TCP connection without wait for a response 
            to every request sent until the server listen all requests and
            when the last one arrives, it must send responses in the same order.
            A keepalive connection is necessary to support this feature and a 
            pipelining connection always comes in the second keepalive request
            and all them use the GET method.
            */
            length_remote_request = strlen(remote_request);

            if(strcmp(remote_request+(length_remote_request-4), "\r\n\r\n")==0)
            {
                DEBUG("GOING TO PARSE CLIENT REQUEST");
                parse_client_request(cr, remote_request);
                //printf("\n--- PIPELINING CONNECTION ---:\n%s--- END --- ", remote_request);
                fflush(stdout);
                break;
            }
            else if(remote_request+(length_remote_request-2) == "\n\n")
            {
                printf("\nNO: %s", remote_request+(length_remote_request-4));
                fflush(stdout);
            }
        }
	} while(process_request);

	s_request = (struct request *) Request_Strip_Header(s_request, remote_request);

	s_request->user_home=VAR_OFF;
	s_request->temp_path = m_build_buffer(config->server_root);
	cr->counter_connections++;

	/* Empty Host (HTTP/1.0) */
	if(!s_request->host){
		s_request->host=m_build_buffer("%s", config->servername);
	}

	/* Server Signature */
	if(config->hideversion==VAR_OFF){
		s_request->server_signature = m_build_buffer("Monkey/%s Server (Host: %s, Port: %i)", 
			VERSION, s_request->host, config->serverport);
	}
	else{
		s_request->server_signature = m_build_buffer("Monkey Server (Host: %s, Port: %i)", 
			s_request->host, config->serverport);
	}

	/* Valid request URI? */
	if(s_request->uri_processed==NULL){
		Request_Error(M_CLIENT_BAD_REQUEST, cr, s_request, 1, s_request->log);
		return EXIT_NORMAL;
	}	
	
	/*  URL it's Allowed ? */
	if(Deny_Check(s_request->uri_processed)==-1 || Deny_Check(s_request->query_string)) {
		s_request->log->final_response=M_CLIENT_FORBIDDEN;
		Request_Error(M_CLIENT_FORBIDDEN, cr, s_request,1,s_request->log);
		return EXIT_NORMAL;
	}
	
	/* HTTP/1.1 needs Host header */
	if(!s_request->host && s_request->protocol==HTTP_11){
		s_request->log->final_response=M_CLIENT_BAD_REQUEST;
		Request_Error(M_CLIENT_BAD_REQUEST, cr, s_request,1,s_request->log);
		return EXIT_NORMAL;
	}
	
	/* Setting info: SERVER_SCRIPTALIAS */
	if(config->server_scriptalias!=NULL){
		s_request->scriptalias[0] = config->server_scriptalias[0];
		s_request->scriptalias[1] = config->server_scriptalias[1];
		s_request->scriptalias[2]='\0';
	}

	/* Method not allowed ? */
	if(s_request->method==METHOD_NOT_ALLOWED){
		s_request->log->final_response=M_CLIENT_METHOD_NOT_ALLOWED;
		Request_Error(M_CLIENT_METHOD_NOT_ALLOWED, cr, s_request,1,s_request->log);
		return EXIT_NORMAL;
	}

	/* Validating protocol version */
	if(!strstr(PROTOCOLS, get_name_protocol(s_request->protocol))) {
		s_request->log->final_response=M_SERVER_HTTP_VERSION_UNSUP;
		Request_Error(M_SERVER_HTTP_VERSION_UNSUP, cr, s_request,1,s_request->log);
		return EXIT_NORMAL;
	}
	
	/* It's a request for a special VirtualHost ? */
	if((vhost=VHOST_Find(s_request->host))!=NULL) {
		if(s_request->temp_path)
			M_free(s_request->temp_path);
			
		s_request->temp_path = m_build_buffer(vhost->documentroot);

		s_request->scriptalias[0]=vhost->cgi_alias;
		s_request->scriptalias[1]=vhost->cgi_path;
		s_request->scriptalias[2]='\0';

		/* If wasn't defined a forcegetdir var on Vhost configuration
		 we assume default server config to getdir var */
		if(vhost->forcegetdir != VAR_NOTSET){
			s_request->getdir = vhost->forcegetdir;
		}
	}

	/* CGI Request ? */
	if(config->server_scriptalias!=NULL){
		int len=0;
		
		len = strlen(config->server_scriptalias[0]);
		if((strncmp(config->server_scriptalias[0], s_request->uri_processed, len))==0){
			int cgi_status;
			cgi_status=M_CGI_main(cr, s_request, s_request->log, remote_request);
			/* Codes:
				-1 : Fallo de permisos
				-2 : Timeout
				-3 : Internal Server Error
			*/
			if(cgi_status==M_CGI_TIMEOUT || cgi_status==M_CGI_INTERNAL_SERVER_ERR){
				Request_Error(s_request->log->final_response, cr, s_request, 1, s_request->log);	
			}
			return cgi_status;
		}
	}

	/* is requesting an user home directory ? */
	if(strncmp(s_request->uri_processed, USER_HOME_STRING, 
        strlen(USER_HOME_STRING))==0 && config->user_dir){
		if(User_main(cr, s_request)!=0)
			return EXIT_NORMAL;
	}

	/* Verificacion del Metodo */
	if(s_request->method==GET_METHOD || s_request->method==HEAD_METHOD){
			status=M_METHOD_Get_and_Head(cr, s_request, cr->socket);
	}
	else {
		if(s_request->method==POST_METHOD){
			if((status=M_METHOD_Post(cr, s_request, remote_request))==-1){
				M_free(s_request->post_variables);
				return status;
			}
			status=M_METHOD_Get_and_Head(cr, s_request, cr->socket);
		}
	}

	return status;
}

/* Returns method of request:
we use this function to know type of method before
request done */
int Get_method_from_request(char *request)
{
	int int_method, pos = 0, max_length_method = 5;
	char *str_method;
	
	pos = str_search(request, " ",1);
	if(pos<=2 || pos>=max_length_method){
		return -1;	
	}
	
	str_method = M_malloc(max_length_method);
	strncpy(str_method, request, pos);
	str_method[pos]='\0';

	int_method = M_METHOD_get_number(str_method);
	M_free(str_method);
	
	return int_method;
}

/* Return a struct with method, URI , protocol version 
and all static headers defined here sent in request */
struct request *Request_Strip_Header(struct request *sr, char *request_body)
{
	int uri_init=0, uri_end=0, 
		query_init=0, query_end=0,
		prot_init=0, prot_end=0;
	
	/* Method */
	sr->method = Get_method_from_request(request_body);

	/* Request URI */
	uri_init = str_search(request_body, " ",1) + 1;
	uri_end = str_search(request_body+uri_init, " ",1) + uri_init;
	
	/* Query String */
    query_init = str_search(request_body+uri_init, "?", 1);
	if(query_init > 0 && query_init <= uri_end){
        query_init+=uri_init+1;
        query_end = uri_end;
        uri_end = query_init - 1;
        sr->query_string = m_copy_string(request_body, query_init, query_end);
	}
	
	/* Request URI Part 2 */
	sr->uri = (char *) m_copy_string(request_body, uri_init, uri_end);

	/* HTTP Version */
	prot_init=str_search(request_body+uri_init+1," ",1)+uri_init+2;

	if(str_search(request_body, "\r\n",2)>0){
		prot_end = str_search(request_body, "\r\n",2);
	}
	else{
		prot_end = str_search(request_body, "\n",1);
	}
	
	if(prot_end!=prot_init && prot_end>0){
		char *str_prot=0;
	
		str_prot = m_copy_string(request_body, prot_init, prot_end);
		sr->protocol=get_version_protocol(str_prot);
		M_free(str_prot);
	}
		
	/* URI processed */
	sr->uri_processed = get_real_string( sr->uri );
		
	/* Host */
	if((strstr2(request_body, RH_HOST))!=NULL){
		char *tmp = Request_Find_Variable(request_body, RH_HOST);
		
		/* is host formated something like xxxxx:yy ???? */
		if(tmp!=NULL && strstr(tmp, ":") != NULL ){
			int pos_sep=0;
			char *port=0;

			pos_sep = str_search(tmp, ":",1);
			sr->host = m_copy_string(tmp, 0, pos_sep);
			port = m_copy_string(tmp, pos_sep, strlen(tmp));
			sr->port=atoi(port);
			M_free(port);
			M_free(tmp);
		}
		else{
			sr->host=tmp; /* maybe null */
			sr->port=config->standard_port;
		}
	}
	else{
		sr->host=NULL;
	}
		
	/* Variables generales del header remoto */
	sr->keep_alive=VAR_OFF;
	if((strstr2(request_body, RH_CONNECTION))!=NULL && sr->protocol==HTTP_11){
		sr->connection = Request_Find_Variable(request_body, RH_CONNECTION);
		if((strstr2(sr->connection,"Keep-Alive"))!=NULL){
			sr->keep_alive=VAR_ON;
		}
	}
	
	sr->accept			= Request_Find_Variable(request_body, RH_ACCEPT);
	sr->accept_charset	= Request_Find_Variable(request_body, RH_ACCEPT_CHARSET);
	sr->accept_encoding   = Request_Find_Variable(request_body, RH_ACCEPT_ENCODING);
	sr->accept_language   = Request_Find_Variable(request_body, RH_ACCEPT_LANGUAGE);
	sr->cookies		   = Request_Find_Variable(request_body, RH_COOKIE);
	sr->referer		   = Request_Find_Variable(request_body, RH_REFERER);
	sr->user_agent		= Request_Find_Variable(request_body, RH_USER_AGENT);
	sr->range			 = Request_Find_Variable(request_body, RH_RANGE);
	sr->if_modified_since = Request_Find_Variable(request_body, RH_IF_MODIFIED_SINCE);
	
	return (struct request *) sr;
}

/* Return value of some variable sent in request */
char *Request_Find_Variable(char *request_body,  char *string)
{
	int pos_init_var=0, pos_end_var=0;
	char *var_value=0;
	
	/* looking for string on request_body ??? */	
	if (strstr2(request_body, string) == NULL)
		return NULL;

	pos_init_var = str_search(request_body, string, strlen(string));
	pos_end_var = str_search(request_body+pos_init_var, "\n", 1) - 1;
	
	if(pos_init_var<=0 || pos_end_var<=0){
		return  NULL;	
	}

	pos_init_var += strlen(string) + 1;
	pos_end_var = (unsigned int) (pos_init_var  + pos_end_var) - (strlen(string) +1);

	var_value = m_copy_string(request_body, pos_init_var, pos_end_var);
	
	return (char *) var_value;
}

/* Look for some  index.xxx in pathfile */
char *FindIndex(char *pathfile)
{
	char *file_aux=0;
	struct indexfile *aux_index;
	
	aux_index=first_index;

	while(aux_index!=NULL) {
		if(pathfile[strlen(pathfile)-1]=='/')
			file_aux=m_build_buffer("%s/%s",pathfile,aux_index->indexname);
		else
			file_aux=m_build_buffer("%s%s",pathfile,aux_index->indexname);
			
		if(access(file_aux,F_OK)==0) {
			M_free(file_aux);
			return (char *) aux_index->indexname;
		}
		M_free(file_aux);
		aux_index=aux_index->next;
	}

	return NULL;
}

/* Send error responses */
void Request_Error(int num_error, struct client_request *cr, 
                   struct request *s_request, int debug, struct log_info *s_log)
{
	char *page_default=0, *aux_message=0;
			
	if(!s_log) {
		s_log=M_malloc(sizeof(struct log_info));
	}
		
	switch(num_error) {
		case M_CLIENT_BAD_REQUEST:
			page_default=Set_Page_Default("Bad Request", "", s_request->server_signature);
			s_log->error_msg=m_build_buffer("[error 400] Bad request");
			break;

		case M_CLIENT_FORBIDDEN:
			page_default=Set_Page_Default("Forbidden", s_request->uri, s_request->server_signature);
			s_log->error_msg=m_build_buffer("[error 403] Forbidden %s",s_request->uri);
			break;

		case M_CLIENT_NOT_FOUND:
			aux_message = m_build_buffer("The requested URL %.100s  was not found on this server.", (char *) s_request->uri);
			page_default=Set_Page_Default("Not Found", aux_message, s_request->server_signature);
			s_log->error_msg=m_build_buffer("[error 404] Not Found %s",s_request->uri);
			break;

		case M_CLIENT_METHOD_NOT_ALLOWED:
			page_default=Set_Page_Default("Method Not Allowed",s_request->uri, s_request->server_signature);
			s_log->final_response=M_CLIENT_METHOD_NOT_ALLOWED;
			s_log->error_msg=m_build_buffer("[error 405] Method Not Allowed %s", M_METHOD_get_name(s_request->method));
			break;

		case M_CLIENT_REQUEST_TIMEOUT:
			s_log->status=S_LOG_OFF;
			s_log->error_msg=m_build_buffer("[error 408] Request Timeout");
			break;

		case M_CLIENT_LENGHT_REQUIRED:
			s_log->error_msg=m_build_buffer("[error 411] Length Required");
			break;
			
		case M_SERVER_INTERNAL_ERROR:
			aux_message = m_build_buffer("Problems found running %s ",s_request->uri);
			page_default=Set_Page_Default("Internal Server Error",aux_message, s_request->server_signature);
			s_log->error_msg=m_build_buffer("[error 411] Internal Server Error %s",s_request->uri);
			break;
			
		case M_SERVER_HTTP_VERSION_UNSUP:
			page_default=Set_Page_Default("HTTP Version Not Supported"," ", s_request->server_signature);
			s_log->error_msg=m_build_buffer("[error 505] HTTP Version Not Supported");
			break;
	}

	s_log->final_response=num_error;
	
	s_request->headers->status = num_error;
	s_request->headers->content_length = 0;
	s_request->headers->location = NULL ;
	s_request->headers->cgi = SH_NOCGI;
	s_request->headers->pconnections_left = 0;
	s_request->headers->last_modified = NULL;
	
	if(aux_message) M_free(aux_message);
	
	if(!page_default)
		s_request->headers->content_type = NULL;
	else
		s_request->headers->content_type = m_build_buffer("text/html");

	M_METHOD_send_headers(cr->socket, s_request->headers, s_log);

	if(debug==1){
		fdprintf(cr->socket, NO_CHUNKED, "%s", page_default);
		M_free(page_default);
	}
}

/* Build error page */
char *Set_Page_Default(char *title, char *message, char *signature)
{
	char *page=0;

	page = m_build_buffer("<HTML><BODY><H1>%s</H1>%s<BR><HR> \
		<ADDRESS>%s</ADDRESS></BODY></HTML>", title, message, signature);
	return (char *) page;
}

/* Set Timeout for send() and recv() */
int Socket_Timeout(int s, char *buf, int len, int timeout, int recv_send)
{
	fd_set fds;
	time_t init_time, max_time;
	int n=0, status;
	struct timeval tv;

	init_time=time(NULL);
	max_time = init_time + timeout;

	FD_ZERO(&fds);
	FD_SET(s,&fds);
	
	tv.tv_sec=timeout;
	tv.tv_usec=0;

	if(recv_send==ST_RECV)
		n=select(s+1,&fds,NULL,NULL,&tv);  // recv 
	else{
		n=select(s+1,NULL,&fds,NULL,&tv);  // send 
	}

	switch(n){
		case 0:
				return -2;
				break;
		case -1:
				pthread_kill(pthread_self(), SIGPIPE);
				return -1;
	}
	
	if(recv_send==ST_RECV){
		status=recv(s,buf,len, 0);
	}
	else{
		status=send(s,buf,len, 0);
	}

	if( status < 0 ){
		if(time(NULL) >= max_time){
			pthread_kill(pthread_self(), SIGPIPE);
		}
	}
	
	return status;
}

/* Create a memory allocation in order to handle the request data */
struct request *alloc_request()
{
    struct request *request=0;

    request = (struct request *) M_malloc(sizeof(struct request));
    request->log= (struct log_info *) M_malloc(sizeof(struct log_info));
    //request->log->ip=PutIP(remote);

    request->headers = (struct header_values *) M_malloc(sizeof(struct header_values));
    request->headers->content_type = NULL;
    request->headers->last_modified = NULL;
    request->headers->location = NULL;
        
    request->status=VAR_OFF; /* Request not processed yet */
    request->make_log=VAR_ON; /* build log file of this request ? */
    request->query_string=NULL;

    request->log->datetime=PutTime();
    request->log->final_response=M_HTTP_OK;
    request->log->status=S_LOG_ON;
    request->status=VAR_ON;
    request->method=METHOD_NOT_FOUND;
    request->getdir = config->getdir;

    request->headers->range_values[0]=-1;
    request->headers->range_values[1]=-1;

    return (struct request *) request;
}