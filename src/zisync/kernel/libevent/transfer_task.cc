/**
 * @file TransferTask.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief transfer task implmentation.
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

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/tar_writer.h"
#include "zisync/kernel/libevent/http_response.h"
#include "zisync/kernel/libevent/transfer_task.h"
#include "zisync/kernel/libevent/transfer_server2.h"

namespace zs {

TransferTask::TransferTask(TransferServer2* server, const std::string& uri,
                           int32_t local_tree_id, const std::string& remote_tree_uuid)
    : uri_(uri), return_code_(ZISYNC_SUCCESS)
    , write_state_(WS_HEAD), read_state_(RS_HEAD)
    , task_id_(0), transfer_server_(server), bev_(NULL, bufferevent_free)
    , local_tree_id_(local_tree_id), remote_tree_uuid_(remote_tree_uuid) {
  finish_event_.reset(new OsEvent);
  finish_event_->Initialize(false);

  response_head_.reset(new HttpResponseHead);
  // outstanding_write_.reset(new AsyncWrite(this));
}

TransferTask::~TransferTask() {
}

err_t TransferTask::Wait() {
    finish_event_->Wait();
    return return_code_;
}

void TransferTask::OnRead(struct bufferevent* bev) {
  err_t rc = ZISYNC_SUCCESS;

  while (read_state_ != RS_DONE && read_state_ != RS_ERROR) {
    switch (read_state_) {
      case RS_HEAD:  // process http response header
        rc = response_head_->ParseMore(bev);
        if (rc == ZISYNC_ERROR_AGAIN) {
          return;
        }
    
        if (rc == ZISYNC_SUCCESS) {
          CreateResponseBodyParser(response_head_->httpcode());
          assert(response_body_ != NULL);
          read_state_ = RS_BODY;
        } else {
          read_state_ = RS_ERROR;
        }

        break;

      case RS_BODY:  // process http body
        rc = response_body_->ParseMore(bev);
        if (rc == ZISYNC_ERROR_AGAIN) {
          return;
        }

        if (rc == ZISYNC_SUCCESS) {
            read_state_ = RS_DONE;
        } else {
          read_state_ = RS_ERROR;
        }
      
        break;

      default:  // RS_DONE or RS_ERROR
        // Should never got here
        assert(0);
    }
  }

  if (read_state_ == RS_ERROR) {
    // @XXX: record real error code
    transfer_server_->RemoveTask(task_id_);
    OnComplete(bev, ZISYNC_ERROR_GENERAL);
  } else if(read_state_ == RS_DONE) {
    transfer_server_->RemoveTask(task_id_);
    OnComplete(bev, ZISYNC_SUCCESS);
  }
  
}

void TransferTask::LambdaAsyncWrite(void* ctx) {
  auto ptr = reinterpret_cast<std::weak_ptr<AsyncWrite>*>(ctx);
  if (auto outstanding_write = ptr->lock()) {
    outstanding_write->Perform();
  } else {
    // printf("writing is out of date.");
  }
  delete ptr;
}

void TransferTask::OnWrite(struct bufferevent* bev) {
  if (!bev) {
    bev = bev_.get();
  } else {
    assert(bev == bev_.get());
  }

  struct evbuffer* output = bufferevent_get_output(bev);

  if (write_state_ == WS_HEAD || write_state_ == WS_BODY) {
    OnWriteMore(bev);
  }

  if (write_state_ == WS_ERROR) {
    transfer_server_->RemoveTask(task_id_);
    OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
  } else if (write_state_ == WS_DONE) {
    if (evbuffer_get_length(output) == 0) {
      evutil_socket_t fd = bufferevent_getfd(bev);
      evutil_shutdown_socket(fd, "w");
    }
  } else {
    // WS_HEAD || WS_BODY
    size_t length = evbuffer_get_length(output);
    if (outstanding_write_ == NULL && length < MAX_OUTSTANDING_SIZE) {
      outstanding_write_.reset(new AsyncWrite(this));
      auto weak_ptr =
          new std::weak_ptr<AsyncWrite>(outstanding_write_);
      // transfer_server_->evbase()->DispatchAsync(
      //     [](void* ctx) {
      //       auto ptr = reinterpret_cast<std::weak_ptr<AsyncWrite>*>(ctx);
      //       if (auto outstanding_write = ptr->lock()) {
      //         outstanding_write->Perform();
      //       } else {
      //         // printf("writing is out of date.");
      //       }
      //       delete ptr;
      //     }, weak_ptr, NULL);
      transfer_server_->evbase()->DispatchAsync(LambdaAsyncWrite, weak_ptr, NULL);
    }
  }
}
  
void TransferTask::OnWriteMore(struct bufferevent* bev) {
  err_t rc = ZISYNC_SUCCESS;
  struct evbuffer* output = bufferevent_get_output(bev);

  if (write_state_ == WS_HEAD) {
    // process http header
    write_state_ = WS_BODY;
    RequestHeadWriteAll(bev);
    assert(evbuffer_get_length(output) > 0);
  }
  else if (write_state_ == WS_BODY) {
    // process http body
    rc = RequestBodyWriteSome(bev);

    if (rc == ZISYNC_ERROR_AGAIN) {
      return;
    }

    if (rc == ZISYNC_SUCCESS) {
      write_state_ = WS_DONE;
    } else {
      write_state_ = WS_ERROR;
    }
    bufferevent_setwatermark(bev, EV_WRITE, 0, 0);
  } else {
    // Ooops! should never reach here.
    assert(false);
  }
}

void TransferTask::OnComplete(struct bufferevent* bev, err_t eno) {
  bev_.reset(); // free bev_
  return_code_ = eno;
  finish_event_->Signal();
}

void TransferTask::OnEvent(struct bufferevent* bev, short events) {
  err_t rc = ZISYNC_SUCCESS;
  //
  // connect success
  //
  if (events & BEV_EVENT_CONNECTED) {
    ZSLOG_INFO("connect success.");

    int rc;
    int bsize = 256 * 1024;
    evutil_socket_t s = bufferevent_getfd(bev);
    rc = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bsize, sizeof(bsize));
    if (rc == -1) {
      ZSLOG_ERROR("Set socket recv buf length(256K) fail: %s", OsGetLastErr());
    }
    rc = setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bsize, sizeof(bsize));
    if (rc == -1) {
      ZSLOG_ERROR("Set socket send buf length(256K) fail: %s", OsGetLastErr());
    }

    OnWrite(bev);
    return;
  }

  //
  // EOF event
  // 
  if (events & BEV_EVENT_EOF) {
    if (events & BEV_EVENT_READING) {
      ZSLOG_INFO("Connection read end has been closed.\n");
      struct evbuffer *input = bufferevent_get_input(bev);
      assert(evbuffer_get_length(input) == 0);

      if (read_state_ == RS_HEAD) {
        rc = response_head_->OnEOF(bev);
        if (rc == ZISYNC_SUCCESS) {
          read_state_ = RS_BODY;
          CreateResponseBodyParser(response_head_->httpcode());
          assert(response_body_ != NULL);
        } else {
          read_state_ = RS_ERROR;
        }
      }

      if (read_state_ == RS_BODY) {
        rc = response_body_->OnEOF(bev);
        if (rc == ZISYNC_SUCCESS) {
          read_state_ = RS_DONE;
        } else {
          read_state_ = RS_ERROR;
        }
      }
      
      if (read_state_ == RS_DONE) {
        transfer_server_->RemoveTask(task_id_);
        OnComplete(bev, ZISYNC_SUCCESS);
      } else if (read_state_ == RS_ERROR) {
        transfer_server_->RemoveTask(task_id_);
        OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
      }
    }

    if (events & BEV_EVENT_WRITING) {
      ZSLOG_INFO("Connection write end has been closed.\n");
      write_state_ = WS_ERROR;
      transfer_server_->RemoveTask(task_id_);
      OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
    }
  }

  if (events & BEV_EVENT_ERROR) {
    /* XXX win32 */
    ZSLOG_ERROR("Got an error on the connection: %s\n", strerror(errno));
    read_state_ = RS_ERROR;
    write_state_ = WS_ERROR;
    transfer_server_->RemoveTask(task_id_);
    OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
  }

  if (events & BEV_EVENT_TIMEOUT) {
    /* XXX win32 */
    ZSLOG_ERROR("Timeouton the connection");
    read_state_ = RS_ERROR;
    write_state_ = WS_ERROR;
    transfer_server_->RemoveTask(task_id_);
    OnComplete(bev, ZISYNC_ERROR_TIMEOUT);
  }  
  
}

}  // namespace zs
