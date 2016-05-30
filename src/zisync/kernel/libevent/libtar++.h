/****************************************************************************
 *       Filename:  tar_file_header.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  02/04/15 13:09:22
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  PangHai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#ifndef TAR_FILE_HEADER_H
#define TAR_FILE_HEADER_H

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "zisync/kernel/libevent/tar.h"

/* useful constants */

#ifdef _WIN32
#include "win32/types.h"
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif  // _WIN32

//#ifdef __cplusplus
//extern "C" {
//#endif  // __cplusplus

#ifdef _WIN32

#include <direct.h>
#include <io.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)

#define S_ISREG(m)  (((m) & (_S_IFMT)) == (_S_IFREG))
#define S_ISDIR(m)  (((m) & (_S_IFMT)) == (_S_IFDIR))
#define S_ISCHR(m)  (((m) & (_S_IFMT)) == (_S_IFCHR))
#define S_ISBLK(m)  (0)
#define S_ISFIFO(m) (((m) & (_S_IFMT)) == (_S_IFIFO))
#define S_ISLNK(m)  (0)
#define S_ISSOCK(m) (0)

#define chown(p, o, g)         (0) 
#define geteuid()              (0)
#define lstat(p, b)            stat((p), (b))
#define makedev(maj, min)      (0)
#define mkdir(d, m)            _mkdir(d)
#define mkfifo(p, m)           (0)
#define mknod(p, m, d)         (0)
#define snprintf(s, n, f, ...) _snprintf((s), (n), (f), __VA_ARGS__)

#endif


namespace zs {
#define T_BLOCKSIZE 512
#define T_NAMELEN 100
#define T_PREFIXLEN 155
#define T_MAXPATHLEN (T_NAMELEN + T_PREFIXLEN)

/* GNU extensions for typeflag */
#define GNU_LONGNAME_TYPE 'L'
#define GNU_LONGLINK_TYPE 'K'

struct tar_header
{
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[23];
  char mtime[12];
  char chksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char padding[1];
  char *gnu_longname;
  char *gnu_longlink;
};

/* determine file type */
#define TH_ISREG(th) ((th)->typeflag == REGTYPE \
                               || (th)->typeflag == AREGTYPE \
                               || (th)->typeflag == CONTTYPE \
                               || (S_ISREG((mode_t)oct_to_int((th)->mode)) \
                                   && (th)->typeflag != LNKTYPE))
#define TH_ISLNK(th) ((th)->typeflag == LNKTYPE)
#define TH_ISSYM(th) ((th)->typeflag == SYMTYPE \
                               || S_ISLNK((mode_t)oct_to_int((th)->mode)))
#define TH_ISCHR(th) ((th)->typeflag == CHRTYPE \
                               || S_ISCHR((mode_t)oct_to_int((th)->mode)))
#define TH_ISBLK(th) ((th)->typeflag == BLKTYPE \
                               || S_ISBLK((mode_t)oct_to_int((th)->mode)))
#define TH_ISDIR(th) ((th)->typeflag == DIRTYPE \
                               || S_ISDIR((mode_t)oct_to_int((th)->mode)) \
                               || ((th)->typeflag == AREGTYPE \
                                   && ((th)->name[strlen((th)->name) - 1] == '/')))
#define TH_ISFIFO(th) ((th)->typeflag == FIFOTYPE \
                                || S_ISFIFO((mode_t)oct_to_int((th)->mode)))

#define TH_ISLONGNAME(th) ((th)->typeflag == GNU_LONGNAME_TYPE)
#define TH_ISLONGLINK(th) ((th)->typeflag == GNU_LONGLINK_TYPE)

/* decode tar header info */
#define th_get_crc(th) oct_to_int((th)->chksum)
#define th_get_size(th) oct_to_int((th)->size)
#define th_get_mtime(th) oct_to_int((th)->mtime)
#define th_get_devmajor(th) oct_to_int((th)->devmajor)
#define th_get_devminor(th) oct_to_int((th)->devminor)
#define th_get_linkname(t) ((th)->gnu_longlink \
                            ? (th)->gnu_longlink \
                            : (th)->gnu_longname)

char *th_get_pathname(const struct tar_header *th,
                      char *filename, int nmemb);
mode_t th_get_mode(struct tar_header *th);
uid_t th_get_uid(struct tar_header *th);
gid_t th_get_gid(struct tar_header *th);

/* encode file info in th_header */
void th_set_type(struct tar_header *th, mode_t mode);
void th_set_path(struct tar_header *th, char *pathname);
void th_set_link(struct tar_header *th, char *linkname);
void th_set_device(struct tar_header *th, dev_t device);
void th_set_user(struct tar_header *th, uid_t uid);
void th_set_group(struct tar_header *th, gid_t gid);
void th_set_mode(struct tar_header *th, mode_t fmode);

/* integer to string-octal conversion, no NULL */
void int_to_oct_nonull(int64_t num, char *oct, size_t octlen);

#define th_set_mtime(th, fmtime) \
    int_to_oct_nonull((fmtime), (th)->mtime, 12)
#define th_set_size(th, fsize) \
    int_to_oct_nonull((fsize), (th)->size, 12)

/* encode everything at once (except the pathname and linkname) */
void th_set_from_stat(struct tar_header *th, struct stat *s);

/* encode magic, version, and crc - must be done after everthing else is set */
void th_finish(struct tar_header *th);

/* calculate header checksum */
int th_crc_calc(struct tar_header *th);
#define th_crc_ok(th) (th_get_crc(th) == th_crc_calc(th))

/* string-octal to integer conversion */
int64_t oct_to_int(const char *oct);

#define int_to_oct(num, oct, octlen) \
    snprintf((oct), (octlen), "%0*lo", (octlen) - 1, (unsigned long)(num))


/* print the tar header */
void th_print(struct tar_header *th);

//#ifdef __cplusplus
//}
//#endif  // __cplusplus

}  // namespace zs
#endif  // TAR_FILE_HEADER_H
