// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_TRANSFER_TRANSFER_H_
#define ZISYNC_KERNEL_TRANSFER_TRANSFER_H_

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <string>
#include <vector>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class ZmqContext;

class ITransferTask {
 public:
  virtual ~ITransferTask() { /* virtual destructor */ }

  virtual int GetTaskId() = 0;

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path) = 0;

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path,
      const std::string& signature) = 0;

  virtual err_t Execute() = 0;

 protected:
  ITransferTask() {}
};

class ITransferTaskInfo {
 public:
  enum TaskType {
    TASK_TYPE_PUT, TASK_TYPE_GET
  };

  virtual ~ITransferTaskInfo() { /* virtual destructor */ }

  virtual int GetTaskId() = 0;
  virtual const std::string& GetTaskDescription() = 0;
  virtual TaskType GetTaskType() = 0;
};

class QueryTransferTaskInfoResult {
 public:
  QueryTransferTaskInfoResult();
  ~QueryTransferTaskInfoResult();

 private:
  QueryTransferTaskInfoResult(QueryTransferTaskInfoResult&);
  void operator=(QueryTransferTaskInfoResult&);

  ITransferTaskInfo *transfer_task_infos_;
  int transfer_task_infos_count_;
};

class ITaskMonitor {
 public:
  virtual ~ITaskMonitor() {}
  // the transfer server will call OnTaskBegin() to notify to
  // total number of files and the total bytes of this task.
  // the transfer server will call OnFileTransfered() to notify
  // the monitor when some file is successly transfered, call
  // OnFileSkiped() if some file is not successly transfered.
  virtual void OnFileTransfered(int32_t count, const std::string &path) = 0;
  virtual void OnFileSkiped(int32_t count, const std::string &path) = 0;

  // After transfer some bytes, the transfer server will
  // call OnByteTransfered() to notify the monitor how many bytes
  // have been transfered. If some bytes can not be transfered, e.g.
  // due to file read error or transfer aborted, the transfer server
  // will call OnByteSkiped().
  virtual void OnByteTransfered(int64_t nbytes) = 0;
  virtual void OnByteSkiped(int64_t nbytes) = 0;
  virtual void OnFileTransfer(const std::string &path) = 0;
  virtual void AppendFile(const std::string &path, int64_t length) = 0;
};

class IGetHandler {
 public:
  virtual ~IGetHandler() {}
  virtual void OnHandleFile(
      const std::string &relative_path, const std::string &sha1) = 0;
};

class IGetTask {
 public:
  virtual ~IGetTask() { /* virtual destructor */ }

  virtual int GetTaskId() = 0;

  virtual void SetHandler(IGetHandler *handler) = 0;

  virtual err_t AppendFile(
      const std::string& encode_path, int64_t size) = 0;

  virtual err_t Execute(const std::string& tmp_dir) = 0;

 protected:
  IGetTask() {}
};

class IDownloadTask {
 public:
  virtual ~IDownloadTask() { /* virtual destructor */ }

  virtual int GetTaskId() = 0;

  virtual err_t AppendFile(
      const std::string& encode_path,
      const std::string &target_path, int64_t size) = 0;

  virtual err_t Execute() = 0;

 protected:
  IDownloadTask() {}
};

class IUploadTask {
 public:
  virtual ~IUploadTask() { /* virtual destructor */ }

  virtual int GetTaskId() = 0;

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path, int64_t size) = 0;

  virtual err_t Execute() = 0;

 protected:
  IUploadTask() {}
};

class IPutTask {
 public:
  virtual ~IPutTask() { /* virtual destructor */ }

  virtual int GetTaskId() = 0;

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path,
      int64_t size) = 0;

  virtual err_t Execute() = 0;

 protected:
  IPutTask() {}
};

class IPutHandler {
 public:
  virtual ~IPutHandler() { /* virtual destructor */ }

  virtual bool StartupHandlePut(
      const std::string& tmp_root, int32_t *task_id) = 0;
  virtual bool OnHandleFile(
      int32_t task_id, const std::string& relative_path, 
      const std::string& sha1) = 0;
  virtual bool ShutdownHandlePut(int32_t task_id) = 0;
};

class IUploadHandler {
 public:
  virtual ~IUploadHandler() { /* virtual destructor */ }

  virtual bool OnHandleFile(
      const std::string& real_path,
      const std::string& relative_path, 
      const std::string& sha1) = 0;
};

class ITreeAgent {
 public:
  virtual ~ITreeAgent() { /* virtual destructor */ }

  virtual std::string GetTreeRoot(const std::string& tree_uuid) = 0;
  /* TmpDir should be the absolute path */
  virtual std::string GetNewTmpDir(const std::string& tree_uuid) = 0;
  virtual int32_t GetTreeId(const std::string& tree_uuid) = 0;
  virtual int32_t GetSyncId(const std::string& tree_uuid) = 0;
  virtual std::string GetTreeUuid(const int32_t tree_id) = 0;
  virtual bool AllowPut(
      int32_t local_tree_id, int32_t remote_tree_id, 
      const std::string& relative_path) = 0;
  virtual bool TryLock(
      int32_t local_tree_id, int32_t remote_tree_id) = 0;
  virtual void Unlock(
      int32_t local_tree_id, int32_t remote_tree_id) = 0;
};

class ISslAgent {
 public:
virtual ~ISslAgent() { /* virtual destructor */ }

virtual std::string GetPrivateKey() = 0;
virtual std::string GetCertificate() = 0;
virtual std::string GetCaCertificate() = 0;
};

class ITransferServer {
 public:
  virtual ~ITransferServer() { /* virtual destructor */ }

  virtual err_t Initialize(
      int32_t port,
      IPutHandler* put_handler,
      IUploadHandler* upload_handler,
      ITreeAgent* tree_agent,
      ISslAgent* ssl_agent) = 0;
  virtual err_t CleanUp() = 0;
  /*
   * task_format can be "tar", "tgz", "dif"
   * uri should be "host:port"
   *
   * return IGetTask need delete
   */
  virtual IGetTask* CreateGetTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri) = 0;

  /*  this is used by Download interface, no need to check NeedSync */
  virtual IDownloadTask* CreateDownloadTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri) = 0;

  virtual IPutTask* CreatePutTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri) = 0;
  
  /*  this is used by Upload interface, no need to check NeedSync */
  virtual IUploadTask* CreateUploadTask(
      ITaskMonitor* monitor,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri) = 0;

  virtual ITreeAgent* GetTreeAgent() = 0;
  virtual IPutHandler* GetPutHandler() = 0;

  virtual err_t CancelTask(int task_id) = 0;
  virtual err_t CancelAllTask() = 0;

  virtual err_t QueryTaskInfoResult(QueryTransferTaskInfoResult *result) = 0;

  virtual err_t SetPort(int32_t port) = 0;

 protected:
  ITransferServer() {}
};

ITransferServer* GetTransferServer();
ITransferServer* GetTransferServer2();

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TRANSFER_H_
