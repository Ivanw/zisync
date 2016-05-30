// Copyright 2015, zisync.com
#ifndef  ZISYNC_KERNEL_UTILS_UPDOWNLOAD_H_
#define  ZISYNC_KERNEL_UTILS_UPDOWNLOAD_H_

#include <memory>
#include <map>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/utils/sync.h"

#ifdef __APPLE__
#include "zisync/kernel/notification/notification.h"
#endif

namespace zs {

using std::unique_ptr;
using std::map;

class UpDownloadThread;

const int UPDOWNLOAD_PREPARE = 1,
      UPDOWNLOAD_PRECESS = 2,
      UPDOWNLOAD_DONE = 3,
      UPDOWNLOAD_FAIL = 4;
class UpDownloadStatus {
 public:
  void ToUploadStatus(UploadStatus *upload_status) const {
    switch (status) {
      case UPDOWNLOAD_PREPARE :
        upload_status->status = UPLOAD_PREPARE;
        break;
      case UPDOWNLOAD_PRECESS :
        upload_status->status = UPLOAD_PRECESS;
        break;
      case UPDOWNLOAD_DONE :
        upload_status->status = UPLOAD_DONE;
        break;
      case UPDOWNLOAD_FAIL :
        upload_status->status = UPLOAD_FAIL;
        break;
      default:
        assert(false);
        break;
    }
    upload_status->total_num_byte = total_num_byte;
    upload_status->num_byte_to_upload = num_byte_to_transfer;
    upload_status->speed_upload = speed_transfer;
    upload_status->error_code = error_code;
  }
  void ToDownloadStatus(DownloadStatus *download_status) const {
    switch (status) {
      case UPDOWNLOAD_PREPARE :
        download_status->status = DOWNLOAD_PREPARE;
        break;
      case UPDOWNLOAD_PRECESS :
        download_status->status = DOWNLOAD_PRECESS;
        break;
      case UPDOWNLOAD_DONE :
        download_status->status = DOWNLOAD_DONE;
        break;
      case UPDOWNLOAD_FAIL :
        download_status->status = DOWNLOAD_FAIL;
        break;
      default:
        assert(false);
        break;
    }
    download_status->total_num_byte = total_num_byte;
    download_status->num_byte_to_download = num_byte_to_transfer;
    download_status->speed_download = speed_transfer;
    download_status->error_code = error_code;

  }

  int status;
  int64_t total_num_byte;
  int64_t num_byte_to_transfer;
  int64_t speed_transfer;
  err_t error_code;
};

class IUpDownloadProgressDelegate {
 public:
  virtual void UpDownloadProgressReport() = 0;
  virtual void UpDownloadProgressReport(const UpDownloadStatus *status, UpDownloadThread *thread) = 0;
};

class UpDownloadMonitor : public ITaskMonitor, IOnTimer {
public:
  UpDownloadMonitor(int64_t total_bytes, IUpDownloadProgressDelegate *delegate = NULL):total_bytes_(total_bytes),
    to_transfer_bytes_(total_bytes), speed_transfer_(0),
    last_caculate_to_transfer_bytes_(total_bytes),
    last_caculate_time_(OsTimeInMs()),
    is_first_time_caculate_speed_(true),
    speed_timer(1000, this),
    delegate_(delegate){
      int ret = speed_timer.Initialize();
      assert(ret == 0);
    }
  ~UpDownloadMonitor() {
    speed_timer.CleanUp();
  }
  virtual void OnFileTransfered(int32_t count) {}
  virtual void OnFileSkiped(int32_t count) {}
  virtual void OnByteTransfered(int64_t nbytes) {
    assert(nbytes >= 0);
    assert(nbytes <= total_bytes_);
    to_transfer_bytes_ -= nbytes;
    assert(to_transfer_bytes_ >= 0);
    if (is_first_time_caculate_speed_) {
      CaculateSpeed();
      is_first_time_caculate_speed_ = false;
    }
  }
  virtual void OnByteSkiped(int64_t nbytes) {}
  virtual void OnFileTransfer(const std::string &path) {OnTimer();}
  virtual void AppendFile(const std::string &local_path,
                          const std::string &remote_path,
                          const std::string &encode_path,
                          int64_t length) {}

  virtual void OnTimer() {
    CaculateSpeed();
#ifdef __APPLE__
    delegate_->UpDownloadProgressReport();
#endif
  }

