/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file contains the procedures for creating, opening, closing, and
 * seeking on files.
 *
 * The entry points into this file are
 *   do_creat:	perform the CREAT system call
 *   do_open:	perform the OPEN system call
 *   do_mknod:	perform the MKNOD system call
 *   do_mkdir:	perform the MKDIR system call
 *   do_close:	perform the CLOSE system call
 *   do_lseek:  perform the LSEEK system call
 */

#include "fs.h"
#include <nucleos/stat.h>
#include <nucleos/fcntl.h>
#include <nucleos/string.h>
#include <nucleos/unistd.h>
#include <nucleos/com.h>
#include <nucleos/u64.h>
#include <nucleos/time.h>
#include <nucleos/types.h>
#include "file.h"
#include <servers/vfs/fproc.h>
#include "lock.h"
#include "param.h"
#include <nucleos/dirent.h>
#include <assert.h>
#include <nucleos/vfsif.h>

#include "vnode.h"
#include "vmnt.h"
#define result_addr	m2_p1
static char mode_map[] = {R_BIT, W_BIT, R_BIT|W_BIT, 0};
 
static int common_open(int oflags, mode_t omode);
static struct vnode *new_node(mode_t bits);
static int pipe_open(struct vnode *vp,mode_t bits,int oflags);


/*===========================================================================*
 *				do_creat				     *
 *===========================================================================*/
int do_creat()
{
/* Perform the creat(name, mode) system call. */
  int r;

  if (fetch_name(user_fullpath, PATH_MAX, m_in.name) < 0) return(err_code);
  r = common_open(O_WRONLY | O_CREAT | O_TRUNC, (mode_t) m_in.mode);
  return(r);
}

/*===========================================================================*
 *				scall_open				     *
 *===========================================================================*/
int scall_open()
{
	/* Perform the open(name, flags,...) system call. */
	int r;

	r = fetch_name(user_fullpath, PATH_MAX, m_in.m1_p1);

	if (r < 0) {
		return(err_code); /* name was bad */
	}

	r = common_open(m_in.m1_i2, m_in.m1_i3);

	return r;
}

/*===========================================================================*
 *				common_open				     *
 *===========================================================================*/
