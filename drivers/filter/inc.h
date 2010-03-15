/* Filter driver - general include file */
#include <nucleos/const.h>
#include <nucleos/type.h>
#include <nucleos/com.h>
#include <nucleos/ipc.h>
#include <nucleos/sysutil.h>
#include <nucleos/syslib.h>
#include <nucleos/partition.h>
#include <nucleos/errno.h>
#include <nucleos/string.h>
#include <nucleos/unistd.h>
#include <servers/ds/ds.h>
#include <asm/ioctls.h>
#include <stdio.h>
#include <stdlib.h>

#define SECTOR_SIZE	512

enum {
  ST_NIL,		/* Zero checksums */
  ST_XOR,		/* XOR-based checksums */
  ST_CRC,		/* CRC32-based checksums */
  ST_MD5		/* MD5-based checksums */
};

enum {
  FLT_WRITE,		/* write to up to two disks */
  FLT_READ,		/* read from one disk */
  FLT_READ2		/* read from both disks */
};

/* Something was wrong and the disk driver has been restarted/refreshed,
 * so the request needs to be redone.
 */
#define RET_REDO	1

/* The cases where the disk driver need to be restarted/refreshed by RS. 
 * BD_DEAD: the disk driver has died. Restart it.
 * BD_PROTO: a protocol error has occurred. Refresh it.
 * BD_DATA: a data error has occurred. Refresh it.
 */
enum {
  BD_NONE,
  BD_DEAD,
  BD_PROTO,
  BD_DATA,
  BD_LAST
};

#define DRIVER_MAIN	0
#define DRIVER_BACKUP	1

/* Requests for more than this many bytes will be allocated dynamically. */
#define BUF_SIZE	(256 * 1024)
#define SBUF_SIZE	(BUF_SIZE * 2)

#define LABEL_SIZE	32

typedef unsigned long	sector_t;

/* main.c */
extern int USE_CHECKSUM;
extern int USE_MIRROR;
extern int BAD_SUM_ERROR;
extern int USE_SUM_LAYOUT;
extern int SUM_TYPE;
extern int SUM_SIZE;
extern int NR_SUM_SEC;
extern int NR_RETRIES;
extern int NR_RESTARTS;
extern int DRIVER_TIMEOUT;
extern int CHUNK_SIZE;

extern char MAIN_LABEL[LABEL_SIZE];
extern char BACKUP_LABEL[LABEL_SIZE];
extern int MAIN_MINOR;
extern int BACKUP_MINOR;

/* sum.c */
extern void sum_init(void);
extern int transfer(u64_t pos, char *buffer, size_t *sizep, int flag_rw);
extern u64_t convert(u64_t size);

/* driver.c */
extern void driver_init(void);
extern void driver_shutdown(void);
extern u64_t get_raw_size(void);
extern void reset_kills(void);
extern int check_driver(int which);
extern int bad_driver(int which, int type, int error);
extern int read_write(u64_t pos, char *bufa, char *bufb, size_t *sizep,
	int flag_rw);

/* util.c */
extern char *flt_malloc(size_t size, char *sbuf, size_t ssize);
extern void flt_free(char *buf, size_t size, char *sbuf);
extern char *print64(u64_t p);
extern clock_t flt_alarm(clock_t dt);
extern void flt_sleep(int secs);