  void CaculateSpeed() {
    MutexAuto auto_mutex(&mutex);
    int64_t to_transfer_bytes_tmp = to_transfer_bytes_;
    int64_t cur_time = OsTimeInMs();
    speed_transfer_ = static_cast<int64_t>(static_cast<double>(
            last_caculate_to_transfer_bytes_ - to_transfer_bytes_tmp) 
        * 1000.0 / (cur_time - last_caculate_time_));
    last_caculate_to_transfer_bytes_ = to_transfer_bytes_tmp;
    last_caculate_time_ = cur_time;
  }

  void Stop() {
    speed_timer.CleanUp();
    speed_transfer_ = 0;
  }

  void ToUpDownloadStatus(UpDownloadStatus *status) {
    status->speed_transfer = speed_transfer_;
    status->total_num_byte = total_bytes_;
    status->num_byte_to_transfer = to_transfer_bytes_;
  }
  
  int64_t speed_transfer() { return speed_transfer_; }
  int64_t total_bytes() { return total_bytes_; }
  int64_t to_transfer_bytes() { return to_transfer_bytes_; }
 private:
  Mutex mutex;
  int64_t total_bytes_;
  int64_t to_transfer_bytes_;
  int64_t speed_transfer_;
  int64_t last_caculate_to_transfer_bytes_;
  int64_t last_caculate_time_;
  bool is_first_time_caculate_speed_;
  OsTimer speed_timer;

private:
  IUpDownloadProgressDelegate *delegate_;
};

class IssueFindFileRequest : public IssueRequest {
 public:
  IssueFindFileRequest(
      int32_t device_id, const string &device_uuid, const char *remote_ip, 
      int32_t remote_route_port_, int32_t remote_data_port_, 
      const string &remote_tree_uuid_):
      IssueRequest(device_id, device_uuid, remote_ip, remote_route_port_, 
                   false),
      remote_tree_uuid(remote_tree_uuid_), remote_data_port(remote_data_port_) {
      }
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }
  FindFileRequest* mutable_find_file_request() { return &request; }

  int32_t sync_id;
  const string remote_tree_uuid;
  int32_t remote_data_port;
  FindFileRequest request;
  FindFileResponse response;
 private:
  IssueFindFileRequest(IssueFindFileRequest&);
  void operator=(IssueFindFileRequest&);
};

class IssueFindFileAbort : public IssueRequestsAbort {
 public:
  IssueFindFileAbort(IAbortable *abort):abort_(abort) {}
  inline virtual bool IsAborted();

 private:
  IAbortable *abort_;
};

bool IssueFindFileAbort::IsAborted() {
  return abort_->IsAborted();
}

class UpDownloadThread : public OsThread, IAbortable, public IUpDownloadProgressDelegate {
 public:
  UpDownloadThread(const char* name, int32_t sync_id_, 
                   const char *sync_uuid_, const string &relative_path_,
                   IUpDownloadProgressDelegate *delegate):
      OsThread(name), sync_id(sync_id_), local_tree_id(-1), 
      transfer_task_id(-1), sync_uuid(sync_uuid_), 
      relative_path(relative_path_), aborted(false), is_done(false), 
      file_length(-1), error(ZISYNC_SUCCESS), should_post_notification(true),
      delegate_(delegate){}
  virtual ~UpDownloadThread() {}

  int Shutdown() {
    {
      MutexAuto mutex_auto(&abort_mutex);
      ITransferServer *transfer_server = zs::GetTransferServer2();
      if (transfer_task_id != -1) {
        transfer_server->CancelTask(transfer_task_id);
      }
      this->Abort();
    }
    return OsThread::Shutdown();
  }

  virtual bool Abort() {
    aborted = true;
    return true;
  }
  virtual bool IsAborted() { return aborted; }
  virtual int Run() {
    int ret = RunIntern();
    is_done = true;
    {
      MutexAuto auto_mutex(&mutex);
      if (monitor) {
        monitor->Stop();
      }
    }
    UpDownloadProgressReport();
    return ret;
  }

  err_t QueryStatus(UpDownloadStatus *status) {
    MutexAuto auto_mutex(&mutex);
    if (!is_done) {
      if (!monitor) {
        status->status = UPDOWNLOAD_PREPARE;
      } else {
        status->status = UPDOWNLOAD_PRECESS;
        monitor->ToUpDownloadStatus(status);
      }
    } else {
      if (error == ZISYNC_SUCCESS) {
        status->status = DOWNLOAD_DONE;
        if (monitor) {
          monitor->ToUpDownloadStatus(status);
        } else {
          status->total_num_byte = 0;
          status->num_byte_to_transfer = 0;
          status->speed_transfer = 0;
        }
      } else {
        status->status = DOWNLOAD_FAIL;
      }
    }
    status->error_code = error;
    return ZISYNC_SUCCESS;
  }
  
