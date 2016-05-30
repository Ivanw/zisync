/**
 * @file transfer_connection.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief connection accepted by libevent.
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

#include <tuple>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/http_request.h"
#include "zisync/kernel/libevent/http_response.h"
#include "zisync/kernel/libevent/transfer_connection.h"
#include "zisync/kernel/libevent/transfer_server2.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/inner_request.h"

namespace zs {

static void LambdaRetrySync(void *ctx);

TransferConnection::TransferConnection(
    struct bufferevent* bev, int32_t task_id, TransferServer2* server)
    : read_state_(RS_HEAD), write_state_(WS_HEAD)
    , task_id_(task_id), transfer_server_(server)
    , bev_(bev, bufferevent_free), return_code_(ZISYNC_SUCCESS)
    , local_tree_id_(-1), remote_tree_id_(-1)
    , need_unlock(false), need_unlock_download(false){
  request_head_.reset(new HttpRequestHead);
  // outstanding_write_.reset(new AsyncWrite(this));
}

TransferConnection::~TransferConnection() {
}

void TransferConnection::OnRead(struct bufferevent* bev) {
  err_t rc = ZISYNC_SUCCESS;

  while (read_state_ != RS_DONE && read_state_ != RS_ERROR) {
    switch (read_state_) {
      case RS_HEAD:  // process http header
        rc = request_head_->OnRead(bev);
        if (rc == ZISYNC_ERROR_AGAIN) {
          return;
        }

        if (rc == ZISYNC_SUCCESS) {
          CreateBodyParser(request_head_.get());
          if (request_body_ != NULL) {
            read_state_ = RS_BODY;
          } else {
            read_state_ = RS_ERROR;
            http_response_.reset(new ErrorResponseWriter(401));
          }
        } else {
          read_state_ = RS_ERROR;
          http_response_.reset(ErrorResponseWriter::GetByErr(rc));
        }

        break;

      case RS_BODY:  // process http body
        rc = request_body_->ParseMore(bev);
        if (rc == ZISYNC_ERROR_AGAIN) {
          return;
        }

        if (rc == ZISYNC_SUCCESS) {
          http_response_.reset(request_body_->CreateResponse(request_head_.get()));
          if (http_response_ != NULL) {
            read_state_ = RS_DONE;
          } else {
            read_state_ = RS_ERROR;
            http_response_.reset(new ErrorResponseWriter(500));
          }
        } else {
          read_state_ = RS_ERROR;
          http_response_.reset(ErrorResponseWriter::GetByErr(rc));
        }

        break;

      default:  // RS_FINISH or RS_ERROR
        // Should never got here
        assert(0);
    }
  }

  if (read_state_ == RS_ERROR || read_state_ == RS_DONE) {
    bufferevent_disable(bev, EV_READ);
  }

  if (http_response_) {
    // tigger on write
    bufferevent_enable(bev, EV_WRITE);
    OnWrite(bev);
  }
}

void TransferConnection::LambdaAsyncWrite(void* ctx) {
  auto ptr = reinterpret_cast<std::weak_ptr<AsyncWrite>*>(ctx);
  if (auto outstanding_write = ptr->lock()) {
    outstanding_write->Perform();
  } else {
    // printf("on write out of date ======\n");
  }
  delete ptr;
}

void TransferConnection::OnWrite(struct bufferevent* bev) {
  // err_t rc = ZISYNC_SUCCESS;
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
    OnComplete(bev, ZISYNC_ERROR_TAR);
    transfer_server_->RemoveConnect(task_id_);
  }
  else if (write_state_ == WS_DONE) {
    if (evbuffer_get_length(output) == 0) {
      OnComplete(bev, ZISYNC_SUCCESS);
      transfer_server_->RemoveConnect(task_id_);
    }
  }
  else {
    // WS_HEAD || WS_BODY
    // assert(rc == ZISYNC_ERROR_AGAIN);

    // Reschedule OnWrite if outstanding bytes not enough.
    //
    // NOTE: if evbuffer_get_length == 0 and we do not reschedule
    // OnWrite(), it will never get called again.
    size_t length = evbuffer_get_length(output);
    if (outstanding_write_ == NULL && length < MAX_OUTSTANDING_SIZE) {
      outstanding_write_.reset(new AsyncWrite(this));
      auto weak_ptr = new std::weak_ptr<AsyncWrite>(outstanding_write_);
      // transfer_server_->evbase()->DispatchAsync(
      //     [](void* ctx) {
      //       auto ptr = reinterpret_cast<std::weak_ptr<AsyncWrite>*>(ctx);
      //       if (auto outstanding_write = ptr->lock()) {
      //         outstanding_write->Perform();
      //       } else {
      //         // printf("on write out of date ======\n");
      //       }
      //       delete ptr;
      //     }, weak_ptr, NULL);
      transfer_server_->evbase()->DispatchAsync(LambdaAsyncWrite, weak_ptr, NULL);
    }
  }
}

void TransferConnection::OnWriteMore(struct bufferevent* bev) {
  err_t rc = ZISYNC_SUCCESS;
  struct evbuffer* output = bufferevent_get_output(bev);

  if (write_state_ == WS_HEAD) {
    // process http header
    write_state_ = WS_BODY;
    http_response_->HeadWriteAll(bev);
    assert(evbuffer_get_length(output) > 0);
  }
  else if (write_state_ == WS_BODY) {
    // process http body
    rc = http_response_->BodyWriteSome(bev);

    if (rc == ZISYNC_ERROR_AGAIN) {
      return;
    }

    if (rc == ZISYNC_SUCCESS) {
      write_state_ = WS_DONE;
    } else {
      write_state_ = WS_ERROR;
    }
    bufferevent_setwatermark(bev, EV_WRITE, 0, 0);
  }
  else {
    // Ooops! should never reach here.
    assert(false);
  }
}

void TransferConnection::OnEvent(struct bufferevent* bev, short events) {
  err_t rc = ZISYNC_SUCCESS;
  //
  // connect success
  //
  if (events & BEV_EVENT_CONNECTED) {
    ZSLOG_INFO("connect success.");
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
        rc = request_head_->OnEOF(bev);
        if (rc == ZISYNC_SUCCESS) {
          CreateBodyParser(request_head_.get());
          if (request_body_ != NULL) {
            read_state_ = RS_BODY;
          } else {
            read_state_ = RS_ERROR;
            http_response_.reset(new ErrorResponseWriter(401));
          }
        } else {
          read_state_ = RS_ERROR;
          http_response_.reset(new ErrorResponseWriter(401));
        }
      }

      if (read_state_ == RS_BODY) {
        rc = request_body_->OnEOF(bev);
        if (rc == ZISYNC_SUCCESS) {
          http_response_.reset(request_body_->CreateResponse(request_head_.get()));
          if (http_response_ != NULL) {
            read_state_ = RS_DONE;
          } else {
            read_state_ = RS_ERROR;
            http_response_.reset(new ErrorResponseWriter(500));
          }
        } else {
          read_state_ = RS_ERROR;
          http_response_.reset(new ErrorResponseWriter(401));
        }
      }

      if (read_state_ == RS_DONE || read_state_ == RS_ERROR) {
        assert(http_response_ != NULL);
        if (http_response_) {
          bufferevent_enable(bev, EV_WRITE);
          OnWrite(bev);
        }
      }
    }

    if (events & BEV_EVENT_WRITING) {
      ZSLOG_INFO("Connection write end has been closed.\n");
      write_state_ = WS_ERROR;
      OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
      transfer_server_->RemoveConnect(task_id_);
    }
  }

  if (events & BEV_EVENT_ERROR) {
    /* XXX win32 */
    ZSLOG_ERROR("Got an error on the connection: %s\n", strerror(errno));
    read_state_ = RS_ERROR;
    write_state_ = WS_ERROR;
    OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
    transfer_server_->RemoveConnect(task_id_);
  }

  if (events & BEV_EVENT_TIMEOUT) {
    /* XXX win32 */
    ZSLOG_ERROR("Timeouton the connection");
    read_state_ = RS_ERROR;
    write_state_ = WS_ERROR;
    OnComplete(bev, ZISYNC_ERROR_LIBEVENT);
    transfer_server_->RemoveConnect(task_id_);
  }  
}

