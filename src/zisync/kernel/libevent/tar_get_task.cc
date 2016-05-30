/**
 * @file tar_get_task.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar get task.
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
#include "zisync/kernel/libevent/transfer_server2.h"
#include "zisync/kernel/libevent/tar_get_task.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/http_response.h"

namespace zs {

TarGetTask2::TarGetTask2(ITaskMonitor* monitor,
                         TransferServer2* server,
                         int32_t local_tree_id,
                         const std::string& remote_tree_uuid,
                         const std::string& uri)
    : TransferTask(server, uri, local_tree_id, remote_tree_uuid)
    , total_size_(0)
    , get_handler_(NULL)
    , monitor_(monitor) {
}

TarGetTask2::~TarGetTask2() {
}

void TarGetTask2::SetHandler(IGetHandler *handler) {
  get_handler_ = handler;
}

err_t TarGetTask2::AppendFile(
    const std::string& encode_path, int64_t size) {
  encode_path_vector_.push_back(encode_path);
  if (encode_path != "/.zisync.meta") {
    total_size_ += size;
  }

  return ZISYNC_SUCCESS;
}

err_t TarGetTask2::Execute(const std::string& tmp_dir) {
  tmp_dir_ = tmp_dir;
  transfer_server_->ScheduleTask(this);
  return Wait();
}

void  TarGetTask2::CreateResponseBodyParser(int32_t http_code) {
  //
  // Wrapper TarParser to confirm IHttpResponseParser
  //
  class TarGetResponseParser : public IHttpResponseParser
      , public ITarParserDataSource {
   public:
    TarGetResponseParser(const std::string& tmproot,
                         ITarParserDelegate* delegate)
        : tmp_dir_(tmproot), delegate_(delegate) {
      tar_parser_.reset(new TarParser(this, delegate));
      if (delegate_) {
        delegate_->OnTarWillTransfer(tmp_dir_);
      }
    }
    virtual ~TarGetResponseParser() {
      // if (delegate_) {
      //   delegate_->OnTarDidTransfer(tmp_dir_);
      // }
    }
      
    virtual err_t ParseMore(struct bufferevent* bev) {
      return tar_parser_->ParseMore(bev);
    }
    virtual err_t OnEOF(struct bufferevent* bev) {
      return tar_parser_->OnEOF(bev);
    }

    virtual std::string GetRealPath(const std::string& encode_path) {
      std::string path = tmp_dir_;
      OsPathAppend(&path, encode_path);
      return path;
    }
    virtual std::string GetAlias(const std::string& encode_path) {
      return string();
    }
   private:
    std::string tmp_dir_;
    std::unique_ptr<TarParser> tar_parser_;
    ITarParserDelegate *delegate_;
  };

  if (http_code == 200) {
    response_body_.reset(new TarGetResponseParser(tmp_dir_, this));
  } else {
    response_body_.reset(new ErrorResponseParser(http_code));
  }
}
void  TarGetTask2::RequestHeadWriteAll(struct bufferevent* bev) {
  ITreeAgent* tree_agent = transfer_server_->GetTreeAgent();
  std::string local_tree_uuid = tree_agent->GetTreeUuid(local_tree_id_);
  
  struct evbuffer* output = bufferevent_get_output(bev);
  int total_files = (int) encode_path_vector_.size();
  
  evbuffer_add_printf(output, "GET tar HTTP/1.1\r\n");
  evbuffer_add_printf(output, "ZiSync-Remote-Tree-Uuid:%s\r\n", remote_tree_uuid_.c_str());
  evbuffer_add_printf(output, "ZiSync-Local-Tree-Uuid:%s\r\n", local_tree_uuid.c_str());
  evbuffer_add_printf(output, "ZiSync-Total-Size:%" PRId64 "\r\n", total_size_);
  evbuffer_add_printf(output, "ZiSync-Total-Files:%d\r\n", total_files);
  evbuffer_add_printf(output, "\r\n");
}
err_t TarGetTask2::RequestBodyWriteSome(struct bufferevent* bev) {
  TarGetFileList file_list;
  struct evbuffer* output = bufferevent_get_output(bev);
  
  for (auto it = encode_path_vector_.begin();
       it != encode_path_vector_.end(); ++it) {
      file_list.add_relative_paths(*it);
  }
  
  std::string blob = file_list.SerializeAsString();

  evbuffer_add(output, blob.data(), blob.size());
  return ZISYNC_SUCCESS;
}

void TarGetTask2::OnComplete(struct bufferevent* bev, err_t eno) {
  response_body_.reset();
  TransferTask::OnComplete(bev, eno);
  
#ifdef __APPLE__
  
  NSDictionary *info = @{kTaskType:[NSNumber numberWithInteger:TaskTypeSync]
    , kTaskReturnCode:[NSNumber numberWithInteger:eno]};
  [[NSNotificationCenter defaultCenter] postNotificationName:kTaskCompleteNotification
                                                      object:nil
                                                    userInfo:info];
#endif
}


void TarGetTask2::OnTarWillTransfer(const std::string& tmp_dir) {

}

void TarGetTask2::OnTarDidTransfer(const std::string& tmp_dir) {
  
}


void TarGetTask2::OnFileWillTransfer(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("begin transfer file: %s", encode_path.c_str());
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileTransfer(encode_path);
    }
  }
}
  
void TarGetTask2::OnFileDidTransfered(
      const std::string& real_path,
      const std::string& encode_path,
      const std::string& sha1) {
  ZSLOG_INFO("end transfer file: [sha1: %s] %s",
             sha1.c_str(), encode_path.c_str());
  if (get_handler_) {
    get_handler_->OnHandleFile(encode_path, sha1);
  }
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileTransfered(1);
    }
  }
}
void TarGetTask2::OnFileDidSkiped(
      const std::string& real_path,
      const std::string& encode_path) {
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileSkiped(1);
    }
  }
}

void TarGetTask2::OnByteDidTransfered(
    const std::string& real_path,
    const std::string& encode_path, int32_t nbytes) {
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnByteTransfered(nbytes);
    }
  }
}


}  // namespace zs