static int common_open(register int oflags, mode_t omode)
{
/* Common code from do_creat and do_open. */
  int b, r, exist = TRUE;
  dev_t dev;
  mode_t bits;
  struct filp *fil_ptr, *filp2;
  struct vnode *vp;
  struct vmnt *vmp;
  struct dmap *dp;

  /* Remap the bottom two bits of oflags. */
  bits = (mode_t) mode_map[oflags & O_ACCMODE];
  if (!bits) return(-EINVAL);

  /* See if file descriptor and filp slots are available. */
  if ((r = get_fd(0, bits, &m_in.fd, &fil_ptr)) != 0) return(r);

  /* If O_CREATE is set, try to make the file. */
  if (oflags & O_CREAT) {
        omode = I_REGULAR | (omode & ALL_MODES & fp->fp_umask);
	vp = new_node(omode);
	r = err_code;
	if (r == 0) exist = FALSE; /* We just created the file */
	else if (r != -EEXIST) return(r);  /* other error */ 
	else exist = !(oflags & O_EXCL);  /* file exists, if the O_EXCL
					     flag is set this is an error */
  } else {
	/* Scan path name */
	if ((vp = eat_path(PATH_NOFLAGS)) == NIL_VNODE) return(err_code);
  }

  /* Claim the file descriptor and filp slot and fill them in. */
  fp->fp_filp[m_in.fd] = fil_ptr;
  FD_SET(m_in.fd, &fp->fp_filp_inuse);
  fil_ptr->filp_count = 1;
  fil_ptr->filp_vno = vp;
  fil_ptr->filp_flags = oflags;
  
  /* Only do the normal open code if didn't just create the file. */
  if(exist) {
	/* Check protections. */
	if ((r = forbidden(vp, bits)) == 0) {
		/* Opening reg. files, directories, and special files differ */
  switch (vp->v_mode & I_TYPE) {
		   case I_REGULAR:
		   	/* Truncate regular file if O_TRUNC. */
		   	if (oflags & O_TRUNC) {
		   		if ((r = forbidden(vp, W_BIT)) != 0)
		   			break;
		   		truncate_vnode(vp, 0);
		   	}
		   	break;
		   case I_DIRECTORY:
		   	/* Directories may be read but not written. */
		   	r = (bits & W_BIT ? -EISDIR : 0);
		   	break;
      case I_CHAR_SPECIAL:
          /* Invoke the driver for special processing. */
			dev = (dev_t) vp->v_sdev;
			r = dev_open(dev, who_e, bits | (oflags & ~O_ACCMODE));
		   	if (r == SUSPEND) suspend(FP_BLOCKED_ON_DOPEN);
          break;
      case I_BLOCK_SPECIAL:
          /* Invoke the driver for special processing. */
			dev = (dev_t) vp->v_sdev;
			r = dev_open(dev, who_e, bits | (oflags & ~O_ACCMODE));
	 
			/* Check whether the device is mounted or not. If so,
			   then that FS is responsible for this device. Else
			   we default to ROOT_FS. */
			vp->v_bfs_e = ROOT_FS_E; /* By default */
			for (vmp = &vmnt[0]; vmp < &vmnt[NR_MNTS]; ++vmp) 
				if (vmp->m_dev == vp->v_sdev) 
	      vp->v_bfs_e = vmp->m_fs_e;
	  
	  /* Get the driver endpoint of the block spec device */
	  dp = &dmap[(vp->v_sdev >> MAJOR) & BYTE];
	  if (dp->dmap_driver == ENDPT_NONE) {
				printf("VFS: driver not found for device %d\n",
		      vp->v_sdev);
				r = -ENXIO;
	      break;
	  }

			/* Send the driver endpoint (even when known already)*/
			if ((r = req_newdriver(vp->v_bfs_e, vp->v_sdev,
					       dp->dmap_driver)) != 0) {
				printf("VFS: error sending driver endpoint\n");
				r = -ENXIO;
	  }
          break;

      case I_NAMED_PIPE:
			/* Create a mapped inode on PFS which handles reads
			   and writes to this named pipe. */
			r = map_vnode(vp);
			if (r == 0) {
	  vp->v_pipe = I_PIPE;
				if (vp->v_ref_count == 1) {
					vp->v_pipe_rd_pos = 0;
					vp->v_pipe_wr_pos = 0;
					if (vp->v_size != 0)
						r = truncate_vnode(vp, 0);
				}
          oflags |= O_APPEND;	/* force append mode */
          fil_ptr->filp_flags = oflags;
			}
			if (r == 0) {
          r = pipe_open(vp, bits, oflags);
			}
          if (r != -ENXIO) {
              /* See if someone else is doing a rd or wt on
               * the FIFO.  If so, use its filp entry so the
               * file position will be automatically shared.
               */
              b = (bits & R_BIT ? R_BIT : W_BIT);
              fil_ptr->filp_count = 0; /* don't find self */
              if ((filp2 = find_filp(vp, b)) != NIL_FILP) {
                  /* Co-reader or writer found. Use it.*/
                  fp->fp_filp[m_in.fd] = filp2;
                  filp2->filp_count++;
		  filp2->filp_vno = vp;
                  filp2->filp_flags = oflags;

					/* v_count was incremented after the
					 * vnode has been found. i_count was
					 * incremented incorrectly in FS, not
					 * knowing that we were going to use an
					 * existing filp entry.  Correct this
					 * error.
                   */
                  put_vnode(vp);
	      } else {
					/* Nobody else found. Restore filp. */
                  fil_ptr->filp_count = 1;
              }
          }
          break;
  }
	}
  }

  /* If error, release inode. */
  if (r != 0) {
	if (r == SUSPEND) return(r);		/* Oops, just suspended */
	fp->fp_filp[m_in.fd] = NIL_FILP;
  	FD_CLR(m_in.fd, &fp->fp_filp_inuse);
	fil_ptr->filp_count= 0;
	put_vnode(vp);     
	fil_ptr->filp_vno = NIL_VNODE;
	return(r);
  }
  
  return(m_in.fd);
}


/*===========================================================================*
 *				new_node				     *
 *===========================================================================*/
