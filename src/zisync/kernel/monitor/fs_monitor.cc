/**
 * @file fs_monitor.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief implements for fs monitor.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#include "zisync/kernel/monitor/fs_monitor.h"

#if defined(linux) || defined(__linux) || defined(__linux__) \
    || defined(__GNU__) || defined(__GLIBC__)
// linux, also other platforms (Hurd etc) that use GLIBC, should these
// really have their own config headers though?
#include "zisync/kernel/monitor/fs_monitor_linux.cc" // NOLINT

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
#include "zisync/kernel/monitor/fs_monitor_windows.cc"  // NOLINT

#elif defined(__BEOS__)
// BeOS
#error "platform unsupported by now"

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
// MacOS
#include "zisync/kernel/monitor/fs_monitor_mac.cc"  // NOLINT

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

