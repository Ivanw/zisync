// Copyright 2014, zisync.com

#include "zisync/kernel/platform/platform.h"  // NOLINT

#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/transfer/tar_download_task.h"

namespace zs {

IDownloadTask* TarDownloadTaskFactory::CreateTask(
    ITaskMonitor* monitor,
    int32_t local_tree_id,
    const std::string& remote_tree_uuid,
    const std::string& uri,
    SSL_CTX* ctx) {
  return new TarDownloadTask(
      monitor, local_tree_id, remote_tree_uuid, uri, ctx);
}

int TarDownloadTask::GetTaskId() {
  return TarGetTaskCommon::GetTaskId();
}

TarDownloadTask::TarDownloadTask(
    ITaskMonitor* monitor,
    int32_t local_tree_id,
    const std::string& remote_tree_uuid,
    const std::string& uri,
    SSL_CTX* ctx):
    TarGetTaskCommon(
        monitor, local_tree_id, remote_tree_uuid, uri, ctx) {}

err_t TarDownloadTask::AppendFile(
    const std::string& encode_path, const std::string &target_path,
    int64_t size) {
  assert(encode_path_.size() == 0);
  assert(target_path_.size() == 0);
  encode_path_ = encode_path;
  target_path_ = target_path;
  TarGetTaskCommon::AppendFile(encode_path, size);
  return ZISYNC_SUCCESS;
}

err_t TarDownloadTask::Execute() {
  err_t zisync_ret = TarGetTaskCommon::Execute();
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  } else if (some_download_fail) {
    return ZISYNC_ERROR_GENERAL;
  } else {
    return ZISYNC_SUCCESS;
  }
}

err_t TarDownloadTask::SendGetHttpHeader(std::ostream *out) {
  std::string buffer;
  const char*  http_header =
      "GET tar HTTP/1.1\r\nZiSync-Remote-Tree-Uuid:%s\r\n"
      "ZiSync-Total-Size:%" PRId64 "\r\nZiSync-Total-Files:%d\r\n\r\n";
  int ret = StringFormat(&buffer, http_header, remote_tree_uuid_.c_str(), 
                         total_size_, total_file_num_);
  assert(ret > 0);

  out->write(buffer.c_str(), buffer.length());

  return  ZISYNC_SUCCESS;
}

}  // namespace zs