static struct vnode *new_node(mode_t bits)
		{
  struct vnode *dirp, *vp;
  int r;
  struct node_details res;
  struct vnode *rest;

  /* See if the path can be opened down to the last directory. */
  if ((dirp = last_dir()) == NIL_VNODE) return(NIL_VNODE);

  /* The final directory is accessible. Get final component of the path. */
  vp = advance(dirp, 0);
  if (vp == NIL_VNODE && err_code == -ENOENT) {
	/* Last path component does not exist. Make a new directory entry. */
	if ((vp = get_free_vnode()) == NIL_VNODE) {
		/* Can't create new vnode: out of vnodes. */
		put_vnode(dirp);	
		return(NIL_VNODE);
	}
	if ((r = forbidden(dirp, W_BIT|X_BIT)) != 0 ||
	    (r = req_create(dirp->v_fs_e, dirp->v_inode_nr,bits, fp->fp_effuid,
			    fp->fp_effgid, user_fullpath, &res)) != 0 ) {
		/* Can't create new directory entry: either no permission or
		   something else is wrong. */
		put_vnode(dirp);
		err_code = r;
		return(NIL_VNODE);
			}

	/* Store results and mark vnode in use */
				vp->v_fs_e = res.fs_e;
				vp->v_inode_nr = res.inode_nr;
				vp->v_mode = res.fmode;
				vp->v_size = res.fsize;
				vp->v_uid = res.uid;
				vp->v_gid = res.gid;
				vp->v_sdev = res.dev;
	vp->v_vmnt = dirp->v_vmnt;
				vp->v_dev = vp->v_vmnt->m_dev;
				vp->v_fs_count = 1;
				vp->v_ref_count = 1;
  } else {
  	/* Either last component exists, or there is some other problem. */
  	if (vp != NIL_VNODE)
  		r = -EEXIST;
		else
		r = err_code; 
	}

  err_code = r;
  put_vnode(dirp);
  return(vp);
}


/*===========================================================================*
 *				pipe_open				     *
 *===========================================================================*/
static int pipe_open(register struct vnode *vp, register mode_t bits,
	register int oflags)
{
/*  This function is called from common_open. It checks if
 *  there is at least one reader/writer pair for the pipe, if not
 *  it suspends the caller, otherwise it revives all other blocked
 *  processes hanging on the pipe.
 */

  vp->v_pipe = I_PIPE; 

  if((bits & (R_BIT|W_BIT)) == (R_BIT|W_BIT)) return(-ENXIO);

  if (find_filp(vp, bits & W_BIT ? R_BIT : W_BIT) == NIL_FILP) { 
	if (oflags & O_NONBLOCK) {
		if (bits & W_BIT) return(-ENXIO);
	} else {
		suspend(FP_BLOCKED_ON_POPEN);	/* suspend caller */
		return(SUSPEND);
	}
  } else if (susp_count > 0) {/* revive blocked processes */
	release(vp, __NR_open, susp_count);
	release(vp, __NR_creat, susp_count);
  }
  return 0;
}


/*===========================================================================*
 *				do_mknod				     *
 *===========================================================================*/
int do_mknod()
{
/* Perform the mknod(name, mode, addr) system call. */
  register mode_t bits, mode_bits;
  int r;
  struct vnode *vp;

  /* Only the super_user may make nodes other than fifos. */
  mode_bits = (mode_t) m_in.mk_mode;		/* mode of the inode */
  if (!super_user && ((mode_bits & I_TYPE) != I_NAMED_PIPE)) return(-EPERM);
  bits = (mode_bits & I_TYPE) | (mode_bits & ALL_MODES & fp->fp_umask);
  
  /* Open directory that's going to hold the new node. */
  if(fetch_name(user_fullpath, PATH_MAX, m_in.name1) < 0) return(err_code);
  if((vp = last_dir()) == NIL_VNODE) return(err_code);

  /* Make sure that the object is a directory */
  if((vp->v_mode & I_TYPE) != I_DIRECTORY) {
	put_vnode(vp);
	  return(-ENOTDIR);
  }

  if ((r = forbidden(vp, W_BIT|X_BIT)) == 0) {
	r = req_mknod(vp->v_fs_e, vp->v_inode_nr, user_fullpath, fp->fp_effuid,
		      fp->fp_effgid, bits, m_in.mk_z0);
  }

  put_vnode(vp);
  return(r);
}


/*===========================================================================*
 *				do_mkdir				     *
 *===========================================================================*/
int do_mkdir()
{
/* Perform the mkdir(name, mode) system call. */
  mode_t bits;			/* mode bits for the new inode */
  int r;
  struct vnode *vp;

  if (fetch_name(user_fullpath, PATH_MAX, m_in.name1) < 0) return(err_code);

  bits = I_DIRECTORY | (m_in.mode & RWX_MODES & fp->fp_umask);

  /* Request lookup */
  if((vp = last_dir()) == NIL_VNODE) return(err_code);

  /* Make sure that the object is a directory */
  if ((vp->v_mode & I_TYPE) != I_DIRECTORY) {
	put_vnode(vp);
	  return(-ENOTDIR);
  }

  if ((r = forbidden(vp, W_BIT|X_BIT)) == 0) {
	r = req_mkdir(vp->v_fs_e, vp->v_inode_nr, user_fullpath, fp->fp_effuid,
		      fp->fp_effgid, bits);
  }
  
  put_vnode(vp);
  return(r);
}

/*===========================================================================*
 *				scall_lseek				     *
 *===========================================================================*/
