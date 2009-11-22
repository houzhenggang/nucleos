/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */

#include "inc.h"
#include <assert.h>
#include <nucleos/dmap.h>
#include <nucleos/endpoint.h>

#include <nucleos/vfsif.h>
#include "fs.h"
#include "buf.h"
#include "inode.h"
#include "drivers.h"


/* Declare some local functions. */
static void init_server(void);
static void get_work(message *m_in);
static void cch_check(void);

/*===========================================================================*
 *				main                                         *
 *===========================================================================*/
int main(int argc, char *argv[])
{
/* This is the main routine of this service. The main loop consists of 
 * three major activities: getting new work, processing the work, and
 * sending the reply. The loop never terminates, unless a panic occurs.
 */
  int error, ind;
  message m;

  /* Initialize the server, then go to work. */
  init_server();	

  fs_m_in.m_type = KCNR_FS_READY;

  if (kipc_send(FS_PROC_NR, &fs_m_in) != 0) {
      printf("MFS(%d): Error sending login to VFS\n", SELF_E);
      return -1;
  }

  while(!unmountdone || !exitsignaled) {
      endpoint_t src;
      /* Wait for request message. */
      get_work(&fs_m_in);
      src = fs_m_in.m_source;
      error = 0;

      caller_uid = -1;	/* To trap errors */
      caller_gid = -1;

      /* Exit request? */
      if(src == PM_PROC_NR) {
	exitsignaled = 1;
	fs_sync();
	continue;
      }

      /* This must be a regular VFS request. */
      assert(src == VFS_PROC_NR && !unmountdone);

      req_nr = fs_m_in.m_type;

      if (req_nr < VFS_BASE)
      {
      	fs_m_in.m_type += VFS_BASE;
      	req_nr = fs_m_in.m_type;
      }
      ind= req_nr-VFS_BASE;

      if (ind < 0 || ind >= NREQS) {
	  printf("mfs: bad request %d\n", req_nr); 
	  printf("ind = %d\n", ind);
          error = -EINVAL; 
      }
      else {
          error = (*fs_call_vec[ind])();
	  /*cch_check();*/
      }

      fs_m_out.m_type = error; 
      reply(src, &fs_m_out);

      if (error == 0 && rdahed_inode != NIL_INODE) {
          read_ahead(); /* do block read ahead */
      }
  }
}

/*===========================================================================*
 *				init_server                                  *
 *===========================================================================*/
static void init_server(void)
{
   int i;

   /* Init inode table */
   for (i = 0; i < NR_INODES; ++i) {
	   inode[i].i_count = 0;
	   cch[i] = 0;
   }
	
   init_inode_cache();

   /* Init driver mapping */
   for (i = 0; i < NR_DEVICES; ++i) 
       driver_endpoints[i].driver_e = NONE;
	
   SELF_E = getprocnr();
   buf_pool();
   fs_block_size = _MIN_BLOCK_SIZE;
}

/*===========================================================================*
 *				get_work				     *
 *===========================================================================*/
static void get_work(m_in)
message *m_in;				/* pointer to message */
{
    int srcok = 0;
    endpoint_t src;
    do {
    	int s;				/* receive status */
	if ((s = kipc_receive(ANY, m_in)) != 0) 	/* wait for message */
		panic("MFS","receive failed", s);
	src = fs_m_in.m_source;

	if (src != FS_PROC_NR) {
		if(src == PM_PROC_NR) {
			if(is_notify(fs_m_in.m_type))
				srcok = 1; /* Normal exit request. */
		    	else
				printf("MFS: unexpected message from PM\n");
		} else
			printf("MFS: unexpected source %d\n", src);
	} else if(src == FS_PROC_NR) {
		if(unmountdone) {
			printf("MFS: unmounted: unexpected message from FS_PROC_NR\n");
		} else {
			/* Normal FS_PROC_NR request. */
			srcok = 1;
		}
	} else
		printf("MFS: unexpected source %d\n", src);
   } while(!srcok);

   assert((src == FS_PROC_NR && !unmountdone) || 
	(src == PM_PROC_NR && is_notify(fs_m_in.m_type)));
}


/*===========================================================================*
 *				reply					     *
 *===========================================================================*/
void reply(who, m_out)
int who;	
message *m_out;                       	/* report result */
{
    if (kipc_send(who, m_out) != 0)    /* send the message */
        printf("MFS(%d) was unable to send reply\n", SELF_E);
}

static void cch_check(void) 
{
  int i;

  for (i = 0; i < NR_INODES; ++i) {
	  if (inode[i].i_count != cch[i] &&
		req_nr != REQ_GETNODE &&
		req_nr != REQ_PUTNODE &&
		req_nr != REQ_CLONE_OPCL && req_nr != REQ_READSUPER_S &&
		req_nr != REQ_MOUNTPOINT_S && req_nr != REQ_UNMOUNT &&
		req_nr != REQ_PIPE && req_nr != REQ_SYNC && 
		req_nr != REQ_LOOKUP_S)
printf("MFS(%d) inode(%d) cc: %d req_nr: %d\n",
	SELF_E, inode[i].i_num, inode[i].i_count - cch[i], req_nr);
	  
	  cch[i] = inode[i].i_count;
  }
}



