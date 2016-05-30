AC_INIT([libsqlite3], [0.6.0], [liulikun@gmail.com])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADER([config.h])

AM_PROG_AR
LT_INIT
AC_PROG_CXX

AC_ARG_ENABLE(debug, 
    [AS_HELP_STRING([--enable-debug],[turns -g on and -O2 off[default = no]])], 
    [[CFLAGS="-g -Wall -Werror"] 
    AC_DEFINE([DEBUG], [], [Enable debug])
    ], [ CFLAGS="-DNDEBUG" ])

AC_CONFIG_FILES([Makefile])

AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)

AC_OUTPUT
