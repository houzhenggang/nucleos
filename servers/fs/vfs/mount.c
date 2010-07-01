/*
 *  Copyright (C) 2010  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file performs the MOUNT and UMOUNT system calls.
 *
 * The entry points into this file are
 *   do_mount:  perform the MOUNT system call
 *   do_umount: perform the UMOUNT system call
 */

#include "fs.h"
#include <nucleos/fcntl.h>
#include <nucleos/string.h>
#include <nucleos/unistd.h>
#include <nucleos/com.h>
#include <nucleos/keymap.h>
#include <nucleos/const.h>
#include <nucleos/endpoint.h>
#include <nucleos/syslib.h>
#include <nucleos/stat.h>
#include <nucleos/dirent.h>
#include "file.h"
#include <servers/vfs/fproc.h>
#include "param.h"

#include <nucleos/vfsif.h>
#include "vnode.h"
#include "vmnt.h"

struct vnode vnode[NR_VNODES];

/* Allow the root to be replaced before the first 'real' mount. */
static int allow_newroot = 1;

static dev_t name_to_dev(void);
static int mount_fs(endpoint_t fs_e);

/*===========================================================================*
 *                              do_fsready                                   *
 *===========================================================================*/
int do_fsready()
{
  int r;

  /* Login before mount request */
  if ((unsigned long)mount_m_in.m_data6 != who_e) {
      last_login_fs_e = who_e;
      r = SUSPEND;
  }
  /* Login after a suspended mount */
  else {
      /* Copy back original mount request message */
      m_in = mount_m_in;

      /* Set up last login FS */
      last_login_fs_e = who_e;

      /* Set up endpoint and call nr */
      who_e = m_in.m_source;
      who_p = _ENDPOINT_P(who_e);
      call_nr = m_in.m_type;
      fp = &fproc[who_p];       /* pointer to proc table struct */
      super_user = (fp->fp_effuid == SU_UID ? TRUE : FALSE);   /* su? */
      
	r = do_mount();
  }
  return(r);
}


/*===========================================================================*
 *                              do_mount                                     *
 *===========================================================================*/
int do_mount()
{
  endpoint_t fs_e; 

  /* Only the super-user may do MOUNT. */
  if (!super_user) return(-EPERM);
	
  /* FS process' endpoint number */ 
  fs_e = (unsigned long) m_in.fs_endpt;

  /* Sanity check on process number. */
  if(fs_e <= 0) {
	printf("VFS: warning: got process number %d for mount call.\n", fs_e);
	return -EINVAL;
  }

  /* Do the actual job */
  return mount_fs(fs_e);
}


/*===========================================================================*
 *                              mount                                        *
 *===========================================================================*/
