// Copyright 2014 zisync.com

#include "zisync/kernel/worker/worker.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/zmq.h"

namespace zs {

Worker::~Worker() {
  for (auto it = msg_handlers_.begin();
       it != msg_handlers_.end(); ++it) {
    delete it->second;
  }
  msg_handlers_.clear();
  if (req != NULL) {
    delete req;
  }
  if (exit != NULL) {
    delete exit;
  }
}
  
Worker::Worker(const char *thread_name):
    OsThread(thread_name), req(new ZmqSocket(GetGlobalContext(), ZMQ_REQ)), 
    exit(new ZmqSocket(GetGlobalContext(), ZMQ_SUB)), name(thread_name) {};

err_t Worker::Startup() {
  err_t zisync_ret = Initialize();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Initialize fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  int ret = OsThread::Startup();
  if (ret == -1) {
    ZSLOG_ERROR("Start thread fail");
    return ZISYNC_ERROR_OS_THREAD;
  }

  return ZISYNC_SUCCESS;
}

err_t Worker::HandleRequest() {
  MessageContainer container(msg_handlers_, true);
  return container.RecvAndHandleSingleMessage(*req, this);
}


int Worker::Run() {
  ZmqIdentify ready_msg(ZmqIdentify::ReadyStr);
  err_t zisync_ret = ready_msg.SendTo(*req, 0);
  assert(zisync_ret == ZISYNC_SUCCESS);

  assert(req != NULL);
  assert(exit != NULL);
  while (1) {
    zmq_pollitem_t items[] = {
      { req->socket(), 0, ZMQ_POLLIN, 0 },
      { exit->socket(), 0, ZMQ_POLLIN, 0 },
    };
    int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t) , -1);
    if (ret == -1) {
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      HandleRequest();
    }

    if (items[1].revents & ZMQ_POLLIN) {
      return 0;
    }
  }

  return 0;
}

}  // namespace zs
