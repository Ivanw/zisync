/**
 * @file TransferTask.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief transfer task.
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

#ifndef TRANSFERTASK_H
#define TRANSFERTASK_H

#include <memory>

#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/libevent/http.h"

namespace zs {

class OsEvent;
class TarWriter;
class TransferServer2;
class HttpResponseHead;
class IHttpResponseParser;

class TransferTask : public IBufferEventDelegate {
  friend class TransferServer2;
  enum ReadState {
    RS_HEAD, RS_BODY, RS_DONE, RS_ERROR
  };
  enum WriteState {
    WS_HEAD, WS_BODY, WS_DONE, WS_ERROR
  };

  class AsyncWrite {
   public:
    AsyncWrite(TransferTask* task) : task_(task) { }
    
    void Perform() {
      task_->outstanding_write_.reset();
      if (task_->bev_) {
        task_->OnWrite(task_->bev_.get());
      }
    }
    
    TransferTask* task_;
  };
  
 public:
  TransferTask(TransferServer2* server, const std::string& uri,
               int32_t local_tree_id, const std::string& remote_tree_uuid);
  virtual ~TransferTask();

  err_t Wait();
  
  virtual void OnRead(struct bufferevent* bev);
  virtual void OnWrite(struct bufferevent* bev);
  virtual void OnEvent(struct bufferevent* bev, short events);

  virtual void CreateResponseBodyParser(int32_t http_code) = 0;
  virtual void RequestHeadWriteAll(struct bufferevent* bev) = 0;
  virtual err_t RequestBodyWriteSome(struct bufferevent* bev) = 0;

 protected:
  virtual void OnComplete(struct bufferevent* bev, err_t eno);

 private:
  virtual void OnWriteMore(struct bufferevent* bev);
  
 protected:
  static void LambdaAsyncWrite(void* ctx);

  std::string uri_;

  err_t return_code_;
  WriteState write_state_;
  ReadState read_state_;

  std::unique_ptr<HttpResponseHead> response_head_;
  std::unique_ptr<IHttpResponseParser> response_body_;
  
  int32_t task_id_;
  TransferServer2* transfer_server_;
  std::unique_ptr<struct bufferevent, void(*)(struct bufferevent*)> bev_;

  std::unique_ptr<OsEvent> finish_event_;
  std::shared_ptr<AsyncWrite> outstanding_write_;

  int32_t local_tree_id_;
  std::string remote_tree_uuid_;
};


}  // namespace zs

#endif
