// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_PLATFORM_NETWORK_H_
#define ZISYNC_KERNEL_PLATFORM_NETWORK_H_

#include "zisync/kernel/platform/platform.h"

#ifdef WINDOWS
#include <winsock2.h>
#elif defined(UNIX)
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif

namespace zs {

//
// XXX:never - move to platform.h
//
// #ifdef WINDOWS
// typedef SOCKET socket_t;
// #elif defined(UNIX)
// typedef int socket_t;
// #endif

static inline void CloseSocket(socket_t fd) {
#ifdef WINDOWS
  closesocket(fd);
#elif defined(UNIX)
  close(fd);
#endif
}

}  // namespace zs

#endif  // ZISYNC_KERNEL_PLATFORM_NETWORK_H_