static int mount_fs(endpoint_t fs_e)
{
/* Perform the mount(name, mfile, rd_only) system call. */
  int rdir, mdir;               /* TRUE iff {root|mount} file is dir */
  int i, r, found, isroot, replace_root;
  struct fproc *tfp;
  struct dmap *dp;
  dev_t dev;
  kipc_msg_t m;
  struct vnode *root_node, *vp = NULL, *bspec;
  struct vmnt *vmp;
  char *label;
  struct node_details res;

  /* Only the super-user may do MOUNT. */
  if (!super_user) return(-EPERM);

  /* If FS not yet logged in, save message and suspend mount */
  if (last_login_fs_e != fs_e) {
      mount_m_in = m_in; 
	return(SUSPEND);
  }
  
  /* Mount request got after FS login or FS login arrived after a suspended mount */
  last_login_fs_e = ENDPT_NONE;
  
  /* Clear endpoint field */
  mount_m_in.fs_endpt = (char *) ENDPT_NONE;

  /* If 'name' is not for a block special file, return error. */
  if (fetch_name(user_fullpath, PATH_MAX, m_in.name1) < 0) return(err_code);
  if ((dev = name_to_dev()) == NO_DEV) return(err_code);

  /* Check whether there is a block special file open which uses the 
   * same device (partition) */
  for (bspec = &vnode[0]; bspec < &vnode[NR_VNODES]; ++bspec) {
      if (bspec->v_ref_count > 0 && bspec->v_sdev == dev) {
          /* Found, sync the buffer cache */
          req_sync(bspec->v_fs_e);          
          break;
		/* Note: there are probably some blocks in the FS process'
		 * buffer cache which contain data on this minor, although
		 * they will be purged since the handling moves to the new
		 * FS process (if everything goes well with the mount...)
           */ 
      }
  }
  /* Didn't find? */
  if (bspec == &vnode[NR_VNODES] && bspec->v_sdev != dev)
      bspec = NULL;
  
  /* Scan vmnt table to see if dev already mounted. If not, find a free slot.*/
  found = FALSE; 
  vmp = NIL_VMNT;
  for (i = 0; i < NR_MNTS; ++i) {
        if (vmnt[i].m_dev == dev) {
            vmp = &vmnt[i];
            found = TRUE;
            break;
	  } else if (!vmp && vmnt[i].m_dev == NO_DEV) {
            vmp = &vmnt[i];
        }
  }

  /* Partition was/is already mounted */
  if (found) {
	/* It is possible that we have an old root lying around that 
	 * needs to be remounted. */
	if (vmp->m_mounted_on || vmp->m_mounted_on == fproc[FS_PROC_NR].fp_rd) {
		/* Normally, m_mounted_on refers to the mount point. For a
		 * root filesystem, m_mounted_on is equal to the root vnode.
		 * We assume that the root of FS is always the real root. If
		 * the two vnodes are different or if the root of FS is equal
		 * to the root of the filesystem we found, we found a
		 * filesystem that is in use.
		 */
		return(-EBUSY);   /* already mounted */
	}

	if(vmp->m_mounted_on)
		panic("vfs", "root unexpectedly mounted somewhere", NO_NUM);

	if (root_dev == vmp->m_dev)
		panic("vfs", "inconsistency remounting old root", NO_NUM);

	/* Now get the inode of the file to be mounted on. */
	if (fetch_name(user_fullpath, PATH_MAX, m_in.name2) < 0) return(err_code);
	if ((vp = eat_path(PATH_NOFLAGS)) == NIL_VNODE) return(err_code);
	if (vp->v_ref_count != 1) {
		put_vnode(vp);
		return(-EBUSY);
	}

	/* Tell FS on which vnode it is mounted (glue into mount tree) */
	if ((r = req_mountpoint(vp->v_fs_e, vp->v_inode_nr)) == 0) {
	root_node = vmp->m_root_node;

		/* File types of 'vp' and 'root_node' may not conflict. */
		mdir = ((vp->v_mode & I_TYPE) == I_DIRECTORY);/* TRUE iff dir*/
		rdir = ((root_node->v_mode & I_TYPE) == I_DIRECTORY);
		if (!mdir && rdir) r = -EISDIR;
	}

	if (r != 0) {
		put_vnode(vp);
		return(r);
	}

	/* Nothing else can go wrong.  Perform the mount. */
	vmp->m_mounted_on = vp;
	vmp->m_flags = m_in.rd_only;
	allow_newroot = 0;              /* The root is now fixed */

	return 0;
  }

  /* Fetch the name of the mountpoint */
  if (fetch_name(user_fullpath, PATH_MAX, m_in.name2) < 0) return(err_code);
  isroot= (strcmp(user_fullpath, "/") == 0);
  replace_root= (isroot && allow_newroot);

  if(!replace_root) {
  	/* Get vnode of mountpoint */
	if ((vp = eat_path(PATH_NOFLAGS)) == NIL_VNODE) return(err_code);

	/* Tell FS on which vnode it is mounted (glue into mount tree) */
	if ((r = req_mountpoint(vp->v_fs_e, vp->v_inode_nr)) != 0) {
		put_vnode(vp);
		return r;
	}
  }

  /* We'll need a vnode for the root inode, check whether there is one */
  if ((root_node = get_free_vnode()) == NIL_VNODE) return(-ENFILE);  

  /* Get driver process' endpoint */  
  dp = &dmap[(dev >> MAJOR) & BYTE];
  if (dp->dmap_driver == ENDPT_NONE) {
	  printf("VFS: no driver for dev %x\n", dev);
        return(-EINVAL);
  }

  label= dp->dmap_label;
  if (strlen(label) == 0)
	panic(__FILE__, "VFS mount_fs: no label for major", dev >> MAJOR);

  /* Tell FS which device to mount */
  if ((r = req_readsuper(fs_e, label, dev, m_in.rd_only, isroot, &res)) != 0){
	if (vp != NIL_VNODE) put_vnode(vp);
	return(r);
  }

  /* Fill in root node's fields */
  root_node->v_fs_e = res.fs_e;
  root_node->v_inode_nr = res.inode_nr;
  root_node->v_mode = res.fmode;
  root_node->v_uid = res.uid;
  root_node->v_gid = res.gid;
  root_node->v_size = res.fsize;
  root_node->v_sdev = NO_DEV;
  root_node->v_fs_count = 1;
  root_node->v_ref_count = 1;

  /* Fill in max file size and blocksize for the vmnt */
  vmp->m_fs_e = res.fs_e;
  vmp->m_dev = dev;
  vmp->m_flags = m_in.rd_only;
  
  /* Root node is indeed on the partition */
  root_node->v_vmnt = vmp;
  root_node->v_dev = vmp->m_dev;
  
  if (replace_root) {
      /* Superblock and root node already read. 
       * Nothing else can go wrong. Perform the mount. */
      vmp->m_root_node = root_node;
      vmp->m_mounted_on = NULL;

      root_dev = dev;
      ROOT_FS_E = fs_e;

      /* Replace all root and working directories */
      for (i= 0, tfp= fproc; i<NR_PROCS; i++, tfp++) {
          if (tfp->fp_pid == PID_FREE)
              continue;

#define MAKEROOT(what) { 		\
		put_vnode(what);	\
		dup_vnode(root_node);	\
		what = root_node;	\
	  }

	  if(tfp->fp_rd) MAKEROOT(tfp->fp_rd);
	  if(tfp->fp_wd) MAKEROOT(tfp->fp_wd);
      }

      return(0);
  }

  /* File types may not conflict. */
  if (r == 0) {
	mdir = ((vp->v_mode & I_TYPE) == I_DIRECTORY); /*TRUE iff dir*/
      rdir = ((root_node->v_mode & I_TYPE) == I_DIRECTORY);
      if (!mdir && rdir) r = -EISDIR;
  }

  /* If error, return the super block and both inodes; release the vmnt. */
  if (r != 0) {
	put_vnode(vp);
      put_vnode(root_node);
      vmp->m_dev = NO_DEV;
      return(r);
  }

  /* Nothing else can go wrong.  Perform the mount. */
  vmp->m_mounted_on = vp;
  vmp->m_root_node = root_node;

  /* The root is now fixed */
  allow_newroot = 0;            

  /* There was a block spec file open, and it should be handled by the 
   * new FS proc now */
  if (bspec) bspec->v_bfs_e = fs_e; 

  return(0);
}

