/**
 * @file tar_upload_task.cc
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

#include <algorithm>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/transfer_server2.h"
#include "zisync/kernel/libevent/tar_upload_task.h"
#include "zisync/kernel/libevent/tar_writer.h"
#include "zisync/kernel/libevent/http_response.h"

namespace zs {

UploadTask2::UploadTask2(ITaskMonitor* monitor,
                         TransferServer2* server,
                         int32_t local_tree_id,
                         const std::string& remote_tree_uuid,
                         const std::string& uri)
    : TransferTask(server, uri, local_tree_id, remote_tree_uuid)
    , total_size_(0), current_index_(0)
    , monitor_(monitor) {
  body_writer_.reset(new TarWriter(this, this));
}

UploadTask2::~UploadTask2() {
}

err_t UploadTask2::AppendFile(
    const std::string& real_path,
    const std::string& encode_path, int64_t size) {
  size_vector_.push_back(size);
  real_path_vector_.push_back(real_path);
  encode_path_vector_.push_back(encode_path);
  total_size_ += size;

  return ZISYNC_SUCCESS;
}

err_t UploadTask2::Execute() {
  transfer_server_->ScheduleTask(this);
  return Wait();
}

void  UploadTask2::CreateResponseBodyParser(int32_t http_code) {
  if (http_code == 200) {
    response_body_.reset(new EmptyResponseParser);
  } else {
    response_body_.reset(new ErrorResponseParser(http_code));
  }
}

void  UploadTask2::RequestHeadWriteAll(struct bufferevent* bev) {
  ITreeAgent* tree_agent = transfer_server_->GetTreeAgent();

  int total_files = std::count_if (
      encode_path_vector_.begin(),
      encode_path_vector_.end(), [](const std::string& s) {
        return s != "/.zisync.meta";
      });
  struct evbuffer* output = bufferevent_get_output(bev);
  
  evbuffer_add_printf(output, "PUT tar/upload HTTP/1.1\r\n");
  evbuffer_add_printf(output, "ZiSync-Remote-Tree-Uuid:%s\r\n", remote_tree_uuid_.c_str());

  if (local_tree_id_ != -1) {
    std::string local_tree_uuid = tree_agent->GetTreeUuid(local_tree_id_);
    evbuffer_add_printf(output, "ZiSync-Local-Tree-Uuid:%s\r\n", local_tree_uuid.c_str());
  }
  evbuffer_add_printf(output, "ZiSync-Total-Size:%" PRId64 "\r\n", total_size_);
  evbuffer_add_printf(output, "ZiSync-Total-Files:%d\r\n", total_files);
  evbuffer_add_printf(output, "\r\n");
}

err_t UploadTask2::RequestBodyWriteSome(struct bufferevent* bev) {
  return body_writer_->WriteSome(bev);
}

bool UploadTask2::EnumNext(
    std::string* real_path, std::string* encode_path, std::string* alias, 
    int64_t* size) {
  if (current_index_ >= real_path_vector_.size()) {
    return false;
  }

  *real_path = real_path_vector_[current_index_];
  *encode_path = encode_path_vector_[current_index_];
  *alias = string();
  *size = size_vector_[current_index_];
  ++current_index_;

  return true;
}

void UploadTask2::OnComplete(struct bufferevent* bev, err_t eno) {
  body_writer_.reset();
  TransferTask::OnComplete(bev, eno);
  
#ifdef __APPLE__
  
  NSDictionary *info = @{kTaskType:[NSNumber numberWithInteger:TaskTypeUpload]
    , kTaskReturnCode:[NSNumber numberWithInteger:eno]};
  [[NSNotificationCenter defaultCenter] postNotificationName:kTaskCompleteNotification
                                                      object:nil
                                                    userInfo:info];
#endif
}

void UploadTask2::OnFileWillTransfer(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("begin transfer file: %s", encode_path.c_str());
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileTransfer(encode_path);
    }
  }
}
  
void UploadTask2::OnFileDidTransfered(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("end transfer file: %s", encode_path.c_str());
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileTransfered(1);
    }
  }
}
void UploadTask2::OnFileDidSkiped(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("end transfer file: %s", encode_path.c_str());
  if (monitor_) {
    if (encode_path != "/.zisync.meta") {
      monitor_->OnFileSkiped(1);
    }
  }
}
  
void UploadTask2::OnByteDidTransfered(int32_t nbytes) {
  if (monitor_) {
    monitor_->OnByteTransfered(nbytes);
  }
}
void UploadTask2::OnByteDidSkiped(int32_t nbytes) {
  if (monitor_) {
    monitor_->OnByteSkiped(nbytes);
  }
}

}  // namespace zs
