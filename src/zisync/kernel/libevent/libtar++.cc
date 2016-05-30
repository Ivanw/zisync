/****************************************************************************
 *       Filename:  tar_file_header.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  02/04/15 13:53:01
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  PangHai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#include <stdio.h>
#include "zisync/kernel/platform/platform.h"

#include "zisync/kernel/libevent/libtar++.h"

namespace zs {
void th_set_mode(struct tar_header *th, mode_t fmode) {
#ifndef _WIN32
  if (S_ISSOCK(fmode)) {
    fmode &= ~S_IFSOCK;
    fmode |= S_IFIFO;
  }
#endif  // _WIN32

  int_to_oct((fmode & 0xFFF), (th)->mode, 8);
}

void int_to_oct_nonull(int64_t num, char *oct, size_t octlen) {
  snprintf(oct, octlen, "%0*" PRIo64, (int)(octlen - 1), num);
  oct[octlen - 1] = 0;
}

void th_set_path(struct tar_header *th, char *pathname) {
  char suffix[2] = "";
  char *tmp;

  if (th->gnu_longname != NULL) {
    free(th->gnu_longname);
    th->gnu_longname = NULL;
  }

  if (pathname[strlen(pathname) - 1] != '/' && TH_ISDIR(th)) {
    strcpy(suffix, "/");
  }

  if (strlen(pathname) > T_NAMELEN-1) {
    /* GNU-style long name */
    th->gnu_longname = strdup(pathname);
    strncpy(th->name, th->gnu_longname, T_NAMELEN);
  } else if (strlen(pathname) > T_NAMELEN) {
    /* POSIX-style prefix field */
    tmp = strchr(&(pathname[strlen(pathname) - T_NAMELEN - 1]), '/');
    if (tmp == NULL) {
      return ;
    }
    snprintf(th->name, 100, "%s%s", &(tmp[1]), suffix);
    snprintf(th->prefix, ((tmp - pathname + 1) <
                                   155 ? (tmp - pathname + 1) : 155), "%s", pathname);
  } else {
    snprintf(th->name, 100, "%s%s", pathname, suffix);
  }
}

void th_finish(struct tar_header *th) {
  int i, sum = 0;

  strncpy(th->magic, "ustar ", 6);
  strncpy(th->version, " ", 2);

  for (i = 0; i < T_BLOCKSIZE; i++) {
    sum += ((unsigned char *)(th))[i];
  }

  for (i = 0; i < 8; i++) {
    sum += (' ' - th->chksum[i]);
  }

  snprintf(th->chksum, 8, "%06lo", (unsigned long)(sum));
  th->chksum[6] = 0;
  th->chksum[7] = ' ';
}

/* string-octal to integer conversion */
int64_t oct_to_int(const char *oct) {
  int64_t i;
  sscanf(oct, "%" PRIo64, &i);

  return i;
}

int th_crc_calc(struct tar_header *th) {
  int i, sum = 0;

  for (i = 0; i < T_BLOCKSIZE; i++) {
    sum += ((unsigned char *)(th))[i];
  }

  for (i = 0; i < 8; i++) {
    sum += (' ' - (unsigned char)th->chksum[i]);
  }

  return sum;
}

char *th_get_pathname(const struct tar_header *th,
                      char *filename, int nmemb) {
  if (th->gnu_longname) {
    return th->gnu_longname;
  }

  if (th->prefix[0] != '\0') {
    snprintf(filename, nmemb, "%.155s/%.100s",
             th->prefix, th->name);
    return filename;
  }
  snprintf(filename, nmemb, "%.100s", th->name);

  return filename;
}

void th_print(struct tar_header *th) {
  puts("\nPrinting tar header:");
  printf("  name     = \"%.100s\"\n", th->name);
  printf("  mode     = \"%.8s\"\n", th->mode);
  printf("  uid      = \"%.8s\"\n", th->uid);
  printf("  gid      = \"%.8s\"\n", th->gid);
  printf("  size     = \"%.12s\"\n", th->size);
  printf("  mtime    = \"%.12s\"\n", th->mtime);
  printf("  chksum   = \"%.8s\"\n", th->chksum);
  printf("  typeflag = \'%c\'\n", th->typeflag);
  printf("  linkname = \"%.100s\"\n", th->linkname);
  printf("  magic    = \"%.6s\"\n", th->magic);
  /*printf("  version  = \"%.2s\"\n", th->version); */
  printf("  version[0] = \'%c\',version[1] = \'%c\'\n",
         th->version[0], th->version[1]);
  printf("  uname    = \"%.32s\"\n", th->uname);
  printf("  gname    = \"%.32s\"\n", th->gname);
  printf("  devmajor = \"%.8s\"\n", th->devmajor);
  printf("  devminor = \"%.8s\"\n", th->devminor);
  printf("  prefix   = \"%.155s\"\n", th->prefix);
  printf("  padding  = \"%.1s\"\n", th->padding);
  printf("  gnu_longname = \"%s\"\n",
         (th->gnu_longname ? th->gnu_longname : "[NULL]"));
  printf("  gnu_longlink = \"%s\"\n",
         (th->gnu_longlink ? th->gnu_longlink : "[NULL]"));
}
}  // namespace zs