void TransferConnection::OnComplete(struct bufferevent* bev, err_t eno) {
  bev_.reset();
  if (need_unlock) {
    ITreeAgent* tree_agent = transfer_server_->GetTreeAgent();
    tree_agent->Unlock(local_tree_id_, remote_tree_id_);
  }
  if (need_unlock_download) {
    ITreeAgent* tree_agent = transfer_server_->GetTreeAgent();
    tree_agent->Unlock(local_tree_id_, local_tree_id_);
  }
  return_code_ = eno;
  
#ifdef __APPLE__
  
  NSDictionary *info = @{kTaskType:[NSNumber numberWithInteger:TaskTypeTransfer]
    , kTaskReturnCode:[NSNumber numberWithInteger:eno]};
  [[NSNotificationCenter defaultCenter] postNotificationName:kTaskCompleteNotification
                                                      object:nil
                                                    userInfo:info];
#endif
}


class TarPutResponseWriter : public IHttpResponseWriter {
 public:
  TarPutResponseWriter() { }
  virtual ~TarPutResponseWriter() { }

  virtual void  HeadWriteAll(struct bufferevent* bev) {
    struct evbuffer* output = bufferevent_get_output(bev);
    evbuffer_add_printf(output, "HTTP/1.1 200 OK\r\n");
    evbuffer_add_printf(output, "\r\n");
  }

