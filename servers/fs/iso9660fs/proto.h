#ifndef __PROTO_H
#define __PROTO_H

#include "type.h"

/* Function prototypes for iso9660 file system. */

struct dir_record;
struct ext_attr_rec;
struct iso9660_vd_pri;

/* main.c */
int main(void);
void reply(int who, kipc_msg_t *m_out);

/* cache.c */
struct buf *get_block(block_t block);
void put_block(struct buf *bp);

/* device.c */
int block_dev_io(int op, dev_t dev, int proc, void *buf,
		 u64_t pos, int bytes, int flags);
int dev_open(endpoint_t driver_e, dev_t dev, int proc,
	     int flags);
void dev_close(endpoint_t driver_e, dev_t dev);
int fs_new_driver(void);
void dev_close(endpoint_t driver_e, dev_t dev);

/* inode.c */
int create_dir_record(struct dir_record *dir, char *buffer,
		      u32_t address);
int create_ext_attr(struct ext_attr_rec *ext, char *buffer);
int fs_getnode(void);
int fs_putnode(void);
struct dir_record *get_dir_record(ino_t id_dir);
struct dir_record *get_free_dir_record(void);
struct ext_attr_rec *get_free_ext_attr(void);
struct dir_record *load_dir_record_from_disk(u32_t address);
int release_dir_record(struct dir_record *dir);

/* misc.c */
int fs_sync(void);

/* mount.c */
int fs_readsuper(void);
int fs_mountpoint(void);
int fs_unmount(void);

/* path.c */
int fs_lookup(void);
int advance(struct dir_record *dirp, char string[NAME_MAX],
	    struct dir_record **resp);
int search_dir(struct dir_record *ldir_ptr,
	       char string [NAME_MAX], ino_t *numb);

/* protect.c */
int fs_access(void);

/* read.c */
int fs_read(void);
int fs_bread(void);
int fs_getdents(void);
int read_chunk(struct dir_record *rip, u64_t position,
	       unsigned off, int chunk, unsigned left,
	       cp_grant_id_t gid, unsigned buf_off,
	       int block_size, int *completed);

/* stadir.c */
int fs_stat(void);
int fs_fstatfs(void);

/* super.c */
int release_v_pri(struct iso9660_vd_pri *v_pri);
int read_vds(struct iso9660_vd_pri *v_pri, dev_t dev);
int create_v_pri(struct iso9660_vd_pri *v_pri, char *buffer,
		 unsigned long address);

/* utility.c */
int no_sys(void);
void panic(char *who, char *mess, int num);

#endif /* __PROTO_H */
