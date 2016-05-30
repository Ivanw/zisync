AC_INIT([libuuid], [2.25.1], [liulikun@gmail.com])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_CONFIG_MACRO_DIR([m4])

AM_PROG_AR
LT_INIT
AC_PROG_CC

AC_CONFIG_FILES([Makefile src/Makefile])

AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)

AC_CHECK_FUNCS([ \
                nanosleep \
                ])
AC_CHECK_HEADERS([ \
                  sys/file.h
                  ])

CFLAGS=$(echo $CXXFLAGS | sed s/-O2// | sed s/-g//)
CFLAGS+=" -DNDEBUG -O2 -include config.h"

AC_CONFIG_HEADER([config.h])


AC_OUTPUT