  virtual err_t BodyWriteSome(struct bufferevent* bev) {
    return ZISYNC_SUCCESS;
  }
};

static err_t ParseMetaFile(
    const std::string &meta_file, MsgPushSyncMeta *meta_parse_data) {
  OsFile file;
  OsFileStat file_stat;
  int ret = OsStat(meta_file, string(), &file_stat);
  if (ret != 0) {
    ZSLOG_ERROR("OsStat(%s) fail : %s", meta_file.c_str(),
                OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  if (file_stat.length <= 0) {
    ZSLOG_ERROR("Invalid MetaFile length(%" PRId64")", file_stat.length);
    return ZISYNC_ERROR_OS_IO;
  }
  ret = file.Open(meta_file.c_str(), string(), "rb");
  if (ret != 0) {
    ZSLOG_ERROR("Open MetaFile(%s) fail : %s", meta_file.c_str(),
                OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  string data;
  size_t len = file.ReadWholeFile(&data);
  assert(len == data.size());
  if (static_cast<int64_t>(len) != file_stat.length) {
    ZSLOG_ERROR("Read from (%s) fail, expected len(%" PRId64 ") != "
                "len(%zd)", meta_file.c_str(), file_stat.length, len);
    return ZISYNC_ERROR_OS_IO;
  }
  if (!meta_parse_data->ParseFromString(data)) {
    ZSLOG_ERROR("Parse Protobuf from MetaFile(%s) fail.", 
                meta_file.c_str());
    return zs::ZISYNC_ERROR_INVALID_MSG;
  }

  return ZISYNC_SUCCESS;
}

void TransferConnection::CreateBodyParser(HttpRequestHead* request_head) {
  //
  // Wrapper TarParser to confirm IHttpRequestParser
  //
  class TarPutRequestParser
      : public IHttpRequestParser
      , public ITarParserDataSource
      , public ITarParserDelegate {
   public:
    TarPutRequestParser(HttpRequestHead* head) :
        local_tree_uuid_(head->remote_tree_uuid()),
        remote_tree_uuid_(head->local_tree_uuid()) {
          ITreeAgent* tree_agent = GetTransferServer2()->GetTreeAgent();

          tmp_dir_ = tree_agent->GetNewTmpDir(head->remote_tree_uuid());
          monitor_.reset(
              tree_agent->CreateTaskMonitor(
                  TASK_TYPE_GET,
                  head->remote_tree_uuid(), // NOTE: this is local_tree_uuid
                  head->local_tree_uuid(),  // NOTE: this is remote_tree_uuid
                  head->total_files(),
                  head->total_bytes()));
          body_parser_.reset(new TarParser(this, this));

          OnTarWillTransfer(tmp_dir_);
        }
    virtual ~TarPutRequestParser() { 
      body_parser_.reset();
      OnTarDidTransfer(tmp_dir_);
    }

    err_t ParseMore(struct bufferevent* bev) {
      return body_parser_->ParseMore(bev);
    }

    err_t OnEOF(struct bufferevent* bev) {
      return body_parser_->OnEOF(bev);
    }

    IHttpResponseWriter* CreateResponse(HttpRequestHead* http_head) {
      return new TarPutResponseWriter();
    }

    //
    // Implement ITarParserDataSource
    //
    std::string GetRealPath(const std::string& encode_path) {
      std::string path = tmp_dir_;
      OsPathAppend(&path, encode_path);
      return path;
    }
    //
    // Implement ITarParserDelegate
    //
    //
    virtual void OnTarWillTransfer(const std::string& tmp_dir) {
      // ZSLOG_INFO("begin transfer file: %s", encode_path.c_str());
      put_handler_.reset(
          GetTransferServer2()->GetTreeAgent()->CreatePutHandler(tmp_dir));
    }

    virtual void OnTarDidTransfer(const std::string& tmp_dir) {
      put_handler_.reset(NULL);
    }

    virtual void OnFileWillTransfer(
        const std::string& real_path,
        const std::string& encode_path) {
      ZSLOG_INFO("begin transfer file: %s", encode_path.c_str());
      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnFileTransfer(encode_path);
        }
      }
    }

    virtual void OnFileDidTransfered(
        const std::string& real_path,
        const std::string& encode_path,
        const std::string& sha1) {
      ZSLOG_INFO("end transfer file: [sha1: %s] %s",
                 sha1.c_str(), encode_path.c_str());
      if (put_handler_) {
        put_handler_->OnHandleFile(encode_path, real_path, sha1);
      }

      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnFileTransfered(1);
        } else {
          MsgPushSyncMeta meta_parse_data;
          std::string meta_file_path =
              tmp_dir_ + "/" + SYNC_FILE_TASKS_META_FILE;
          if (ParseMetaFile(
                  meta_file_path, &meta_parse_data) != ZISYNC_SUCCESS) {
            ZSLOG_ERROR("Parse meta file(%s) in transfer fail.",
                        meta_file_path.c_str());
            return;
          }

          ITreeAgent *tree_agent = GetTransferServer2()->GetTreeAgent();
          std::string local_tree_root =
              tree_agent->GetTreeRoot(local_tree_uuid_);
          if (local_tree_root.empty()) {
            ZSLOG_ERROR("Get local tree root by uuid(%s) fail.",
                        local_tree_uuid_.c_str());
            return;
          }

          std::string remote_tree_root =
              tree_agent->GetTreeRoot(remote_tree_uuid_);
          if (remote_tree_root.empty()) {
            ZSLOG_ERROR("Get remote tree root by uuid(%s) fail.",
                        remote_tree_uuid_.c_str());
            return;
          }

          const MsgRemoteMeta &remote_meta = meta_parse_data.remote_meta();
          for (int i = 0; i < remote_meta.stats_size(); i++) {
            if (remote_meta.stats(i).status() == FS_NORMAL) {
              std::string local_path = local_tree_root;
              std::string remote_path = remote_tree_root;
              std::string file_name = remote_meta.stats(i).path();

              OsPathAppend(&local_path, file_name);
              OsPathAppend(&remote_path, file_name);

              monitor_->AppendFile(local_path, remote_path, file_name,
                                   remote_meta.stats(i).length());
            }
          }
        }
      }
    }

    virtual void OnFileDidSkiped(
        const std::string& real_path,
        const std::string& encode_path) {
      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnFileSkiped(1);
        }
      }           
    }

    virtual void OnByteDidTransfered(
        const std::string& real_path,
        const std::string& encode_path,
        int32_t nbytes) {
      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnByteTransfered(nbytes);
        }
      }
    }

    std::string GetAlias(const std::string& encode_path) {
      return string();
    }

   private:
    std::string tmp_dir_;
    std::unique_ptr<TarParser> body_parser_;
    std::unique_ptr<IPutHandler> put_handler_;
    std::unique_ptr<ITaskMonitor> monitor_;

    std::string local_tree_uuid_;
    std::string remote_tree_uuid_;
  };

  class TarUploadRequestParser
      : public IHttpRequestParser
      , public ITarParserDataSource
      , public ITarParserDelegate {
   public:
    TarUploadRequestParser(HttpRequestHead* head) :
        local_tree_uuid_(head->remote_tree_uuid()),
        remote_tree_uuid_(head->local_tree_uuid()) {
          ITreeAgent* tree_agent = GetTransferServer2()->GetTreeAgent();
          tree_uuid_ = head->remote_tree_uuid();
          tmp_dir_ = tree_agent->GetNewTmpDir(tree_uuid_);
          monitor_.reset(
              tree_agent->CreateTaskMonitor(
                  TASK_TYPE_GET,
                  head->remote_tree_uuid(), // NOTE: this is local_tree_uuid
                  head->local_tree_uuid(),  // NOTE: this is remote_tree_uuid
                  head->total_files(),
                  head->total_bytes()));
          body_parser_.reset(new TarParser(this, this));
          put_handler_.reset(
              tree_agent->CreateUploadHandler(tree_uuid_, tmp_dir_));
          // assert(put_handler_);

          OnTarWillTransfer(tmp_dir_);
    }
    virtual ~TarUploadRequestParser() { 
      put_handler_.reset(NULL);
    }

    err_t ParseMore(struct bufferevent* bev) {
      return body_parser_->ParseMore(bev);
    }

    err_t OnEOF(struct bufferevent* bev) {
      return body_parser_->OnEOF(bev);
    }

    IHttpResponseWriter* CreateResponse(HttpRequestHead* http_head) {
      return new TarPutResponseWriter();
    }
    //
    // Implement ITarParserDataSource
    //
    std::string GetRealPath(const std::string& encode_path) {
      std::string path = tmp_dir_;
      OsPathAppend(&path, encode_path);
      return path;
    }
    std::string GetAlias(const std::string& encode_path) {
      return string();
    }
    //
    // Implement ITarParserDelegate
    //
    virtual void OnTarWillTransfer(const std::string& tmp_dir) {
    }
    virtual void OnTarDidTransfer(const std::string& tmp_dir) {
    }

    virtual void OnFileWillTransfer(
        const std::string& real_path,
        const std::string& encode_path) {
      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnFileTransfer(encode_path);
        }
      }
    }

    virtual void OnFileDidTransfered(
        const std::string& real_path,
        const std::string& encode_path,
        const std::string& sha1) {
      if (put_handler_) {
        // assert(put_handler_);
        put_handler_->OnHandleFile(encode_path, real_path, sha1);
      }
      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnFileTransfered(1);
        } else {
          MsgPushSyncMeta meta_parse_data;
          std::string meta_file_path =
              tmp_dir_ + "/" + SYNC_FILE_TASKS_META_FILE;
          if (ParseMetaFile(
                  meta_file_path, &meta_parse_data) != ZISYNC_SUCCESS) {
            ZSLOG_ERROR("Parse meta file(%s) in transfer fail.",
                        meta_file_path.c_str());
            return;
          }

          ITreeAgent *tree_agent = GetTransferServer2()->GetTreeAgent();
          std::string local_tree_root =
              tree_agent->GetTreeRoot(local_tree_uuid_);
          if (local_tree_root.empty()) {
            ZSLOG_ERROR("Get local tree root by uuid(%s) fail.",
                        local_tree_uuid_.c_str());
            return;
          }

          std::string remote_tree_root =
              tree_agent->GetTreeRoot(remote_tree_uuid_);
          if (remote_tree_root.empty()) {
            ZSLOG_ERROR("Get remote tree root by uuid(%s) fail.",
                        remote_tree_uuid_.c_str());
            return;
          }

          const MsgRemoteMeta &remote_meta = meta_parse_data.remote_meta();
          for (int i = 0; i < remote_meta.stats_size(); i++) {
            if (remote_meta.stats(i).status() == FS_NORMAL) {
              std::string local_path = local_tree_root;
              std::string remote_path = remote_tree_root;
              std::string file_name = remote_meta.stats(i).path();

              OsPathAppend(&local_path, file_name);
              OsPathAppend(&remote_path, file_name);

              monitor_->AppendFile(local_path, remote_path, file_name,
                                   remote_meta.stats(i).length());
            }
          }
        }
      }
    }

    virtual void OnFileDidSkiped(
        const std::string& real_path,
        const std::string& encode_path) {
      if (monitor_) {
        monitor_->OnFileSkiped(1);
      }           
    }

    virtual void OnByteDidTransfered(
        const std::string& real_path,
        const std::string& encode_path,
        int32_t nbytes) {
      if (monitor_) {
        if (encode_path != "/.zisync.meta") {
          monitor_->OnByteTransfered(nbytes);
        }
      }
    }

   private:
    std::string tree_uuid_;
    std::string tmp_dir_;
    std::unique_ptr<TarParser> body_parser_;
    std::unique_ptr<IPutHandler> put_handler_;
    std::unique_ptr<ITaskMonitor> monitor_;

    std::string local_tree_uuid_;
    std::string remote_tree_uuid_;
  };

  ITreeAgent* tree_agent = GetTransferServer2()->GetTreeAgent();
  //
  // NOTE:
  //     remote_tree_uuid => local_tree_id_
  //     local_tree_uuid => remote_tree_id_
  // 
  local_tree_id_ = tree_agent->GetTreeId(request_head_->remote_tree_uuid());
  remote_tree_id_ = tree_agent->GetTreeId(request_head_->local_tree_uuid());

  //
  // Try Lock (local_tree_id, remote_tree_id)
  //
  if (remote_tree_id_ != -1) {
    assert(local_tree_id_ != -1);
    need_unlock = tree_agent->TryLock(local_tree_id_, remote_tree_id_);
    if (need_unlock == false) {
      ZSLOG_WARNING(
          "TryLock fail(local_tree: %s, remote_tree: %s)",
          request_head_->local_tree_uuid().c_str(),
          request_head_->remote_tree_uuid().c_str());
      return_code_ = ZISYNC_ERROR_GENERAL;
      if (request_head_->local_tree_uuid() > request_head_->remote_tree_uuid()) {
        struct timeval tv = {0, 100000};
        auto ctx = new std::tuple<int32_t, int32_t>(local_tree_id_, remote_tree_id_);
        transfer_server_->evbase()->DispatchAsync(LambdaRetrySync, ctx, &tv);
      }
      return;
    }
  }

  if (request_head->method() == "PUT") {
    need_unlock_download = tree_agent->TryLock(local_tree_id_, local_tree_id_);
    if (!need_unlock_download) {
      ZSLOG_WARNING(
          "TryLock fail(local_tree: %s)"
          , request_head_->local_tree_uuid().c_str());
      return_code_ = ZISYNC_ERROR_GENERAL;
      return;
    }
    if (request_head->format() == "tar") {
      request_body_.reset(new TarPutRequestParser(request_head_.get()));
    } else if (request_head->format() == "tar/upload") {
      request_body_.reset(new TarUploadRequestParser(request_head_.get()));
    }
  } else if (request_head->method() == "GET") {
    if (request_head->format() == "tar") {
      std::string treeroot =
          tree_agent->GetTreeRoot(request_head->remote_tree_uuid());
      request_body_.reset(new TarGetRequestParser(
              treeroot, request_head->remote_tree_uuid()));
    }
  }
}

static void LambdaRetrySync(void *ctx) {
  int32_t local, remote;
  std::tie(local, remote) = *((std::tuple<int32_t, int32_t>*)ctx);
  IssueSync(local, remote);
  delete (std::tuple<int32_t, int32_t>*)ctx;
}


}  // namespace zs
