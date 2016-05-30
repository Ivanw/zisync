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
#include "zisync/kernel/libevent/tar_download_task.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/http_response.h"

namespace zs {

DownloadTask2::DownloadTask2(ITaskMonitor* monitor,
                             TransferServer2* server,
                             int32_t local_tree_id,
                             const std::string& remote_tree_uuid,
                             const std::string& uri)
    : TransferTask(server, uri, local_tree_id, remote_tree_uuid)
    , total_size_(0), monitor_(monitor) {
}

DownloadTask2::~DownloadTask2() {
}

err_t DownloadTask2::AppendFile(
    const std::string& encode_path,
    const std::string &target_path, int64_t size) {
  encode_path_ = encode_path;
  target_path_ = target_path;
  if (encode_path != "/.zisync.meta") {
    total_size_ += size;
  }
  return ZISYNC_SUCCESS;
}

err_t DownloadTask2::Execute() {
  transfer_server_->ScheduleTask(this);
  return Wait();
}

void  DownloadTask2::CreateResponseBodyParser(int32_t http_code) {
  //
  // Wrapper TarParser to confirm IHttpResponseParser
  //
  class TarGetResponseParser : public IHttpResponseParser {
   public:
    TarGetResponseParser(ITarParserDataSource* data_source,
                         ITarParserDelegate* delegate) {
      tar_parser_.reset(new TarParser(data_source, delegate));
    }
      
    virtual err_t ParseMore(struct bufferevent* bev) {
      return tar_parser_->ParseMore(bev);
    }
    virtual err_t OnEOF(struct bufferevent* bev) {
      return tar_parser_->OnEOF(bev);
    }

   private:
    std::unique_ptr<TarParser> tar_parser_;
  };

  if (http_code == 200) {
    response_body_.reset(new TarGetResponseParser(this, this));
  } else {
    response_body_.reset(new ErrorResponseParser(http_code));
  }
}
void  DownloadTask2::RequestHeadWriteAll(struct bufferevent* bev) {
  ITreeAgent* tree_agent = transfer_server_->GetTreeAgent();
  
  struct evbuffer* output = bufferevent_get_output(bev);
  int total_files = 1;
  
  evbuffer_add_printf(output, "GET tar HTTP/1.1\r\n");
  evbuffer_add_printf(output, "ZiSync-Remote-Tree-Uuid:%s\r\n", remote_tree_uuid_.c_str());
  if (local_tree_id_ != -1) {
    local_tree_uuid_ = tree_agent->GetTreeUuid(local_tree_id_);
    evbuffer_add_printf(output, "ZiSync-Local-Tree-Uuid:%s\r\n", local_tree_uuid_.c_str());
  }
  evbuffer_add_printf(output, "ZiSync-Total-Size:%" PRId64 "\r\n", total_size_);
  evbuffer_add_printf(output, "ZiSync-Total-Files:%d\r\n", total_files);
  evbuffer_add_printf(output, "\r\n");
}
err_t DownloadTask2::RequestBodyWriteSome(struct bufferevent* bev) {
  TarGetFileList file_list;
  struct evbuffer* output = bufferevent_get_output(bev);
  
  file_list.add_relative_paths(encode_path_);
  
  std::string blob = file_list.SerializeAsString();

  evbuffer_add(output, blob.data(), blob.size());
  return ZISYNC_SUCCESS;
}

void DownloadTask2::OnComplete(struct bufferevent* bev, err_t eno) {
  response_body_.reset();
  TransferTask::OnComplete(bev, eno);
  
#ifdef __APPLE__
  
  NSString *taskDoneDetail = [NSString stringWithUTF8String:encode_path_.c_str()];
  NSDictionary *info = @{kTaskType:[NSNumber numberWithInteger:TaskTypeDownload]
    , kTaskReturnCode:[NSNumber numberWithInteger:eno]
    , kTaskCompleteNotificationDetailString:taskDoneDetail};
  [[NSNotificationCenter defaultCenter] postNotificationName:kTaskCompleteNotification
                                                      object:nil
                                                    userInfo:info];
#endif
}


void DownloadTask2::OnTarWillTransfer(const std::string& tmp_dir) {
  
}
void DownloadTask2::OnTarDidTransfer(const std::string& tmp_dir) {
  
}


void DownloadTask2::OnFileWillTransfer(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("begin transfer file: %s", encode_path.c_str());
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileTransfer(encode_path);
    }
  }
}
  
void DownloadTask2::OnFileDidTransfered(
      const std::string& real_path,
      const std::string& encode_path,
      const std::string& sha1) {
  ZSLOG_INFO("end transfer file: [sha1: %s] %s",
             sha1.c_str(), encode_path.c_str());
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileTransfered(1);
    }
  }
}
void DownloadTask2::OnFileDidSkiped(
      const std::string& real_path,
      const std::string& encode_path) {
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileSkiped(1);
    }
  }
}
  
void DownloadTask2::OnByteDidTransfered(
      const std::string& real_path,
      const std::string& encode_path, int32_t nbytes) {
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnByteTransfered(nbytes);
    }
  }
}

std::string DownloadTask2::GetRealPath(const std::string& encode_path) {
  return target_path_;
}

std::string DownloadTask2::GetAlias(const std::string& encode_path) {
  return string();
}

}  // namespace zs
