/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  handle.c - libtar code for initializing a TAR handle
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

/* Demote modified */
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

//#ifdef HAVE_UNISTD_H
# include <unistd.h>
//#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
#endif

const char libtar_version[] = PACKAGE_VERSION;

struct _default_usrdata {
	char *pathname;
	int oflags;
	mode_t mode;
};
typedef struct _default_usrdata default_usrdata;

/* Demote modified */
//#define S_IRUSR    0400 
//#define S_IWUSR    0200
//#define S_IXUSR    0100
//#define S_IRGRP    0040
//#define S_IWGRP    0020
//#define S_IXGRP    0010
//#define S_IROTH    0004
//#define S_IWOTH    0002
//#define S_IXOTH    0001
int default_open(TAR *t, void* usrdata)
{
	tartype_t *old_type = (tartype_t *)(t->type->old);
	default_usrdata *ud = (default_usrdata *)usrdata;
	char *pathname = ud->pathname;
	int oflags = ud->oflags;
	mode_t mode = ud->mode;
	int options = t->options;

	t->pathname = pathname;
	

	if ((options & TAR_NOOVERWRITE) && (oflags & O_CREAT))
		oflags |= O_EXCL;

#ifdef O_BINARY
	oflags |= O_BINARY;
#endif

	(t)->desp.fd = (*(old_type->openfunc))(pathname, oflags, mode);
	if ((t)->desp.fd == -1)
	{
		free(t);
		return -1;
	}
	return 0;
}
int default_close(TAR *t)
{
	tartype_t *old_type = (tartype_t *)(t->type->old);
	return (*(old_type->closefunc))(t->desp.fd);
}
ssize_t default_read(TAR *t, void *buf, size_t count)
{
	tartype_t *old_type = (tartype_t *)(t->type->old);
	return (*(old_type->readfunc))(t->desp.fd, buf, count);
}
ssize_t default_write(TAR *t, const void *buf, size_t count)
{
	tartype_t *old_type = (tartype_t *)(t->type->old);
	return (*(old_type->writefunc))(t->desp.fd, buf, count);
}
int default_lstat(TAR *t, const char* pathname, struct stat *s)
{
	if (lstat(pathname, s) != 0)
	{
#ifdef DEBUG
		perror("lstat()");
#endif
		return -1;
	}
	
	/*
	s->st_mode |= S_IROTH;
	s->st_mode |= S_IWOTH;
	
	s->st_mode |= S_IRGRP;
	s->st_mode |= S_IWGRP;
	
	s->st_mode |= S_IRUSR;
	s->st_mode |= S_IWUSR;
	*/
	
	return 0;
}
static tartype_t default_type_sys = { open, close, read, write };
static tartypex_t default_type = { default_open, default_close, 
	default_read, default_write, default_lstat, &default_type_sys};
//static tartype_t default_type = { open, close, read, write };

/* Demote modified */
static int
tar_init(TAR **t, char *pathname, tartypex_t *type,
	 int oflags, int mode, int options)
{
	if ((oflags & O_ACCMODE) == O_RDWR)
	{
		errno = EINVAL;
		return -1;
	}

	*t = (TAR *)calloc(1, sizeof(TAR));
	if (*t == NULL)
		return -1;

	(*t)->pathname = pathname;
	(*t)->options = options;
	(*t)->type = (type ? type : &default_type);
	(*t)->oflags = oflags;

	if ((oflags & O_ACCMODE) == O_RDONLY)
		(*t)->h = libtar_hash_new(256,
					  (libtar_hashfunc_t)path_hashfunc);
	else
		(*t)->h = libtar_hash_new(16, (libtar_hashfunc_t)dev_hash);
	if ((*t)->h == NULL)
	{
		free(*t);
		return -1;
	}

	return 0;
}


/* open a new tarfile handle */
int
tar_open(TAR **t, char *pathname, tartype_t *type,
	 int oflags, int mode, int options)
{
	/*
	if (tar_init(t, pathname, type, oflags, mode, options) == -1)
		return -1;

	if ((options & TAR_NOOVERWRITE) && (oflags & O_CREAT))
		oflags |= O_EXCL;

#ifdef O_BINARY
	oflags |= O_BINARY;
#endif

	(*t)->fd = (*((*t)->type->openfunc))(pathname, oflags, mode);
	if ((*t)->fd == -1)
	{
		free(*t);
		return -1;
	}
	return 0;
	*/
	default_usrdata ud;
	tartypex_t* typex = NULL;

	ud.pathname = pathname;
	ud.oflags = oflags;
	ud.mode = mode;

	
	if (NULL != type) {
		typex = (tartypex_t*) calloc(1, sizeof(tartypex_t));
		typex->old = type;
		typex->openfuncx = default_open;
		typex->closefuncx = default_close;
		typex->readfuncx = default_read;
		typex->writefuncx = default_write;
		typex->lstatfuncx = default_lstat;
	}

	return tar_open_raw(t, typex, options, oflags, &ud);

}

/* Demote modified: open a new tarfile handle */
/* in tar_init(), pathname, oflags, mode need to be set in openfuncx_t*/
int
tar_open_raw(TAR **t, tartypex_t *type, int options, int oflags, void* usrdata)
{
	int retval = 0;
	if (tar_init(t, /*pathname*/NULL, type, oflags, /*mode*/0, options) == -1)
		return -1;

	retval = (*((*t)->type->openfuncx))((*t), usrdata);
	if (retval == -1)
	{
		free(*t);
		return -1;
	}

	return 0;
}

int
tar_fdopen(TAR **t, int fd, char *pathname, tartype_t *type,
	   int oflags, int mode, int options)
{
	/* Demote modified */
	tartypex_t* typex = NULL;
	if (NULL != type) {
		typex = (tartypex_t*) calloc(1, sizeof(tartypex_t));
		typex->old = type;
		typex->openfuncx = default_open;
		typex->closefuncx = default_close;
		typex->readfuncx = default_read;
		typex->writefuncx = default_write;
		typex->lstatfuncx = default_lstat;
	}

	if (tar_init(t, pathname, typex, oflags, mode, options) == -1)
		return -1;

	/* Demote modified */
	(*t)->desp.fd = fd;
	return 0;
}


int
tar_fd(TAR *t)
{
	/* Demote modified */
	//return t->fd;
	return t->desp.fd;
}


/* close tarfile handle */
int
tar_close(TAR *t)
{
	int i;

	/* Demote modified */
	//i = (*(t->type->closefunc))(t->fd);
	i = (*(t->type->closefuncx))(t);
	if (&default_type != t->type) {
		free(t->type);
	}

	if (t->h != NULL)
		libtar_hash_free(t->h, ((t->oflags & O_ACCMODE) == O_RDONLY
					? free
					: (libtar_freefunc_t)tar_dev_free));
	free(t);

	return i;
}

/* Demote modified */
int
tar_close_raw(TAR *t)
{
	int i;

	i = (*(t->type->closefuncx))(t);
	
	if (t->h != NULL)
		libtar_hash_free(t->h, ((t->oflags & O_ACCMODE) == O_RDONLY
		? free
		: (libtar_freefunc_t)tar_dev_free));
	free(t);

	return i;
}