int scall_lseek()
{
	/* Perform the lseek(ls_fd, offset, whence) system call. */
	register struct filp *rfilp;
	int r;
	long offset;
	u64_t pos, newpos;

	/* Check to see if the file descriptor is valid. */
	if ( (rfilp = get_filp(m_in.ls_fd)) == NIL_FILP)
		return(err_code);

	/* No lseek on pipes. */
	if (rfilp->filp_vno->v_pipe == I_PIPE)
		return -ESPIPE;

	/* The value of 'whence' determines the start position to use. */
	switch(m_in.whence) {
	case SEEK_SET:
		pos = cvu64(0);
		break;

	case SEEK_CUR:
		pos = rfilp->filp_pos;
		break;

	case SEEK_END:
		pos = cvul64(rfilp->filp_vno->v_size);
		break;

	default:
		return(-EINVAL);
	}

	offset= m_in.offset_lo;
	if (offset >= 0)
		newpos= add64ul(pos, offset);
	else
		newpos= sub64ul(pos, -offset);

	/* Check for overflow. */
	if (ex64hi(newpos) != 0)
		return -EINVAL;

	if (cmp64(newpos, rfilp->filp_pos) != 0) { /* Inhibit read ahead request */
		r = req_inhibread(rfilp->filp_vno->v_fs_e, rfilp->filp_vno->v_inode_nr);

		if (r != 0)
			return r;
	}

	rfilp->filp_pos = newpos;

	return ex64lo(newpos);
}

/*===========================================================================*
 *				scall_llseek				     *
 *===========================================================================*/
int scall_llseek(void)
{
	/* Perform the llseek(fd, offset, whence) system call. */
	register struct filp *rfilp;
	loff_t pos, newpos;
	int r;

	/* Check to see if the file descriptor is valid. */
	if ((rfilp = get_filp(m_in.ls_fd)) == NIL_FILP)
		return -EBADF;

	/* No lseek on pipes. */
	if (rfilp->filp_vno->v_pipe == I_PIPE)
		return -ESPIPE;

	/* The value of 'whence' determines the start position to use. */
	switch(m_in.whence) {
	case SEEK_SET:
		pos = (loff_t)0;
		break;

	case SEEK_CUR:
		pos = rfilp->filp_pos;
		break;

	case SEEK_END:
		pos = (loff_t)rfilp->filp_vno->v_size;
		break;

	default:
		return -EINVAL;
	}

	newpos = pos + (((loff_t)m_in.offset_high << 32) | m_in.offset_lo);

	/* Check for overflow. */
	if (((long)m_in.offset_high > 0) && newpos < pos)
		return -EINVAL;

	if (((long)m_in.offset_high < 0) && newpos > pos)
		return -EINVAL;

	if (newpos != rfilp->filp_pos) { /* Inhibit read ahead request */
		r = req_inhibread(rfilp->filp_vno->v_fs_e, rfilp->filp_vno->v_inode_nr);

		if (r != 0)
			return r;
	}

	rfilp->filp_pos = newpos;

	/* Copy the result to user space */
	if (sys_datacopy(ENDPT_SELF, (vir_bytes)&newpos, who_e, (vir_bytes)m_in.result_addr, sizeof(newpos)))
		return -EFAULT;

	return 0;
}

/*===========================================================================*
 *				do_close				     *
 *===========================================================================*/
int do_close()
{
/* Perform the close(fd) system call. */
  return close_fd(fp, m_in.fd);
}


/*===========================================================================*
 *				close_fd				     *
 *===========================================================================*/
int close_fd(rfp, fd_nr)
struct fproc *rfp;
int fd_nr;
{
/* Perform the close(fd) system call. */
  register struct filp *rfilp;
  register struct vnode *vp;
  struct file_lock *flp;
  int lock_count;

  /* First locate the vnode that belongs to the file descriptor. */
  if ( (rfilp = get_filp2(rfp, fd_nr)) == NIL_FILP) return(err_code);
  vp = rfilp->filp_vno;
  close_filp(rfilp);

  FD_CLR(fd_nr, &rfp->fp_cloexec_set);
  rfp->fp_filp[fd_nr] = NIL_FILP;
  FD_CLR(fd_nr, &rfp->fp_filp_inuse);

  /* Check to see if the file is locked.  If so, release all locks. */
  if (nr_locks == 0) return 0;
  lock_count = nr_locks;	/* save count of locks */
  for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
	if (flp->lock_type == 0) continue;	/* slot not in use */
	if (flp->lock_vnode == vp && flp->lock_pid == rfp->fp_pid) {
		flp->lock_type = 0;
		nr_locks--;
	}
  }
  if (nr_locks < lock_count) lock_revive();	/* lock released */
  return 0;
}