/*===========================================================================*
 *                              do_umount                                    *
 *===========================================================================*/
int do_umount()
{
/* Perform the umount(name) system call. */
  dev_t dev;

  /* Only the super-user may do umount. */
  if (!super_user) return(-EPERM);

  /* If 'name' is not for a block special file, return error. */
  if (fetch_name(user_fullpath, PATH_MAX, m_in.name) < 0) return(err_code);
  if ( (dev = name_to_dev()) == NO_DEV) return(err_code);
  return unmount(dev);
}


/*===========================================================================*
 *                              unmount                                      *
 *===========================================================================*/
int unmount(dev)
dev_t dev;
{
  struct vnode *vp, *vi;
  struct vmnt *vmp_i = NULL, *vmp = NULL;
  struct dmap *dp;
  int count, r;
  int fs_e;
  
  /* Find vmnt that is to be unmounted */
  for (vmp_i = &vmnt[0]; vmp_i < &vmnt[NR_MNTS]; ++vmp_i) {
      if (vmp_i->m_dev == dev) {
	if(vmp) panic(__FILE__, "device mounted more than once", dev);
	vmp = vmp_i;
      }
  }

  /* Did we find the vmnt (i.e., was dev a mounted device)? */
  if(!vmp) return(-EINVAL);

  /* See if the mounted device is busy.  Only 1 vnode using it should be
   * open -- the root vnode -- and that inode only 1 time. */
  count = 0;
  for(vp = &vnode[0]; vp < &vnode[NR_VNODES]; vp++)
	  if(vp->v_ref_count > 0 && vp->v_dev == dev) count += vp->v_ref_count;

  if(count > 1) return(-EBUSY);    /* can't umount a busy file system */

  /* Tell FS to drop all inode references for root inode except 1. */
  vnode_clean_refs(vmp->m_root_node);

  if (vmp->m_mounted_on) {
      put_vnode(vmp->m_mounted_on);
      vmp->m_mounted_on = NIL_VNODE;
  }

  /* Tell FS to unmount */
  if(vmp->m_fs_e <= 0 || vmp->m_fs_e == ENDPT_NONE)
	panic(__FILE__, "unmount: strange fs endpoint", vmp->m_fs_e);

  if ((r = req_unmount(vmp->m_fs_e)) != 0)              /* Not recoverable. */
	printf("VFS: ignoring failed umount attempt (%d)\n", r);
  
  vmp->m_root_node->v_ref_count = 0;
  vmp->m_root_node->v_fs_count = 0;
  vmp->m_root_node->v_sdev = NO_DEV;
  vmp->m_root_node = NIL_VNODE;
  vmp->m_dev = NO_DEV;
  vmp->m_fs_e = ENDPT_NONE;

  /* Is there a block special file that was handled by that partition? */
  for (vp = &vnode[0]; vp < &vnode[NR_VNODES]; vp++) {
	if((vp->v_mode & I_TYPE)==I_BLOCK_SPECIAL && vp->v_bfs_e==vmp->m_fs_e){

          /* Get the driver endpoint of the block spec device */
          dp = &dmap[(dev >> MAJOR) & BYTE];
          if (dp->dmap_driver == ENDPT_NONE) {
			printf("VFS: driver not found for device %d\n", dev);
              continue;
          }

		printf("VFS: umount moving block spec %d to root FS\n", dev);
          vp->v_bfs_e = ROOT_FS_E;

		  /* Send the (potentially new) driver endpoint */
		r = req_newdriver(vp->v_bfs_e, vp->v_sdev, dp->dmap_driver);
		if (r != 0) 
			printf("VFS: error sending driver endpoint for"
				" moved block spec\n");
		  
      }
  }

  return(0);
}

/*===========================================================================*
 *                              name_to_dev                                  *
 *===========================================================================*/
static dev_t name_to_dev()
{
/* Convert the block special file 'path' to a device number.  If 'path'
 * is not a block special file, return error code in 'err_code'. */
  int r;
  dev_t dev;
  struct vnode *vp;
  
  /* Request lookup */
  if ((vp = eat_path(PATH_NOFLAGS)) == NIL_VNODE) {
	printf("VFS: name_to_dev: lookup of '%s' failed\n", user_fullpath);
	return(NO_DEV);
  }

  if ((vp->v_mode & I_TYPE) != I_BLOCK_SPECIAL) {
  	err_code = -ENOTBLK;
	dev= NO_DEV;
  } else
	dev= vp->v_sdev;

  put_vnode(vp);
  return(dev);
}

