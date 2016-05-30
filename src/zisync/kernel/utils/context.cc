// Copyright 2014, zisync.com
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/zmq.h"

namespace zs {


static ZmqContext *context_;

const ZmqContext& GetGlobalContext() {
  assert(context_ != NULL);
  return *context_;
}

void GlobalContextInit() {
  if (context_ == NULL) {
    context_ = new ZmqContext();
    int io_threads = 4;
    zmq_ctx_set (context_->context(), ZMQ_IO_THREADS, io_threads);
  }
}

void GlobalContextFinal() {
  if (context_ != NULL) {
    delete context_;
    context_ = NULL;
  }
}

}  // namespace zs

