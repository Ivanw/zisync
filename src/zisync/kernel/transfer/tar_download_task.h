// Copyright 2014, zisync.com

#ifndef ZISYNC_KERNEL_TRANSFER_TAR_DOWNLOAD_TASK_H_
#define ZISYNC_KERNEL_TRANSFER_TAR_DOWNLOAD_TASK_H_

#include <string>
#include <vector>

#include "zisync/kernel/transfer/transfer_server.h"
#include "zisync/kernel/transfer/transfer.pb.h"
#include "zisync/kernel/transfer/tar_get_task_common.h"

namespace zs {

class OsTcpSocket;

class TarDownloadTaskFactory : public DownloadTaskFactory {
  friend class TransferServer;
 public:
  virtual ~TarDownloadTaskFactory() {}

  virtual IDownloadTask* CreateTask(
      ITaskMonitor* monitor,
      int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx);
  virtual const std::string& GetFormat() {
    return format_;
  }

 private:
  TarDownloadTaskFactory():format_("tar") {}

  std::string format_;
  static TarDownloadTaskFactory s_tar_download_task_factory_;
};

/**
 * Download Put a bounch of file (including data) of the same tree from remote.
 *
 * The file paths relative to tree root are sent as http body encoded by
 * protobuf message TarDownloadFileList defined in transfer.proto. that is,
 * DOWNLOAD tar HTTP/1.1\r\n
 * ZiSync-Remote-Tree-Uuid:00000000-0000-0000-0000-000000000000\r\n
 * ZiSync-Total-Size:total_size\r\n
 * ZiSync-Total-Files:total_files\r\n
 * \r\n
 * <path encoded by protobuf ...>
 *
 * The result of get is in libtar format, carried by http response. e.g. if succeed:
 *
 * HTTP/1.1 200 OK\r\n
 * \r\n
 * <data in libtar format ...>
 *
 * otherwise:
 *
 * HTTP/1.1 Status-Code Reason-Phrase\r\n
 * ZiSync-Error-Code: 123\r\n
 * ZiSync-Error-String: not suppored method\r\n
 * \r\n
 *
 * Note:
 *   1. boths the request and response SHOULD confirm to HTTP protocol.
 *   2. <data in libtar format ...> MUST be inflated by linux tar command
 *   3. <path encoded by protobuf ...> MUST be parser ok using google protobuf
 */
class TarDownloadTask : public IDownloadTask, TarGetTaskCommon {
  friend class TarDownloadTaskFactory;
 public:
  virtual ~TarDownloadTask() {}

  virtual int GetTaskId();

  virtual err_t AppendFile(
      const std::string& encode_path, const std::string &target_path,
      int64_t size);

  virtual err_t Execute();
  virtual std::string GetTargetPath(const std::string &encode_path) {
    assert(encode_path == encode_path_);
    return target_path_;
  }
  virtual bool PrepareFileParentDir(const std::string &path) {
    return true;
  }
  virtual bool AllowPut(const std::string &path) {
    return true;
  }
  virtual err_t SendGetHttpHeader(std::ostream *out);

 private:
  TarDownloadTask();
  TarDownloadTask(
      ITaskMonitor* monitor,
      int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx);

  std::string encode_path_;
  std::string target_path_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TAR_DOWNLOAD_TASK_H_
