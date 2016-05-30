// Copyright 2014 zisync.com

#include "zisync/kernel/platform/platform.h"

#if defined(linux) || defined(__linux) || defined(__linux__) \
    || defined(__GNU__) || defined(__GLIBC__)
// linux, also other platforms (Hurd etc) that use GLIBC, should these
// really have their own config headers though?
#include "zisync/kernel/platform/platform_linux.cc" // NOLINT

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
    || defined(__DragonFly__)
// BSD:
#error "platform unsupported by now"

#elif defined(sun) || defined(__sun)
// solaris:
#error "platform unsupported by now"

#elif defined(__sgi)
// SGI Irix:
#error "platform unsupported by now"

#elif defined(__hpux)
// hp unix:
#error "platform unsupported by now"

#elif defined(__CYGWIN__)
// cygwin is not win32:
#error "platform unsupported by now"

#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
// win32:
#include "zisync/kernel/platform/platform_windows.cc"  // NOLINT

#elif defined(__BEOS__)
// BeOS
#error "platform unsupported by now"

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
// MacOS
#include "zisync/kernel/platform/platform_mac.cc"  // NOLINT

#elif defined(__IBMCPP__) || defined(_AIX)
// IBM
#error "platform unsupported by now"

#elif defined(__amigaos__)
// AmigaOS
#error "platform unsupported by now"

#elif defined(__QNXNTO__)
// QNX:
#error "platform unsupported by now"

#elif defined(__VXWORKS__)
// vxWorks:
#error "platform unsupported by now"

#elif defined(__SYMBIAN32__)
// Symbian:
#error "platform unsupported by now"

#elif defined(__VMS)
// VMS:
#error "platform unsupported by now"

#else
// Unknown
#error "Unkonwn platform "

#endif
