// Copyright 2014, zisync.com
#include <memory>
#include <map>
#include <list>
#include <string>
#include <algorithm>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/proto/kernel.pb.h"

#include "zisync/kernel/utils/upload.h"
#include "zisync/kernel/utils/updownload.h"
#include "zisync/kernel/libevent/transfer.h"

#ifdef __APPLE__
#include "zisync/kernel/kernel_stats.h"
#endif

namespace zs {

using std::unique_ptr;
using std::map;
using std::list;
using std::string;

class UploadThread;

class Upload : public IUpload, UpDownload, IUpDownloadProgressDelegate{
 public:
  ~Upload() {
    assert(task_map.empty());
  }
  static Upload* GetInstance() {
    return &handler;
  }
  virtual err_t Startup(
      int32_t sync_id, const std::string &relative_path, 
      const string &real_path, int32_t *task_id);
  virtual err_t Shutdown(int32_t task_id ) {
    return ShutdownTaskImp(task_id);
  }
  virtual err_t QueryStatus(int32_t task_id, UploadStatus *status) {
    UpDownloadStatus status_;
    err_t zisync_ret = QueryStatusImp(task_id, &status_);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("QueryStatus(%d) fail : %s", 
                  task_id, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    status_.ToUploadStatus(status);
    return ZISYNC_SUCCESS;
  }
  virtual void CleanUp() {
    return CleanUpImp();
  }
  
  void UpDownloadProgressReport(){}
  void UpDownloadProgressReport(const UpDownloadStatus *status, UpDownloadThread *thread);

 private:
  Upload() {}
  
  static Upload handler;
};

void Upload::UpDownloadProgressReport(const UpDownloadStatus *status_, UpDownloadThread *thread) {
#ifdef __APPLE__
  if (mutex.TryAquire()) {
    UploadStatus status;
    status_->ToUploadStatus(&status);
    int32_t task_id = -1;
    for (auto it = task_map.begin(); it != task_map.end(); ++it) {
      if ((void*)it->second.get() == (void*)thread) {
        task_id = it->first;
        break;
      }
    }
    if (task_id != -1) {
      UploadStatusObjc *stat = [[UploadStatusObjc alloc] initWithUploadStatus:status];
      NSDictionary *usrIfo = @{kZSNotificationUserInfoEventId:[NSNumber numberWithInteger:task_id], kZSNotificationUserInfoData:stat};
      [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameUploadStatus
                                                          object:nil
                                                        userInfo:usrIfo];
    }
    
    mutex.Release();
  }
#endif
}

class UploadThread : public UpDownloadThread{
 public:
  UploadThread(
      int32_t sync_id_, const char *sync_uuid_, 
      const string &relative_path_, const string &real_path_, IUpDownloadProgressDelegate *delegate_ = NULL):
      UpDownloadThread("Upload", sync_id_, sync_uuid_, relative_path_, delegate_),
      real_path(real_path_) {}
  virtual ~UploadThread() {
    UpDownloadThread::Shutdown();
  };
  
 private:
  virtual bool IsFindFileResponseOk(const IssueFindFileRequest &issue_request) {
    const MsgStat &stat = issue_request.response.response().stat();
    if (issue_request.error() == ZISYNC_ERROR_FILE_NOENT) {
      return true;
    }else if (stat.status() == FS_REMOVE) {
      return true;
    } else {
      error = ZISYNC_ERROR_FILE_EXIST;
      return false;
    }
  }
  
  virtual err_t ExecuteTransferTask() {
    unique_ptr<IUploadTask> task;
    OsFileStat stat;
    int os_ret = OsStat(real_path, string(), &stat);
    if (os_ret != 0) {
      ZSLOG_ERROR("OsStat(%s) fail : %s", real_path.c_str(), OsGetLastErr());
      return ZISYNC_ERROR_FILE_NOENT;
    }
    {
      MutexAuto auto_mutex(&mutex);
      monitor.reset(new UpDownloadMonitor(stat.length, this));
    }
    {
      MutexAuto mutex_auto(&abort_mutex);
      if (IsAborted()) {
        return ZISYNC_ERROR_GENERAL;
      }
      task.reset(GetTransferServer2()->CreateUploadTask(
              monitor.get(), local_tree_id, "tar", remote_tree_uuid, 
              remote_device_data_uri));
      transfer_task_id = task->GetTaskId();
    }
    err_t ret = task->AppendFile(real_path, relative_path, stat.length);
    if (ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("UploadTask AppendFile(%s) fail : %s",
                  relative_path.c_str(), zisync_strerror(ret));
      return ret;
    } 
    ret = task->Execute();
    if (ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Execute UploadTask fail : %s", zisync_strerror(ret));
      return ret;
    }
    return ZISYNC_SUCCESS;
  }
 
  string real_path;
  
};

Upload Upload::handler;
err_t Upload::Startup(
    int32_t sync_id, const string &relative_path, 
    const string &real_path, int32_t *task_id) {
  string sync_uuid;
  err_t zisync_ret = GetSyncUuid(sync_id, &sync_uuid);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetSyncUuid(%d) faile", sync_id);
    return zisync_ret;
  }

  return StartupTaskImp(sync_id, new UploadThread(
          sync_id, sync_uuid.c_str(), relative_path, real_path, this), task_id);
}

IUpload* IUpload::GetInstance() {
  return Upload::GetInstance();
}

}  // namespace zs

