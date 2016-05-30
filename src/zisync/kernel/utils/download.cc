#include <memory>
#include <map>
#include <list>
#include <string>
#include <algorithm>

#include "zisync/kernel/platform/platform.h"

#include "zisync/kernel/utils/download.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/updownload.h"

#ifdef __APPLE__
#include "zisync/kernel/kernel_stats.h"
#endif

namespace zs {

using std::unique_ptr;
using std::map;
using std::list;
using std::string;

class DownloadThread;

class CachedFile {
 public:
  CachedFile(const string &path, int64_t length):
      path_(path), length_(length) {}
  const string& path() { return path_; }
  int64_t length() { return length_; }
 private:
  const string path_;
  const int64_t length_;
};

static Mutex cached_files_mutex;
static list<unique_ptr<CachedFile>> cached_files;
static int64_t cached_files_size_amount = 0;

class Download : public IDownload, UpDownload, IUpDownloadProgressDelegate {
 public:
  virtual ~Download() {}
  static Download* GetInstance() {
    return &handler;
  }
  virtual bool Initialize();
  virtual err_t Startup(
      int32_t sync_id, const std::string &relative_path, 
      string *target_path, int32_t *task_id);
  virtual err_t Shutdown(int32_t task_id ) {
    return ShutdownTaskImp(task_id);
  }
  
  virtual err_t QueryStatus(int32_t task_id, DownloadStatus *status) {
    UpDownloadStatus status_;
    err_t zisync_ret = QueryStatusImp(task_id, &status_);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("QueryStatus(%d) fail : %s", 
                  task_id, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    status_.ToDownloadStatus(status);
    return ZISYNC_SUCCESS;
  }
  virtual void CleanUp() {
    cached_files.clear();
    cached_files_size_amount = 0;
    return CleanUpImp();
  }
  virtual bool ReduceDownloadCacheVolume(int64_t new_volume);
  virtual bool HasDownloadCache(
      const std::string &path, int64_t file_mtime, int64_t file_length);
  virtual err_t CleanUpDownloadCache();
  int64_t  GetDownloadCacheAmount() {
    return cached_files_size_amount;
  }

  virtual void UpDownloadProgressReport(){}
  virtual void UpDownloadProgressReport(const UpDownloadStatus *status, UpDownloadThread *thread);
 private:
  Download() {}
  static Download handler;
  bool has_user_define_target_path;
};

  void Download::UpDownloadProgressReport(const UpDownloadStatus *status_, UpDownloadThread *thread) {
#ifdef __APPLE__
    if (mutex.TryAquire()) {
      DownloadStatus status;
      status_->ToDownloadStatus(&status);
      int32_t task_id = -1;
      for (auto it = task_map.begin(); it != task_map.end(); ++it) {
        if ((void*)it->second.get() == (void*)thread) {
          task_id = it->first;
          break;
        }
      }
      if (task_id != -1) {
        DownloadStatusObjc *stat = [[DownloadStatusObjc alloc] initWithDownloadStatus:status];
        NSDictionary *usrIfo = @{kZSNotificationUserInfoEventId:[NSNumber numberWithInteger:task_id], kZSNotificationUserInfoData:stat};
        [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameDownloadStatus
                                                            object:nil
                                                          userInfo:usrIfo];
      }
      
      mutex.Release();
    }
#endif
}

static inline void CacheDeleteParentDir(const string &path) {
  if (path == Config::download_cache_dir()) {
    return;
  }

  if (path.find(Config::download_cache_dir()) != 0) {
    ZSLOG_ERROR("Delete Dir (%s) not in download cache dir(%s)",
                path.c_str(), Config::download_cache_dir().c_str());
    return;
  }

  int ret = zs::OsDeleteDirectory(path);
  if (ret != 0) {
    ZSLOG_ERROR("Delete dir(%s) fail : %s", path.c_str(),
                OsGetLastErr());
    return;
  }

  return CacheDeleteParentDir(zs::OsDirName(path));
}

static inline int CacheDeleteFile(const string &path) {
  int ret = zs::OsDeleteFile(path, false);
  if (ret != 0) {
    ZSLOG_ERROR("OsDeleteFile(%s) fail : %s",
                path.c_str(), zs::OsGetLastErr());
    return ret;
  }
  CacheDeleteParentDir(zs::OsDirName(path));
  return 0;
}

class DownloadThread : public UpDownloadThread {
 public:
  virtual ~DownloadThread() {
    UpDownloadThread::Shutdown();
  }
  DownloadThread(
      int32_t sync_id_, const char *sync_uuid_, 
      const string &relative_path_, const string &target_path_,
      bool has_user_define_target_path, IUpDownloadProgressDelegate *delegate = NULL):
      UpDownloadThread("Download", sync_id_, sync_uuid_, relative_path_, delegate),
      target_path(target_path_), file_mtime(-1),
      has_user_define_target_path_(has_user_define_target_path) {}

