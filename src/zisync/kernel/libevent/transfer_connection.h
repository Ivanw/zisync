/**
 * @file transfer_connection.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Accepted connection .
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

#ifndef ZISYNC_KERNEL_LIBEVENT_TRANSFER_CONNECTION_H_
#define ZISYNC_KERNEL_LIBEVENT_TRANSFER_CONNECTION_H_

#include <event2/bufferevent.h>
#include <memory>

#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/libevent/transfer.h"

namespace zs {

class TransferServer2;
class HttpRequestHead;
class IHttpRequestParser;
class IHttpResponseWriter;
class AsyncWrite;


class TransferConnection : public IBufferEventDelegate {
  friend class TransferServer2;
  enum WriteState {
    WS_HEAD, WS_BODY, WS_DONE, WS_ERROR,
  };
  enum ReadState {
    RS_HEAD, RS_BODY, RS_DONE, RS_ERROR,
  };

  class AsyncWrite {
   public:
    AsyncWrite(TransferConnection* tc) : tc_(tc) { }

    void Perform() {
      tc_->outstanding_write_.reset();
      if (tc_->bev_) {
        tc_->OnWrite(tc_->bev_.get());
      }
    }

    TransferConnection* tc_;
  };

 public:
  TransferConnection(
      struct bufferevent* bev,
      int32_t task_id,
      TransferServer2* transfer_server);
  ~TransferConnection();

  //
  // Implement IBufferEventDelegate
  //
  virtual void OnRead(struct bufferevent* bev);
  virtual void OnWrite(struct bufferevent* bev);
  virtual void OnEvent(struct bufferevent* bev, short events);

 protected:
  void OnWriteMore(struct bufferevent* bev);
  void OnComplete(struct bufferevent* bev, err_t eno);
  void CreateBodyParser(HttpRequestHead* request_head);
  
 protected:
  static void LambdaAsyncWrite(void* ctx);
  
  ReadState read_state_;
  std::unique_ptr<HttpRequestHead> request_head_;
  std::unique_ptr<IHttpRequestParser> request_body_;

  WriteState write_state_;
  std::unique_ptr<IHttpResponseWriter> http_response_;

  int32_t task_id_;
  TransferServer2* transfer_server_;
  std::unique_ptr<struct bufferevent, decltype(bufferevent_free)*> bev_;

  std::shared_ptr<AsyncWrite> outstanding_write_;

  err_t return_code_;
  int32_t local_tree_id_;
  int32_t remote_tree_id_;
  bool need_unlock;
  bool need_unlock_download;
};



}  // namespace zs


#endif  // ZISYNC_KERNEL_LIBEVENT_TRANSFER_CONNECTION_H_