/*===========================================================================*
 *				close_filp				     *
 *===========================================================================*/
void close_filp(fp)
struct filp *fp;
{
  int mode_word, rw;
  dev_t dev;
  struct vnode *vp;

  vp = fp->filp_vno;
  if (fp->filp_count - 1 == 0 && fp->filp_mode != FILP_CLOSED) {
	/* Check to see if the file is special. */
	mode_word = vp->v_mode & I_TYPE;
	if (mode_word == I_CHAR_SPECIAL || mode_word == I_BLOCK_SPECIAL) {
		dev = (dev_t) vp->v_sdev;
		if (mode_word == I_BLOCK_SPECIAL)  {
			if (vp->v_bfs_e == ROOT_FS_E) {
				/* Invalidate the cache unless the special is
				 * mounted. Assume that the root filesystem's
				 * is open only for fsck.
			 	 */
          			req_flush(vp->v_bfs_e, dev);
			}
		}
		/* Do any special processing on device close. */
		(void) dev_close(dev, fp-filp);

		/* Ignore any errors, even SUSPEND. */
	}
  }

  /* If the inode being closed is a pipe, release everyone hanging on it. */
  if (vp->v_pipe == I_PIPE) {
	rw = (fp->filp_mode & R_BIT ? __NR_write : __NR_read);
	release(vp, rw, NR_PROCS);
  }

  /* If a write has been done, the inode is already marked as DIRTY. */
  if (--fp->filp_count == 0) {
	if (vp->v_pipe == I_PIPE) {
		/* Last reader or writer is going. Tell MFS about latest
		 * pipe size.
		 */
		truncate_vnode(vp, vp->v_size);
	}
		
	put_vnode(fp->filp_vno);
  }

}

/*===========================================================================*
 *				close_reply				     *
 *===========================================================================*/
void close_reply()
{
	/* No need to do anything */
}


/*===========================================================================*
 *				do_vm_open				     *
 *===========================================================================*/
int do_vm_open()
{
	int len, r, n;
	endpoint_t ep;

	len = m_in.VMVO_NAME_LENGTH;
	m_out.VMV_ENDPOINT = ep = m_in.VMVO_ENDPOINT;

	/* Do open() call on behalf of any process, performed by VM. */ 
	if(len < 2 || len > sizeof(user_fullpath)) {
		printf("do_vm_open: strange length %d\n", len);
		m_out.VMVRO_FD = -EINVAL;
	return(VM_VFS_REPLY_OPEN);
	}

	/* Do open on behalf of which process? */
	if(isokendpt(ep, &n) != 0) {
		printf("do_vm_open: strange endpoint %d\n", ep);
		m_out.VMVRO_FD = -EINVAL;
	return(VM_VFS_REPLY_OPEN);
	}

	/* XXX - do open on behalf of this process */
	fp = &fproc[n];

	/* Get path name from VM address space. */
	if((r=sys_safecopyfrom(VM_PROC_NR, m_in.VMVO_NAME_GRANT, 0,
		(vir_bytes) user_fullpath, len, D)) != 0) {
		printf("do_vm_open: sys_safecopyfrom failed: %d\n", r);
		m_out.VMVRO_FD = -EPERM;
	return(VM_VFS_REPLY_OPEN);
	}

	/* Check if path is null-terminated. */
	if(user_fullpath[len-1] != '\0') {
		printf("do_vm_open: name (len %d) not 0-terminated\n", len);
		m_out.VMVRO_FD = -EINVAL;
	return(VM_VFS_REPLY_OPEN);
	}

	/* Perform open(). */
	m_out.VMVRO_FD = common_open(m_in.VMVO_FLAGS, m_in.VMVO_MODE);
	m_out.VMV_ENDPOINT = ep;

	/* Send open() reply. */
  return(VM_VFS_REPLY_OPEN);
}


/*===========================================================================*
 *				do_vm_close				     *
 *===========================================================================*/
int do_vm_close()
{
	int len, r, n;
	endpoint_t ep;

	len = m_in.VMVO_NAME_LENGTH;

	/* Do close() call on behalf of any process, performed by VM. */ 
	m_out.VMV_ENDPOINT = ep = m_in.VMVC_ENDPOINT;
	if(isokendpt(ep, &n) != 0) {
		printf("do_vm_close: strange endpoint %d\n", ep);
	return(VM_VFS_REPLY_CLOSE);
	}

	/* Perform close(). */
	r = close_fd(&fproc[n], m_in.VMVC_FD);

  return(VM_VFS_REPLY_CLOSE);
}