  void UpDownloadProgressReport() {
#ifdef __APPLE__
    UpDownloadStatus status;
    QueryStatus(&status);
    assert(delegate_);
    MutexAuto auto_mutex(&mutex);
    if (should_post_notification) {
      delegate_->UpDownloadProgressReport(&status, this);
      if (status.status == DOWNLOAD_DONE) {
        should_post_notification = false;
      }
    }
#endif
  }
  
  void UpDownloadProgressReport(const UpDownloadStatus *status, UpDownloadThread *thread){}
 protected:
  int RunIntern();
  virtual err_t ExecuteTransferTask() = 0;
  virtual bool IsFindFileResponseOk(
      const IssueFindFileRequest &issue_request) = 0;

  bool GetUpDownloadSource();
  
  Mutex mutex;
  Mutex abort_mutex;
  unique_ptr<UpDownloadMonitor> monitor;
  int32_t sync_id, local_tree_id, transfer_task_id;
  string sync_uuid, local_tree_uuid, remote_tree_uuid, 
         remote_device_data_uri, relative_path;
  bool aborted, is_done;
  int64_t file_length;
  err_t error;
private:
  bool should_post_notification;
protected:
  IUpDownloadProgressDelegate *delegate_;
};

class UpDownload {
 protected:
  UpDownload():alloc_task_id(0) {}
  virtual ~UpDownload() {
    assert(task_map.empty());
  }
  int32_t AllocTaskId() {
    return (alloc_task_id ++);
  }
  err_t GetSyncUuid(int32_t sync_id, string *sync_uuid) {
    IContentResolver *resolver = GetContentResolver();
    const char *sync_projs[] = {
      TableSync::COLUMN_UUID, TableSync::COLUMN_PERM,
    };
    unique_ptr<ICursor2> sync_cursor(resolver->Query(
            TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
            "%s = %d", TableSync::COLUMN_ID, sync_id));
    if (!sync_cursor->MoveToNext() || 
        Sync::IsUnusable(sync_cursor->GetInt32(1))) {
      ZSLOG_ERROR("Noent Sync(%d)", sync_id);
      return ZISYNC_ERROR_SYNC_NOENT;
    } 
    *sync_uuid = sync_cursor->GetString(0);
    return ZISYNC_SUCCESS;
  }

  err_t StartupTaskImp(int32_t sync_id, UpDownloadThread *thread_, 
                       int32_t *task_id) {
    MutexAuto auto_mutex(&mutex);
    *task_id = AllocTaskId();
    assert(*task_id >= 0);
    unique_ptr<UpDownloadThread> thread(thread_);
    int ret = thread->Startup();
    if (ret != 0) {
      ZSLOG_ERROR("Start thread fail");
      return ZISYNC_ERROR_OS_THREAD;
    }
    unique_ptr<UpDownloadThread> &task = task_map[*task_id];
    assert(!task);
    task.reset(thread.release());
    return ZISYNC_SUCCESS;
  }

  err_t ShutdownTaskImp(int32_t task_id) {
    MutexAuto auto_mutex(&mutex);
    auto find = task_map.find(task_id);
    if (find != task_map.end()) {
      unique_ptr<UpDownloadThread> &task = find->second;
      assert(task);
      task.reset(NULL);
      task_map.erase(find);
      return ZISYNC_SUCCESS;
    } else {
      return ZISYNC_ERROR_DOWNLOAD_NOENT;
    }
  }
  
  err_t QueryStatusImp(int32_t task_id, UpDownloadStatus *status) {
    MutexAuto auto_mutex(&mutex);
    auto find = task_map.find(task_id);
    if (find != task_map.end()) {
      return find->second->QueryStatus(status);
    } else {
      return ZISYNC_ERROR_DOWNLOAD_NOENT;
    }
  }

  void CleanUpImp();

 protected:
  Mutex mutex;
  int32_t alloc_task_id;
  map<int32_t /* task_id */, unique_ptr<UpDownloadThread>> task_map;
};

}  // namespace zs

#endif   // ZISYNC_KERNEL_UTILS_UPDOWNLOAD_H_
