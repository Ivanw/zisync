// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_CONTEXT_H_
#define ZISYNC_KERNEL_UTILS_CONTEXT_H_

namespace zs {

class ZmqContext;

const ZmqContext& GetGlobalContext();
void GlobalContextInit();
void GlobalContextFinal();

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_CONTEXT_H_
