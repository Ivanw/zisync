AC_INIT([ZiSyncKernel], [1.3.0], [never.wencan@gmail.com])
# AC_CANONICAL_HOST
# AC_CANONICAL_BUILD
AC_CANONICAL_SYSTEM

AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([foreign -Wall   subdir-objects])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADER([config.h])

AM_PROG_CC_C_O
AM_PROG_AR
LT_INIT([disable-shared])
AC_PROG_CXX

PROJECT_DIR=$(pwd)
PROJECT_HEADER_DIR="$PROJECT_DIR/include"
GTEST_DIR="$PROJECT_DIR/third-part/gtest-1.7.0"
THIRD_PART_HEADER_DIR="$PROJECT_DIR/third-part/build/include"
THIRD_PART_LIB_DIR="$PROJECT_DIR/third-part/build/lib"
THIRD_PART_BIN_DIR="$PROJECT_DIR/third-part/build/bin"
BUILD_ANDROID_INSTALL=${BUILD_ANDROID_INSTALL}

dnl 
dnl Check OS
dnl
case "${host_os}" in 
     linux*)
     AC_CHECK_LIB(rt,timer_create,[LIBS+=" -lrt "],[AC_MSG_WARN([Could  not find rt library])])
     AC_CHECK_LIB(pthread,pthread_create,[LIBS+=" -lpthread "],[AC_MSG_WARN([Could  not find rt library])])
     ;;

     *darwin*)
     # nothing to do currently
     INOTIFY_SRC="zisync/kernel/monitor/fs_monitor_mac.cc"
     ;;

     *mingw32*)
     # nothing to do currently
     ;;

     *)
      AC_MSG_ERROR(["un-supportd platform ${host_os}"])
      ;;
esac

case "${host}" in
     *linux-androideabi*)
      AC_MSG_WARN(["*************** platform ${host_os}***************"])
      AM_CONDITIONAL([ZS_ANDROID], [ test 1 = 1 ])
      ;;
     *)
      AM_CONDITIONAL([ZS_ANDROID], [ test 0 = 1 ])
      ;;
esac


AC_SUBST(PROJECT_DIR)
AC_SUBST(GTEST_DIR)
AC_SUBST(PROJECT_HEADER_DIR)
AC_SUBST(THIRD_PART_HEADER_DIR)
AC_SUBST(THIRD_PART_LIB_DIR)
AC_SUBST(THIRD_PART_BIN_DIR)
AC_SUBST(host_os)
AC_SUBST(BUILD_ANDROID_INSTALL)


AC_ARG_ENABLE(debug, 
    [AS_HELP_STRING([--enable-debug],[turns -g on and -O2 off[default = no]])], 
	[[ CXXFLAGS=$(echo $CXXFLAGS | sed s/-O2// | sed s/-g// );  CXXFLAGS+=" -g -Wall ";
	 CFLAGS=$(echo $CFLAGS | sed s/-O2// | sed s/-g// );  CFLAGS+=" -g -Wall " ] 
    AC_DEFINE([DEBUG], [], [Enable debug])
    ], [ CXXFLAGS=$(echo $CXXFLAGS | sed s/-O2// | sed s/-g//);  CXXFLAGS+=" -DNDEBUG -O2 ";
		CFLAGS=$(echo $CFLAGS | sed s/-O2// | sed s/-g//);  CFLAGS+=" -DNDEBUG -O2 " ])
AC_ARG_ENABLE(test, 
    [AS_HELP_STRING([--enable-test],[define ZS_TEST])], 
    [CXXFLAGS+=" -DZS_TEST"])

CXXFLAGS+=" -I/opt/local/include -D_LARGEFILE64_SOURCE -DSQLITE_HAS_CODEC -std=c++11 -Wformat-security"

AC_CONFIG_FILES([Makefile 
                 src/Makefile
                 test/zisync/kernel/Makefile
                 ])

AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)

AC_OUTPUT