  //err_t QueryStatus(DownloadStatus *status);
 
 private:
  virtual err_t ExecuteTransferTask();
  virtual bool IsFindFileResponseOk(const IssueFindFileRequest &issue_request);

  
  string target_path;
  int64_t file_mtime;
  bool has_user_define_target_path_;
};

err_t DownloadThread::ExecuteTransferTask() {
  OsFileStat os_stat;
  if (!has_user_define_target_path_) {
    int stat_ret = zs::OsStat(target_path, string(), &os_stat);
    if (stat_ret == 0) {
      const string &target_path_ = target_path;
      MutexAuto mutex_auto(&cached_files_mutex);
      auto find = std::find_if(
          cached_files.begin(), cached_files.end(), 
          [ &target_path_ ] (const unique_ptr<CachedFile> &cached_file) 
          { return cached_file->path() == target_path_; });
      if (os_stat.length == file_length && 
          !HasMtimeChanged(os_stat.mtime, file_mtime)) {
        ZSLOG_INFO("Download file already has download cache");
        // update cache
        if (find != cached_files.end()) {
          CachedFile *cached_file = find->release();
          cached_files.erase(find);
          cached_files.emplace_back(cached_file);
        } else {
          cached_files.emplace_back(new CachedFile(target_path_, file_length));
        }
        return ZISYNC_SUCCESS;
      } else {
        if (find != cached_files.end()) {
          cached_files_size_amount -= (*find)->length();
          cached_files.erase(find);
        }
      }
    }

    MutexAuto mutex_auto(&cached_files_mutex);
    auto iter = cached_files.begin();
    while (cached_files_size_amount + file_length > 
           Config::download_cache_volume()) {
      if (iter == cached_files.end()) {
        if (cached_files.empty()) { // all cached file removed
          break;
        } else {
          ZSLOG_ERROR("Cache Volume is not enough, may due to the delete fail "
                      "of some cached file");
          return ZISYNC_ERROR_DOWNLOAD_FILE_TOO_LARGE;
        }
      }
      int ret = zs::CacheDeleteFile((*iter)->path());
      if (ret != 0) {
        ZSLOG_WARNING("CacheDeleteFile(%s) fail", (*iter)->path().c_str());
        iter ++;
      } else {
        cached_files_size_amount -= (*iter)->length();
        auto erase_iter = iter;
        iter ++;
        cached_files.erase(erase_iter);
      }
    }
  } // end !has_user_define_target_path
  {
    MutexAuto auto_mutex(&mutex);
    monitor.reset(new UpDownloadMonitor(file_length, this));
  }

  unique_ptr<IDownloadTask> task;
  { 
    MutexAuto mutex_auto(&abort_mutex);
    if (IsAborted()) {
      return ZISYNC_ERROR_GENERAL;
    }
    task.reset(GetTransferServer2()->CreateDownloadTask(
            monitor.get(), local_tree_id, "tar", remote_tree_uuid, 
            remote_device_data_uri));
    transfer_task_id = task->GetTaskId();
  }
  err_t ret = task->AppendFile(relative_path, target_path, file_length);
  if (ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("DownloadTask AppendFile(%s) fail : %s",
                relative_path.c_str(), zisync_strerror(ret));
    return ret;
  } 
  ret = task->Execute();
  if (ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Execute DownloadTask fail : %s", zisync_strerror(ret));
    zs::CacheDeleteFile(target_path);
    return ret;
  }
  int stat_ret = zs::OsSetMtime(target_path, file_mtime);
  if (stat_ret != 0) {
    ZSLOG_WARNING("SetMtime(%s) fail : %s", 
                  target_path.c_str(), OsGetLastErr());
  }

  // success
  if (!has_user_define_target_path_) {
    cached_files_size_amount += file_length;
    cached_files.emplace_back(new CachedFile(target_path, file_length));
  }
  return ZISYNC_SUCCESS;
}

bool DownloadThread::IsFindFileResponseOk(
    const IssueFindFileRequest &issue_request) {
    const MsgStat &stat = issue_request.response.response().stat();
  if (stat.type() != zs::FT_REG || stat.status() != zs::FS_NORMAL) {
    return false;
  } else {
    file_mtime = stat.mtime();
    return true;
  }
}

Download Download::handler;
err_t Download::Startup(
    int32_t sync_id, const string &relative_path, 
    string *target_path, int32_t *task_id) {
  string sync_uuid;
  err_t zisync_ret = GetSyncUuid(sync_id, &sync_uuid);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetSyncUuid(%d) fail : %s", 
                sync_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  if (target_path->empty()) {
    *target_path = GenDownloadTmpPath(sync_uuid, relative_path);
    has_user_define_target_path = false;
  } else {
    has_user_define_target_path = true;
  }
  return StartupTaskImp(sync_id, new DownloadThread(
          sync_id, sync_uuid.c_str(), relative_path, *target_path, 
          has_user_define_target_path, this), task_id);
}


IDownload* IDownload::GetInstance() {
  return Download::GetInstance();
}

bool Download::Initialize() {
  class DownloadInitVisitor : public IFsVisitor {
   public:
    DownloadInitVisitor() {}
    ~DownloadInitVisitor() {};
    virtual int Visit(const OsFileStat &stat) {
      if (stat.type == OS_FILE_TYPE_REG) {
        cached_files_size_amount += stat.length;
        cached_files.emplace_back(new CachedFile(stat.path, stat.length));
      }
      return 0;
    }
    virtual bool IsIgnored(const string &path) const {
      return false;
    }
  };

  cached_files.clear();
  cached_files_size_amount = 0;
  DownloadInitVisitor visitor;
  OsFsTraverser traverser(Config::download_cache_dir(), &visitor);
  if (traverser.traverse() != 0) {
    ZSLOG_ERROR("Traverse (%s) fail : %s",
                Config::download_cache_dir().c_str(),
                OsGetLastErr());
    return false;
  }
  return true;
}

bool Download::ReduceDownloadCacheVolume(int64_t new_volume) {
  MutexAuto mutex_auto(&cached_files_mutex);
  auto iter = cached_files.begin();
  while (cached_files_size_amount > new_volume) {
    if (iter == cached_files.end()) {
      assert(false);
      ZSLOG_ERROR("Donwloaded file is too large : (%" PRId64 ") > (%" 
                  PRId64 ")", cached_files_size_amount, new_volume);
      return false;
    }
    int ret = zs::CacheDeleteFile((*iter)->path());
    if (ret != 0) {
      ZSLOG_WARNING("CacheDeleteFile(%s) fail", (*iter)->path().c_str());
      iter ++;
    } else {
      cached_files_size_amount -= (*iter)->length();
      auto erase_iter = iter;
      iter ++;
      cached_files.erase(erase_iter);
    }
  }
  return true;
}

bool Download::HasDownloadCache(
    const std::string &path, int64_t file_mtime, int64_t file_length) {
  OsFileStat file_stat;
  int ret = OsStat(path, string(), &file_stat);
  return ret == 0 && file_stat.length == file_length && 
      !HasMtimeChanged(file_stat.mtime, file_mtime);
}

err_t Download::CleanUpDownloadCache() {
  int ret = zs::OsDeleteDirectories(Config::download_cache_dir(), false);
  // reinit the cache_amount and cache_files
  Initialize();
  if (ret != 0) {
    ZSLOG_ERROR("Delete Cache in Download Cache dir fail : %s",
                OsGetLastErr());
    return ZISYNC_ERROR_GENERAL;
  }
  return ZISYNC_SUCCESS;
}

}  // namespace zs

