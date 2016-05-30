// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_TAR_GET_TASK_COMMON_H_
#define ZISYNC_KERNEL_TAR_GET_TASK_COMMON_H_

#include <openssl/ssl.h>
#include <string>
#include <ostream>

#include "zisync_kernel.h"  // NO_LINT
#include "zisync/kernel/transfer/transfer.pb.h"
#include "zisync/kernel/transfer/transfer_server.h"

namespace zs {

class ITaskMonitor;

class TarGetTaskCommon {
 public:
  TarGetTaskCommon(ITaskMonitor *monitor,
                   int32_t local_tree_id, 
                   const std::string &remote_tree_uuid,
                   const std::string &uri, SSL_CTX* ctx):
      some_download_fail(false), local_tree_id_(local_tree_id),
      remote_tree_uuid_(remote_tree_uuid), 
      total_size_(0), total_file_num_(0), uri_(uri), monitor_(monitor),
      ctx_(ctx) {
        task_id_ = GetTasksManager()->AddTask();
      }
  ~TarGetTaskCommon() {
    GetTasksManager()->SetTaskAbort(task_id_);
  }
  virtual std::string GetTargetPath(const std::string &encode_path) = 0;
//  virtual bool PrepareFileParentDir(const std::string &path) = 0;
  virtual bool AllowPut(const std::string &path) = 0;
  virtual err_t SendGetHttpHeader(std::ostream *out) = 0;


  void AppendFile(
      const std::string& encode_path, int64_t size) {
    file_list_.add_relative_paths(encode_path);
    total_size_ += size;
    total_file_num_++;
  }

  int32_t GetTaskId() {
    return task_id_;
  }

  err_t Execute();

 protected:
  bool some_download_fail;
  int32_t local_tree_id_;
  std::string remote_tree_uuid_;
  int64_t total_size_;
  int total_file_num_;

 private:
  TarGetFileList file_list_;
  std::string uri_;
  ITaskMonitor* monitor_;
  int32_t task_id_;
  SSL_CTX* ctx_;

  err_t SendRelativePathsProtobuf(std::ostream *out) {
    std::string message;
    bool ret = file_list_.SerializeToString(&message);
    assert(ret == true);
    out->write(message.c_str(), message.length());
    return ZISYNC_SUCCESS;
  }
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TAR_GET_TASK_COMMON_H_
