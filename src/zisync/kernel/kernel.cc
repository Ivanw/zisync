#include <zmq.h>
#ifndef _WIN32
#include <signal.h>
#endif  // _WIN32
#include <cassert>
#include <algorithm>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/router.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/libevent/discover.h"
#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/monitor/monitor.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/utils/sync_put_handler.h"
#include "zisync/kernel/sync_tree_agent.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/tree_status.h"
#include "zisync/kernel/utils/discover_device.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/download.h"
#include "zisync/kernel/utils/upload.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/normalize_path.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/utils/query_cache.h"
#include "zisync/kernel/libevent/report_data_server.h"
#include "zisync/kernel/libevent/ui_event_server.h"
#include "zisync/kernel/libevent/discover_server.h"
#include "zisync/kernel/history/history_manager.h"
#include "zisync/kernel/permission.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/plain_config.h"
#ifdef __APPLE__
#import <CoreFoundation/CoreFoundation.h>
#include "zisync/kernel/notification/notification.h"
#include "zisync/kernel/kernel_stats.h"
#endif
namespace zs {

using std::unique_ptr;

static inline void DeviceIPCheck() {
  IContentResolver *resolver = GetContentResolver();
  int64_t earliest_no_resp_time = OsTimeInS() - 
      DEVICE_NO_RESP_OFFLINE_TIMEOUT_IN_S;

  resolver->Delete(
      TableDeviceIP::URI, " %s != %" PRId64 " AND %s < %" PRId64 ,
      TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME, 
      TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE,
      TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME, earliest_no_resp_time);

  vector<int32_t> device_ids;
  {
    const char *device_projs[] = {
      TableDevice::COLUMN_ID,
    };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
            "%s = %d AND %s != %d", 
            TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE,
            TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID));

    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      device_ids.push_back(device_id);
    }
  }
  std::for_each(device_ids.begin(), device_ids.end(),
                [] (int32_t device_id) 
                { CheckAndDeleteNoIpLeftDevice(device_id); });
}

static inline int32_t GetCurrentTreeCount() {
//  std::vector<std::unique_ptr<Tree>> trees;
//  Tree::QueryBy(&trees, "%s = %d AND %s = %d",
//                TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
//                TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
//  return trees.size();
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    "COUNT(*)",
  };

  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d",
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,//no matter root_status, anti-cheat, even if tree_root moved, still count as one tree
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_NONE));
  if (!cursor->MoveToNext()) {
    ZSLOG_ERROR("Get data from databse fail.");
    return -1;
  }

  return cursor->GetInt32(0);
}

static inline int32_t GetCurrentBackupCount() {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    "COUNT(*)"
  };

  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d",
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_SRC));
  if (!cursor->MoveToNext()) {
    ZSLOG_ERROR("Get backup count from database fail.");
    return -1;
  }

  return cursor->GetInt32(0);
}

static inline int32_t GetCurrentShareCount() {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    "COUNT(*)",
  };

  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableShareSync::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s > 0", TableShareSync::COLUMN_ID));
  if (!cursor->MoveToNext()) {
    ZSLOG_ERROR("Get data from database fail.");
    return -1;
  }

  return cursor->GetInt32(0);
}

class TreeRootChecker: public IOnTimer {
  public:
    TreeRootChecker(int next_due_time_in_ms, int due_time_increase_step):
      next_due_time_in_ms_(next_due_time_in_ms)
      , due_time_increase_step_(due_time_increase_step){}
    ~TreeRootChecker() {
      if (timeout_) {
        timeout_->CleanUp();
        timeout_.reset(NULL);
      }
    }

    void StartupCheck() {
      timeout_.reset(new OsTimeOut(next_due_time_in_ms_, this));
      timeout_->Initialize();
    }

    /* implement IOnTimer*/
    virtual void OnTimer() {
      vector<int32_t> trees;
      err_t zisync_ret = GetRootMovedTrees(&trees);
      if (zisync_ret == ZISYNC_SUCCESS) {
        for(auto it = trees.begin(); it != trees.end(); ++it) {
          IssueRefresh(*it);
        }
      }

      next_due_time_in_ms_  += due_time_increase_step_;
      if (timeout_) {
        timeout_->CleanUp();
        timeout_.reset(NULL);
      }
      if (!ShouldResign()) {
        timeout_.reset(new OsTimeOut(next_due_time_in_ms_, this));
        timeout_->Initialize();
      }
    }
  private:
    unique_ptr<OsTimeOut> timeout_;
    int next_due_time_in_ms_;
    int due_time_increase_step_;

    bool ShouldResign() const {
      if (next_due_time_in_ms_ > REFRESH_INTERVAL_IN_MS) {
        return true;
      }
      vector<int32_t> trees;
      err_t zisync_ret = GetRootMovedTrees(&trees);
      if (zisync_ret != ZISYNC_SUCCESS) {
        //sth bad Happend, we quit in case to make things worse
        ZSLOG_ERROR("GetRootMovedTrees failed.");
        assert(false);
        return true;
      }else {
        if (trees.size() == 0) {
          return true;
        }
      }
      return false;
    }

    err_t GetRootMovedTrees(vector<int32_t> *trees) const {
      assert(trees);
      trees->clear();
      IContentResolver *resolver = GetContentResolver();
      const char * projs[] = {
        TableTree::COLUMN_ID,
      };
      unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, projs, ARRAY_SIZE(projs)
            , "%s = %" PRId32 " and %s = %" PRId32
            , TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_REMOVED
            , TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
      while (tree_cursor->MoveToNext()) {
        trees->push_back(tree_cursor->GetInt32(0));
      }

      return ZISYNC_SUCCESS;
    }
};

class ZiSyncKernel : public IZiSyncKernel {
 public:
  ZiSyncKernel(): 
    has_content_initialized(false), 
    device_info_timer(DEVICE_INFO_INTERVAL_IN_MS, &device_info_on_timer),
    refresh_on_timer(),
    refresh_timer(REFRESH_INTERVAL_IN_MS, &refresh_on_timer),
    sync_on_timer(), 
    device_ip_check_timer(
        DEVICE_NO_RESP_OFFLINE_CHECK_INTERVAL_IN_MS, 
        &device_ip_check_on_timer),
    background_on_timer(this),
    has_startup(false), is_background(false) {
      if (IsMobileDevice()) {
        sync_timer.reset(new OsTimer(
                MOBILE_DEVICE_SYNC_INTERVAL_IN_MS, &sync_on_timer));
      } else {
        sync_timer.reset(new OsTimer(SYNC_INTERVAL_IN_MS, &sync_on_timer));
      }
      background_timer.reset(new OsTimer(BACKGROUND_INTERVAL_IN_MS, &background_on_timer));
    }
  virtual ~ZiSyncKernel() {}

  err_t InitContentFramework(const vector<string> *mtokens = NULL) {
    if (!has_content_initialized) {
      err_t eno = StartupContentFramework(Config::database_dir().c_str()
          , mtokens);
      if (eno == ZISYNC_SUCCESS) {
        has_content_initialized = true;
      }
      return eno;
    }
    return ZISYNC_SUCCESS;
  }


  virtual err_t Initialize(const char* appdata,
                           const char* username,
                           const char* password, 
                           const char* backup_root = NULL,
                           const vector<string>* mtokens = NULL);

  virtual err_t Startup(const char* appdata, 
                        int32_t discover_port,
                        IEventListener *listener,
                        const char *tree_root_prefix = NULL,
                        const vector<string>* mtokens = NULL);
  virtual void Shutdown();

  virtual const string GetDeviceUuid();
  virtual const string GetDeviceName();
  virtual err_t SetDeviceName(const char* device_name);

  virtual const string GetAccountName();
  virtual err_t SetAccount(
      const char* username, const char* password);

  virtual int32_t GetDiscoverPort();
  virtual int32_t GetRoutePort();
  virtual int32_t GetDataPort();

  virtual err_t SetDiscoverPort(int32_t new_port);
  virtual err_t SetRoutePort(int32_t new_port);
  virtual err_t SetDataPort(int32_t new_port);

  virtual int32_t GetTransferThreadCount();
  virtual err_t SetTransferThreadCount(int32_t new_thread_count);

  virtual int64_t GetUploadLimit();
  virtual int64_t GetDownloadLimit();
  virtual err_t SetUploadLimit(int64_t bytes_per_second);
  virtual err_t SetDownloadLimit(int64_t bytes_per_second);

  virtual int32_t GetSyncInterval();
  virtual err_t SetSyncInterval(int32_t interval_in_second);
  virtual err_t SetDownloadCacheVolume(int64_t new_volume);
  virtual int64_t GetDownloadCacheVolume();
  virtual int64_t GetDownloadCacheAmount();
  virtual err_t CleanUpDownloadCache();
  
  virtual err_t RemoveRemoteFile(int32_t sync_id, const std::string &path);

  virtual err_t QuerySyncInfo(QuerySyncInfoResult* result);
  virtual err_t QuerySyncInfo(int32_t sync_id, SyncInfo *sync_info);

  virtual err_t CreateSync( const char* sync_name,
                           SyncInfo* sync_info);
  virtual err_t DestroySync(int32_t sync_id);
  virtual err_t ImportSync( const string& blob,
                           SyncInfo* sync_info);
  virtual err_t ExportSync(int32_t sync_id, std::string* blob);
#ifdef __APPLE__
  virtual err_t ListSync(int32_t sync_id, const std::string& path,
                         int64_t *list_sync_id);
#else
  virtual err_t ListSync(int32_t sync_id, const std::string& path,
                         ListSyncResult *result);
#endif
  virtual err_t CreateTree( int32_t sync_id,
                            const char* tree_root,
                           TreeInfo* tree_info);
  virtual err_t DestroyTree(int32_t tree_id);
  virtual err_t QueryTreeInfo(int32_t tree_id, TreeInfo *tree_info);
  virtual err_t SetTreeRoot(int32_t tree_id, const char *tree_root);
  virtual err_t AddFavorite(
      int32_t tree_id, const char* favorite);
  virtual err_t DelFavorite(
      int32_t tree_id, const char* favorite);
  virtual int GetFavoriteStatus(int32_t tree_id, const char *path);
  virtual bool HasFavorite(int32_t tree_id);
  virtual err_t SyncOnce(int32_t sync_id);
  virtual err_t QueryTreeStatus(int32_t tree_id, TreeStatus* tree_status);
  virtual err_t QueryTreePairStatus(
      int32_t local_tree_id, int32_t remote_tree_id, 
      TreePairStatus* tree_status);
  virtual err_t QueryTransferList(
      int32_t tree_id, TransferListStatus *transfer_list,
      int32_t offset, int32_t max_num);
  virtual err_t QueryHistoryInfo(QueryHistoryResult *histories
      , int32_t offset, int32_t limit, int32_t backup_type);
  virtual err_t StartupDiscoverDevice(
      int32_t sync_id, int32_t *discover_id);
  virtual err_t GetDiscoveredDevice(
      int32_t discover_id, DiscoverDeviceResult *result);
  virtual err_t ShutdownDiscoverDevice(int32_t discover_id);
  virtual err_t ShareSync(
      int32_t discover_id, int32_t device_id, int32_t sync_perm);
  virtual err_t CancelShareSync(int32_t device_id, int32_t sync_id);
  virtual err_t DisconnectShareSync(int32_t sync_id);
  virtual err_t SetShareSyncPerm(
      int32_t device_id, int32_t sync_id, int32_t sync_perm);
  virtual err_t GetShareSyncPerm(
      int32_t device_id, int32_t sync_id, int32_t *sync_perm);
  
  virtual err_t StartupDownload(
      int32_t sync_id, const std::string& relative_path, 
      string* target_path, int32_t *task_id);
  virtual err_t ShutdownDownload(int32_t task_id);
  virtual err_t QueryDownloadStatus(
      int32_t task_id, DownloadStatus *status);
  
  virtual err_t StartupUpload(
      int32_t sync_id, const std::string& relative_path, 
      const string& target_path, int32_t *task_id);
  virtual err_t ShutdownUpload(int32_t task_id);
  virtual err_t QueryUploadStatus(
      int32_t task_id, UploadStatus *status);
  virtual err_t Verify(const std::string &key);
  virtual VerifyStatus_t VerifyStatus();
  virtual err_t QueryLicencesInfo(
      struct LicencesInfo *licences);
  virtual err_t Bind(const std::string &key);
  virtual err_t Unbind();
  virtual bool CheckPerm(__IN UserPermission_t perm, __IN int32_t data);

  virtual err_t SetBackground(int32_t interval = 300);
  virtual err_t SetForeground();
  virtual err_t QueryBackupInfo(QueryBackupInfoResult* result);
  virtual err_t QueryBackupInfo(int32_t backup_id, BackupInfo *backup_info);
  virtual err_t CreateBackup(
       const char* backup_name, const char *root,
       BackupInfo* backup_info);
  virtual err_t DestroyBackup(int32_t sync_id);
  virtual err_t AddBackupTargetDevice(
      int32_t backup_id, int32_t device_id, 
      TreeInfo *remote_backup_tree, const char *backup_root = NULL);
  virtual err_t DelBackupTargetDevice(int32_t backup_id, int32_t device_id);
  virtual err_t ListBackupTargetDevice(
      int32_t backup_id, ListBackupTargetDeviceResult *result);
  
  virtual err_t AddBackupTarget(
      int32_t backup_id, int32_t device_id, 
      TreeInfo *remote_backup_tree, const char *backup_root = NULL);
  virtual err_t DelBackupTarget(int32_t dst_tree_id);
  
  virtual err_t DisableSync(int32_t tree_id);
  virtual err_t EnableSync(int32_t tree_id);

  virtual std::string GetBackupRoot();
  virtual err_t SetBackupRoot(const std::string &root);
  
  virtual err_t SetSyncMode(
      int32_t local_tree_id, int32_t remote_tree_id,
      int sync_mode, int32_t sync_time_in_s = -1);
  virtual err_t GetSyncMode(
      int32_t local_tree_id, int32_t remote_tree_id,
      int *sync_mode, int32_t *sync_time_in_s = NULL);
  virtual err_t SetLocalDeviceAsCreator(int32_t sync_id);
  virtual int32_t GetVersion() {
    return Config::version();
  }

  virtual err_t QueryUserPermission(std::map<UserPermission_t, int32_t> *perms);
  
  virtual err_t GetStaticPeers(__OUT ListStaticPeers* peers);
  virtual err_t AddStaticPeers(__IN const ListStaticPeers& peers);
  virtual err_t DeleteStaticPeers(__IN const ListStaticPeers& peers);
  virtual err_t SaveStaticPeers(__IN const ListStaticPeers &peers);

  virtual err_t VerifyCDKey(__IN const std::string &key);
  virtual err_t SetSyncPerm(int32_t sync_id, int32_t perm);

  virtual err_t Feedback(const std::string &type,
                         const std::string &version,
                         const std::string &message,
                         const std::string &contact);
 private:
  class DeviceInfoOnTimer : public IOnTimer {
   public:
    DeviceInfoOnTimer():last_time_in_s_(-1) {}
    virtual void OnTimer();
   private:
    int64_t last_time_in_s_;
  };
  class RefreshOnTimer : public IOnTimer {
   public:
    RefreshOnTimer() {}
    virtual void OnTimer();
  };
  class SyncOnTimer : public IOnTimer {
   public:
    SyncOnTimer():last_time_in_s_(-1) {}
    virtual void OnTimer();
   private:
    int64_t last_time_in_s_;
  };
  class DeviceIPCheckOnTimer : public IOnTimer {
   public:
    DeviceIPCheckOnTimer() {}
    virtual void OnTimer() {
      DeviceIPCheck();
    }
  };
  class ReportOnTimer : public IOnTimer {
   public:
    ReportOnTimer() {}
    virtual void OnTimer();
  };
  class BackgroundOnTimer : public IOnTimer {
   public:
    BackgroundOnTimer(ZiSyncKernel *kernel):kernel_(kernel) {}
    virtual void OnTimer();
   private:
    ZiSyncKernel *kernel_;
  };
  RwLock rwlock;
  unique_ptr<Router> router;
  bool has_content_initialized;
  unique_ptr<ZmqSocket> exit_pub;
  // bool has_initialized;
  vector<unique_ptr<RefreshWorker>> refresh_workers;
  vector<unique_ptr<SyncWorker>> sync_workers;
  vector<unique_ptr<OuterWorker>> outer_workers;
  vector<unique_ptr<InnerWorker>> inner_workers;
  // key is tree_id

  DeviceInfoOnTimer device_info_on_timer;
  OsTimer device_info_timer;
  RefreshOnTimer refresh_on_timer;
  OsTimer refresh_timer;
  SyncOnTimer sync_on_timer;
  unique_ptr<OsTimer> sync_timer;
  DeviceIPCheckOnTimer device_ip_check_on_timer;
  OsTimer device_ip_check_timer;
  BackgroundOnTimer background_on_timer;
  unique_ptr<OsTimer> background_timer;
  unique_ptr<TreeRootChecker> tree_root_checker;

  bool has_startup;
  bool is_background;

  bool HasStartup() {
    return has_startup;
  }
  err_t SendStartupRefreshRequests();
  err_t StartupIntern(
      const char *appdata, int32_t discover_port, 
      const char *tree_root_prefix, const vector<string> *mtokens);
  void ShutdownIntern();
};

static ZiSyncKernel kernel;

/*  not thread safe */
IZiSyncKernel* GetZiSyncKernel(const char* type) {
  if (strcmp(type, "virtual") == 0) {
    /*  @TODO who impleted virtual kernel replace the code here */
    assert(false);
    return NULL;
  } else if (strcmp(type, "actual") == 0) {
    return &kernel;
  } else {
    assert(false);
    return NULL;
  }
}

static inline err_t SyncOnceIntern(
    int32_t sync_id, bool is_manual) {
  vector<int32_t> local_tree_ids;
  vector<int32_t> tree_ids;
  IContentResolver *resolver = GetContentResolver();

  const char *tree_projs[] = {
    TableTree::COLUMN_ID, TableTree::COLUMN_DEVICE_ID, 
    TableTree::COLUMN_IS_ENABLED, 
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %" PRId32 " AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));//todo: should filter out root_removed trees
  while (tree_cursor->MoveToNext()) {
    int32_t tree_id = tree_cursor->GetInt32(0);
    int32_t device_id = tree_cursor->GetInt32(1);
    if (device_id == TableDevice::LOCAL_DEVICE_ID) {
      bool is_sync_enabled = tree_cursor->GetBool(2);
      if (is_sync_enabled) {
        local_tree_ids.push_back(tree_id);
        tree_ids.push_back(tree_id);
      }
    } else {
      tree_ids.push_back(tree_id);
    }
  }

  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = req.Connect(zs::router_sync_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  SyncRequest request;
  MsgSyncRequest *request_msg = request.mutable_request();
  request_msg->set_is_manual(is_manual);
  for (auto local_iter = local_tree_ids.begin(); 
       local_iter != local_tree_ids.end(); local_iter ++) {
    request_msg->set_local_tree_id(*local_iter);
    /*  the local tree may be disabled by DisableAutoSync 
     *  , so we need to call AbortAddTree here */
    for (auto remote_iter = tree_ids.begin();
         remote_iter != tree_ids.end(); remote_iter ++) {
      if (*remote_iter != *local_iter) {
        if (is_manual) {
          zs::AbortAddSyncTree(*local_iter, *remote_iter);
        }
        request_msg->set_remote_tree_id(*remote_iter);

        zisync_ret = request.SendTo(req);
        assert(zisync_ret == ZISYNC_SUCCESS);
      }
    }
  }
  return ZISYNC_SUCCESS;
}


void ZiSyncKernel::DeviceInfoOnTimer::OnTimer() {
  int64_t cur_time = OsTimeInS();
  if (last_time_in_s_ != -1 && 
      cur_time - last_time_in_s_ >= 0 && 
      cur_time - last_time_in_s_ < DEVICE_INFO_INTERVAL_IN_MS / 2000) {
    return;
  }
  last_time_in_s_ = cur_time;
  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = req.Connect(zs::router_inner_pull_fronter_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connect to (%s) fail.", router_inner_pull_fronter_uri);
    return;
  }

  IssueDeviceInfoRequest request;
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);

  IssuePushDeviceInfoRequest push_request;
  zisync_ret = push_request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void ZiSyncKernel::RefreshOnTimer::OnTimer() {
  IssueAllRefresh();
}

void ZiSyncKernel::SyncOnTimer::OnTimer() {
#ifdef ZS_TEST
  if (!Config::is_auto_sync_enabled()) {
    return;
  }
#endif
  int64_t cur_time = OsTimeInS();
  if (last_time_in_s_ != -1 && 
      cur_time - last_time_in_s_ >= 0 && 
      cur_time - last_time_in_s_ < SYNC_INTERVAL_IN_MS / 2000) {
    return;
  }
  last_time_in_s_ = cur_time;
  IContentResolver *resolver = GetContentResolver();
  const char *sync_projs[] = {
    TableSync::COLUMN_ID,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
          "%s = %d", TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));

  while (sync_cursor->MoveToNext()) {
    int32_t sync_id = sync_cursor->GetInt32(0);
    SyncOnceIntern(sync_id, false);
  }
}

void ZiSyncKernel::ReportOnTimer::OnTimer() {
}

void ZiSyncKernel::BackgroundOnTimer::OnTimer() {
  class BackgroundHelper {
   public:
    BackgroundHelper(ZiSyncKernel *kernel):kernel_(kernel) {}
    ~BackgroundHelper() {
      if (kernel_->is_background) {
        IssueDiscoverSetBackground();
        IssueRouteShutdown();
      }
    }
   private:
    ZiSyncKernel *kernel_;
  };
  ZSLOG_INFO("Start BackgroundOnTimer");
  BackgroundHelper helper(kernel_);
  err_t zisync_ret = IssueRouteStartup();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_WARNING("IssueRouteStartup fail : %s", zisync_strerror(zisync_ret));
    return;
  }
  IssueDiscoverSetForeground();
  kernel_->device_info_on_timer.OnTimer();
  kernel_->sync_on_timer.OnTimer();
  sleep(BACKGROUND_ALIVE_TIME_IN_S);
  ZSLOG_INFO("End BackgroundOnTimer");
}

static inline err_t InitHomeDir() {
  if (!OsDirExists(Config::home_dir())) {
    int ret = OsCreateDirectory(Config::home_dir(), false);
    if (ret == -1) {
      ZSLOG_ERROR("Create home_dir(%s) fail : %s",
                  Config::home_dir().c_str(), OsGetLastErr());
      return ZISYNC_ERROR_CONFIG;
    }
  }
  return ZISYNC_SUCCESS;
  // Database dir is created in ContentProvider->OnCreate
}

static inline bool IsUsedTcpPort(int32_t port) {
  string addr;
  StringFormat(&addr, "tcp://*:%" PRId32, port);
  OsTcpSocket socket(addr);
  int ret = socket.Bind();
  return ret == 0 ? false : true;
}

static inline bool IsUsedUdpPort(int32_t port) {
  string addr;
  StringFormat(&addr, "udp://*:%" PRId32, port);
  OsUdpSocket socket(addr);
  int ret = socket.Bind();
  return ret == 0 ? false : true;
}

err_t ZiSyncKernel::Initialize(const char *app_data,
                               const char* username,
                               const char* password,
                               const char *backup_root_,/* = NULL */
                               const vector<string>* mtokens) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  RwLockWrAuto wr_auto(&rwlock);
  if (HasStartup()) {
    ZSLOG_ERROR("You can not call Initialize after call startup");
    return ZISYNC_ERROR_GENERAL;
  }

  std::vector<std::string> vec(1, std::string("test-key"));
  if (!mtokens) {
    mtokens = &vec;
  }

  string app_data_ = app_data;
  if (!IsAbsolutePath(app_data_) || !NormalizePath(&app_data_)) {
    ZSLOG_ERROR("Invalid path of app_data(%s)", app_data);
    return ZISYNC_ERROR_INVALID_PATH;
  }

  if (!PlainConfig::HasInit()) {
    PlainConfig::Initialize(app_data, *mtokens);
  }

  string backup_root;
  if (backup_root_ != NULL) {
    backup_root = backup_root_;
    if (!IsAbsolutePath(backup_root) || !NormalizePath(&backup_root)) {
      ZSLOG_ERROR("Invalid path of backup_root(%s)", backup_root_);
      return ZISYNC_ERROR_INVALID_PATH;
    }
  }

  Config::SetHomeDir(app_data_.c_str());
  InitHomeDir();
  if (!OsDirExists(app_data)) {
    ZSLOG_ERROR("AppData(%s) does not exist", app_data);
    return ZISYNC_ERROR_DIR_NOENT;
  }

  zisync_ret = InitContentFramework(mtokens);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("InitContentFramework fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  /* call before DatabaseInit, because DabaseInit will use backup_root */
  if (backup_root_ != NULL) {
    assert(!IsMobileDevice());
    if (!OsDirExists(backup_root)) {
      ZSLOG_ERROR("BackupRoot(%s) does not exist", backup_root.c_str());
      return ZISYNC_ERROR_DIR_NOENT;
    }
    zisync_ret = SaveBackupRoot(backup_root.c_str());
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("SaveBackupRoot fail : %s", zisync_strerror(zisync_ret));
      return zisync_ret;
    }
  }

  /* clear database */
  zisync_ret = DatabaseInit();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Clear Database : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  zisync_ret = SaveAccount(username, password);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SaveAccount fail.");
    return zisync_ret;
  }

  // } else {
  //  assert(IsMobileDevice());
  //}
  return ZISYNC_SUCCESS;
}

static inline err_t UpdateTreeRoot(const string &tree_root_prefix) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID, TableTree::COLUMN_ROOT,
  };

  const string old_tree_root_prefix = Config::tree_root_prefix();
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));

  OperationList op_list;
  while (tree_cursor->MoveToNext()) {
    int32_t tree_id = tree_cursor->GetInt32(0);
    string tree_root = tree_cursor->GetString(1);
    string new_tree_root;
    size_t offset = old_tree_root_prefix.empty() ? 
        tree_root_prefix.length() : old_tree_root_prefix.length();
    if (offset < tree_root.length()) {
      new_tree_root = tree_root_prefix + tree_root.substr(offset);
      ContentOperation *co = op_list.NewUpdate(
          TableTree::URI, "%s = %d", TableTree::COLUMN_ID, tree_id);
      ContentValues *cv = co->GetContentValues();
      cv->Put(TableTree::COLUMN_ROOT, new_tree_root, true);
    }
  }
  if (op_list.GetCount() > 0) {
    int affected_row_num = resolver->ApplyBatch(
        ContentProvider::AUTHORITY, &op_list);
    if (affected_row_num != op_list.GetCount()) {
      ZSLOG_ERROR("UpdateTreeRoot fail");
      return ZISYNC_ERROR_CONTENT;
    }
  }
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::StartupIntern(
    const char *appdata, int32_t discover_port, 
    const char *tree_root_prefix, const vector<string> *mtokens) {
#ifdef _WIN32
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
  wVersionRequested = MAKEWORD(2, 2);

  err = WSAStartup(wVersionRequested, &wsaData);
  if (err != 0) {
    return ZISYNC_ERROR_OS_SOCKET;
  }
#endif
  GlobalContextInit();
  /* TODO if data port and route occupy, dynamic set one */
  err_t zisync_ret = ZISYNC_SUCCESS;
  if (router) {
    return ZISYNC_SUCCESS;
  }
  string app_data_ = appdata;
  if (!IsAbsolutePath(app_data_) || !NormalizePath(&app_data_)) {
    ZSLOG_ERROR("Invalid path of app_data(%s)", appdata);
    return ZISYNC_ERROR_INVALID_PATH;
  }

  Config::SetHomeDir(app_data_.c_str());
  if (!PlainConfig::HasInit()) {
    PlainConfig::Initialize(appdata, *mtokens);
  }
  zisync_ret = InitContentFramework(mtokens);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("InitContentFramework fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  InitDeviceStatus();

  zisync_ret = Config::ReadFromContent();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Read config from content fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  
  if (Config::report_host().empty()) {
    zisync_ret = SaveReportHost(kReportHost);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Save report host fail.");
      return zisync_ret;
    }
  }

  if (Config::ca_cert().empty()) {
    zisync_ret = SaveCAcert(CAcert);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Save ca cert fail.");
      return zisync_ret;
    }
  }

  if (mtokens->size() > 0) {
    zisync_ret = SaveMacToken(mtokens->at(0));
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Save mactoken fail.");
      return zisync_ret;
    }
  } else {
    assert(0);
  }

  /* Update tree_root */
  if (tree_root_prefix != NULL && 
      Config::tree_root_prefix() != tree_root_prefix) {
    zisync_ret = UpdateTreeRoot(tree_root_prefix);
    if (zisync_ret != ZISYNC_SUCCESS) {
      return zisync_ret;
    }
    zisync_ret = SaveTreeRootPrefix(tree_root_prefix);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("SaveTreeRootPrefix fail : %s", 
                  zisync_strerror(zisync_ret));
      return zisync_ret;
    }
  }
  
  zisync_ret = zs::GetMaxUsnFromContent();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Get Max Usn from content fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

    // Configure discover port
  if (discover_port != -1) {
	  if (!IsValidPort(discover_port)) {
		  ZSLOG_ERROR("Invalid port(%" PRId32")", discover_port);
		  return ZISYNC_ERROR_INVALID_PORT;
	  }
	  if (IsUsedUdpPort(discover_port)) {
		  ZSLOG_ERROR("Port(%" PRId32") has been used.", discover_port);
		  return ZISYNC_ERROR_ADDRINUSE;
	  }
	  zisync_ret = SaveDiscoverPort(discover_port);
	  if (zisync_ret != ZISYNC_SUCCESS) {
		  ZSLOG_ERROR("SaveDiscoverPort fail.");
		  return zisync_ret;
	  }
	  Config::set_discover_port(discover_port);
  }

  // Configure Data port
  bool not_valid;
  int data_port = Config::DefaultDataPort;
  if ((not_valid = !IsValidPort(data_port)) || IsUsedTcpPort(data_port)) {
     if (not_valid) {
        ZSLOG_ERROR("Invalid port(%" PRId32")", data_port);
     } else {
        ZSLOG_ERROR("Port(%" PRId32") has been used.", data_port);
     }

     do {
       data_port = OsGetRandomTcpPort();
     } while(IsUsedTcpPort(data_port) && data_port != -1);

     if (data_port == -1) {
        ZSLOG_ERROR("OsGetRandomTcpPort() fail : %s", OsGetLastErr());
        return ZISYNC_ERROR_OS_SOCKET;
     }
     
  }

  if (data_port != Config::data_port()) {
    zisync_ret = SaveDataPort(data_port);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("SaveDataPort() fail.");
      return zisync_ret;
    }
    Config::set_data_port(data_port);
  }

  if (!exit_pub) {
    exit_pub.reset(new ZmqSocket(GetGlobalContext(), ZMQ_PUB));
    zisync_ret = exit_pub->Bind(exit_uri);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  zs::AbortInit();

  zisync_ret = QueryCache::GetInstance()->Startup();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("QueryCache::Startup fail: %s", 
      zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  // Init download handle
  if (!IDownload::GetInstance()->Initialize()) {
    ZSLOG_ERROR("Init Download fail");
    return ZISYNC_ERROR_GENERAL;
  }

  // Init & Start Router
  router.reset(new Router());
  zisync_ret = router->Startup();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Startup Router fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  zisync_ret = Monitor::GetMonitor()->Startup();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Startup MonitorListener fail : %s", 
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  // Init Discover virtual server
  IDiscoverServer* discover = IDiscoverServer::GetInstance();
  zisync_ret = discover->Initialize(Config::route_port());
  if (zisync_ret != ZISYNC_SUCCESS) {
	  return zisync_ret;
  }

  // Init Transfer virtual server
  int port = Config::data_port();
  ITransferServer* transfer_server = GetTransferServer2();
  zisync_ret = transfer_server->Initialize(
	  port, SyncTreeAgent::GetInstance(), NULL);
  assert(zisync_ret == ZISYNC_SUCCESS);

  IHistoryDataSource *history_datasource = new HistoryDataSource;
  IHistoryManager *history_manager = GetHistoryManager();
  zisync_ret = history_manager->Initialize(unique_ptr<IHistoryDataSource>(history_datasource));
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  // Start write db libevent
  GetEventBaseDb()->RegisterVirtualServer(GetHistoryManager());
  GetEventBaseDb()->Startup();

  // Register & Start all virtual server
  ILibEventBase* event_base = GetEventBase();
  event_base->RegisterVirtualServer(transfer_server);
  zisync_ret = ReportDataServer::GetInstance()->Initialize(NULL);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  event_base->RegisterVirtualServer(ReportDataServer::GetInstance());
  event_base->RegisterVirtualServer(UiEventServer::GetInstance());
  event_base->RegisterVirtualServer(DiscoverServer::GetInstance());
  event_base->Startup(); 
  // init permissions
//  GetLicences()->Initialize();
//  zisync_ret = GetPermission()->Initialize(mtokens);
//  if (zisync_ret != ZISYNC_SUCCESS) {
//    return zisync_ret;
//  }


  zisync_ret = Tree::InitAllTreesModules();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("InitAllTreesModules fail: %s", 
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  zs::IDownload::GetInstance()->Initialize();

  for (int i = 0; i < Config::refresh_workers_num(); i ++) {
    refresh_workers.emplace_back(new RefreshWorker);
    zisync_ret = refresh_workers[i]->Startup();
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  for (int i = 0; i < Config::sync_workers_num(); i ++) {
    sync_workers.emplace_back(new SyncWorker);
    zisync_ret = sync_workers[i]->Startup();
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  for (int i = 0; i < Config::outer_workers_num(); i ++) {
    outer_workers.emplace_back(new OuterWorker);
    zisync_ret = outer_workers[i]->Startup();
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  for (int i = 0; i < Config::inner_workers_num(); i ++) {
    inner_workers.emplace_back(new InnerWorker);
    zisync_ret = inner_workers[i]->Startup();
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  device_info_on_timer.OnTimer();
  int ret = device_info_timer.Initialize();
  if (ret != 0) {
    ZSLOG_ERROR("Initialize device_info_timer fail : %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_TIMER;
  }
  refresh_on_timer.OnTimer();
  ret = refresh_timer.Initialize();
  if (ret != 0) {
    ZSLOG_ERROR("Initialize refresh_timer fail : %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_TIMER;
  }

  if (GetPlatform() != PLATFORM_ANDROID &&
      GetPlatform() != PLATFORM_IOS) {
    sync_on_timer.OnTimer();
    ret = sync_timer->Initialize();
    if (ret != 0) {
      ZSLOG_ERROR("Initialize sync_timer fail : %s", OsGetLastErr());
      return ZISYNC_ERROR_OS_TIMER;
    }
  }

  ret = device_ip_check_timer.Initialize();
  if (ret != 0) {
    ZSLOG_ERROR("Initialize device_ip_check_timer fail : %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_TIMER;
  }

  tree_root_checker.reset(new TreeRootChecker(2000, 3000));
  tree_root_checker->StartupCheck();

#ifdef _WIN32
  // zisync_ret = GetUIEventMonitor()->Initialize();
  // zisync_ret = UiEventServer::GetInstance()->
  // if (zisync_ret != ZISYNC_SUCCESS) {
  //   ZSLOG_ERROR("ReportUIMonitor Initialize fail : %s",
  //               zisync_strerror(zisync_ret));
  // }
#endif

  return ZISYNC_SUCCESS;

}

err_t ZiSyncKernel::Startup(
    const char *appdata, int32_t discover_port, IEventListener *listener,
    const char *tree_root_prefix /* = NULL */, const vector<string> *mtokens ) {
  RwLockWrAuto wr_auto(&rwlock);
#ifndef _WIN32
  // sighandler_t ret = signal(SIGPIPE, SIG_IGN);
  // assert(ret != SIG_ERR);
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, 0) == -1) {
        ZSLOG_ERROR("failed to ignore sig pipe");
    }
    
#endif  // _WIN32
  ZSLOG_INFO("Start Startup");
  if (HasStartup()) {
    return ZISYNC_SUCCESS;
  }

  std::vector<std::string> vec(1, std::string("test-key"));
  if (!mtokens) {
    mtokens = &vec;
  }

  zs::GetEventNotifier()->SetEventListner(listener);
  err_t zisync_ret = StartupIntern(appdata, discover_port, tree_root_prefix, mtokens);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ShutdownIntern();
    return zisync_ret;
  }
  GetPermission()->UseDefaultPermissions();
  has_startup = true;
  ZSLOG_INFO("End Startup");
  return zisync_ret;
}

void IssueAnnounceExit() {
  IContentResolver *resolver = GetContentResolver();
  vector<string> uris;

  const char *device_projs[] = {
    TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT, 
  };
  const char *device_ip_projs[] = {
    TableDeviceIP::COLUMN_IP,
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
          "%s = %d AND %s = %d", 
          TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE,
          TableDevice::COLUMN_IS_MINE, true));

  AnnounceExitRequest request;
  request.mutable_request()->set_device_uuid(Config::device_uuid());
  string uri;

  while (device_cursor->MoveToNext()) {
    int32_t device_id = device_cursor->GetInt32(0);
    int32_t route_port = device_cursor->GetInt32(1);
    unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
            TableDeviceIP::URI, device_ip_projs, ARRAY_SIZE(device_ip_projs),
            "%s = %d", TableDeviceIP::COLUMN_DEVICE_ID, device_id));
    while (device_ip_cursor->MoveToNext()) {
      StringFormat(&uri, "tcp://%s:%d", device_ip_cursor->GetString(0), 
                   route_port);
      ZmqSocket req(GetGlobalContext(), ZMQ_REQ, 1000);
      err_t zisync_ret = req.Connect(uri.c_str());
      assert(zisync_ret == ZISYNC_SUCCESS);
      zisync_ret = request.SendTo(req);
      assert(zisync_ret == ZISYNC_SUCCESS);
    }
  }
}

void ZiSyncKernel::Shutdown() {
  RwLockWrAuto wr_auto(&rwlock);

  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call Shutdown before call startup");
    return;
  }
  
  ShutdownIntern();
}

void ZiSyncKernel::ShutdownIntern() {
  if (exit_pub != NULL) {
    err_t zisync_ret;
    ZmqMsg exit_msg;
    zisync_ret = exit_msg.SendTo(*exit_pub, 0);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  zs::Abort();

//#ifdef _WIN32
//  GetUIEventMonitor()->CleanUp();
//#endif
    
  sync_timer->CleanUp();
  refresh_timer.CleanUp();
  device_info_timer.CleanUp();
  background_timer->CleanUp();

  // Shutdown virtual servers
  ILibEventBase* event_base = GetEventBase();
  event_base->Shutdown(); 

  // Shutdown db libevent
  ILibEventBase* event_base_db = GetEventBaseDb();
  event_base_db->Shutdown();

  for (auto iter = inner_workers.begin(); iter != inner_workers.end(); 
       iter ++) {
    (*iter)->Shutdown();
  }
  inner_workers.clear();
  for (auto iter = outer_workers.begin(); iter != outer_workers.end(); 
       iter ++) {
    (*iter)->Shutdown();
  }
  outer_workers.clear();
  for (auto iter = sync_workers.begin(); iter != sync_workers.end(); iter ++) {
    (*iter)->Shutdown();
  }
  sync_workers.clear();
  for (auto iter = refresh_workers.begin(); iter != refresh_workers.end(); 
       iter ++) {
    (*iter)->Shutdown();
  }
  refresh_workers.clear();

  /*  should call this before transfer_server */
  zs::IDownload::GetInstance()->CleanUp();
  zs::IUpload::GetInstance()->CleanUp();
  zs::ReportDataServer::GetInstance()->CleanUp();


  // Unregister all virtual server
  event_base->UnregisterVirtualServer(GetTransferServer2());
  event_base->UnregisterVirtualServer(UiEventServer::GetInstance());
  event_base->UnregisterVirtualServer(ReportDataServer::GetInstance());
  event_base->UnregisterVirtualServer(DiscoverServer::GetInstance());

  event_base_db->UnregisterVirtualServer(GetHistoryManager());
  
  // Cleanup Virtual server
  GetTransferServer2()->CleanUp();
  IDiscoverServer::GetInstance()->CleanUp();
  GetHistoryManager()->CleanUp();

//  ITreeManager* tree_manager = GetTreeManager();
//  tree_manager->Shutdown();
  QueryCache::GetInstance()->Shutdown();

  Monitor::GetMonitor()->Shutdown();
  if (router) {
    router->Shutdown();
    router.reset(NULL);
  }
  exit_pub.reset(NULL);

  IssueAnnounceExit();

  if (has_content_initialized) {
    ShutdownContentFramework();
    has_content_initialized = false;
  }
  GetEventNotifier()->SetEventListner(NULL);
  zs::IDiscoverDeviceHandler::GetInstance()->CleanUp();

  GlobalContextFinal();

#ifdef _WIN32
  WSACleanup();
#endif
  has_startup = false;
}

const string ZiSyncKernel::GetDeviceUuid() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDeviceUuid before call startup");
    return string();
  }
  return Config::device_uuid();
}
const string ZiSyncKernel::GetDeviceName() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDeviceName before call startup");
    return string();
  }
  return Config::device_name();
}

err_t ZiSyncKernel::SetDeviceName(const char* device_name) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetDeviceName before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  err_t zisync_ret = SaveDeviceName(device_name);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  IssuePushDeviceMeta();
  return ZISYNC_SUCCESS;
}

const string ZiSyncKernel::GetAccountName() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetAccountName before call startup");
    return string();
  }
  return Config::account_name();
}

err_t ZiSyncKernel::SetAccount(
    const char* username, const char* password) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetAccount before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  password = "";
  ZSLOG_ERROR("Begin SetAccount");

  bool account_changed = false;
  if (Config::account_name() != username || 
      Config::account_passwd() != password) {
    account_changed = true;
  }
  string old_account_name = Config::account_name();

  err_t zisync_ret = SaveAccount(username, password);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SaveAccount fail.");
    return zisync_ret;
  }

  if (account_changed) {
    IssueTokenChanged(old_account_name);
  }

  ZSLOG_ERROR("SetAccount OK");
  return ZISYNC_SUCCESS;
}

int32_t ZiSyncKernel::GetDiscoverPort() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDiscoverPort before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return Config::discover_port();
}
int32_t ZiSyncKernel::GetRoutePort() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetRoutePort before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return Config::route_port();
}
int32_t ZiSyncKernel::GetDataPort() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDataPort before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return Config::data_port();
}

err_t ZiSyncKernel::SetDiscoverPort(int32_t new_port) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetDiscoverPort before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (new_port == Config::discover_port()) {
    return ZISYNC_SUCCESS;
  }

  if (!IsValidPort(new_port)) {
    ZSLOG_ERROR("Invalid port(%" PRId32")", new_port);
    return ZISYNC_ERROR_INVALID_PORT;
  }
  if (IsUsedUdpPort(new_port)) {
    ZSLOG_ERROR("Port(%" PRId32") has been used.", new_port);
    return ZISYNC_ERROR_ADDRINUSE;
  }

  err_t zisync_ret = IDiscoverServer::GetInstance()->SetDiscoverPort(new_port);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SetDiscoverPort fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  IssueDiscoverBroadcast();

  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetRoutePort(int32_t new_port) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetRoutePort before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (new_port == Config::route_port()) {
    return ZISYNC_SUCCESS;
  }

  if (!IsValidPort(new_port)) {
    ZSLOG_ERROR("Invalid port(%" PRId32")", new_port);
    return ZISYNC_ERROR_INVALID_PORT;
  }
  if (IsUsedTcpPort(new_port)) {
    ZSLOG_ERROR("Port(%" PRId32") has been used.", new_port);
    return ZISYNC_ERROR_ADDRINUSE;
  }

  err_t zisync_ret;
  SetRoutePortRequest request;
  MsgSetRoutePortRequest *msg_request = request.mutable_request();
  msg_request->set_new_port(new_port);

  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  zisync_ret = req.Connect(router_cmd_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connet to inner fronter fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  zisync_ret = request.SendTo(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send SetRoutePortRequest fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  SetRoutePortResponse response;
  zisync_ret = response.RecvFrom(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SetRoutePort fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  IssuePushDeviceMeta();

  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetDataPort(int32_t new_port) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetDataPort before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (new_port == Config::data_port()) {
    return ZISYNC_SUCCESS;
  }

  if (!IsValidPort(new_port)) {
    ZSLOG_ERROR("Invalid port(%" PRId32")", new_port);
    return ZISYNC_ERROR_INVALID_PORT;
  }
  if (IsUsedTcpPort(new_port)) {
    ZSLOG_ERROR("Port(%" PRId32") has been used.", new_port);
    return ZISYNC_ERROR_ADDRINUSE;
  }

  if (new_port == Config::data_port()) {
    return ZISYNC_SUCCESS;
  }

  if (router != NULL) {
    ITransferServer *transfer_server = GetTransferServer2();
    err_t zisync_ret = transfer_server->SetPort(new_port);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("SetDataPort(%" PRId32 ") fail : %s", 
                  new_port, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
  }

  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableDevice::COLUMN_DATA_PORT, new_port);
  resolver->Update(
      TableDevice::URI, &cv, "%s = %d", 
      TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  Config::set_data_port(new_port);

  IssuePushDeviceMeta();

  return ZISYNC_SUCCESS;
}

int32_t ZiSyncKernel::GetTransferThreadCount() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetTransferThreadCount before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return 0;
}
err_t ZiSyncKernel::SetTransferThreadCount(int32_t new_thread_count) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetTransferThreadCount before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return ZISYNC_SUCCESS;
}
int64_t ZiSyncKernel::GetUploadLimit() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetUploadLimit before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return 0;
}
int64_t ZiSyncKernel::GetDownloadLimit() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDownloadLimit before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return 0;
}
err_t ZiSyncKernel::SetUploadLimit(int64_t bytes_per_second) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetUploadLimit before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return ZISYNC_SUCCESS;
}
err_t ZiSyncKernel::SetDownloadLimit(int64_t bytes_per_second) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetDownloadLimit before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return ZISYNC_SUCCESS;
}

int32_t ZiSyncKernel::GetSyncInterval() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetSyncInterval before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return Config::sync_interval();
}
err_t ZiSyncKernel::SetSyncInterval(int32_t interval_in_second) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetSyncInterval before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  IContentResolver* resolver = GetContentResolver();

  string interval;
  StringFormat(&interval, "%" PRId32, interval_in_second);
  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_SYNC_INTERVAL);
  cv.Put(TableConfig::COLUMN_VALUE, interval.c_str());
  int32_t row_id = resolver->Insert(TableConfig::URI, &cv, AOC_REPLACE);
  if (row_id < 0) {
    ZSLOG_ERROR("Insert sync_interval into Provider fail. Maybe you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  Config::set_discover_port(interval_in_second);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetDownloadCacheVolume(int64_t new_volume) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetDownloadCacheVolume before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  if (!IDownload::GetInstance()->ReduceDownloadCacheVolume(
          new_volume)) {
    ZSLOG_ERROR("ReduceDownloadCacheVolume(%" PRId64 " fail", new_volume);
    return ZISYNC_ERROR_GENERAL;
  }
  Config::set_download_cache_volume(new_volume);

  return ZISYNC_SUCCESS;
}

int64_t ZiSyncKernel::GetDownloadCacheVolume() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDownloadCacheVolume before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return Config::download_cache_volume();
}

int64_t ZiSyncKernel::GetDownloadCacheAmount() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDownloadCacheVolume before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IDownload::GetInstance()->GetDownloadCacheAmount();
}

err_t ZiSyncKernel::CleanUpDownloadCache() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call CleanUpDownloadCache before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IDownload::GetInstance()->CleanUpDownloadCache();
}

// static void ParseMsgTreeInfo(
//     const MsgTreeInfo &msg_tree_info, TreeInfo* tree_info) {
//   tree_info->tree_id = msg_tree_info.tree_id();
//   tree_info->tree_uuid.assign(msg_tree_info.tree_uuid());
//   tree_info->tree_root.assign(msg_tree_info.tree_root());
//   tree_info->device_name.assign(msg_tree_info.device_name());
// }


// static void ParseMsgSyncInfo(
//     const MsgSyncInfo &msg_sync_info, SyncInfo *sync_info) {
//   sync_info->sync_id = msg_sync_info.sync_id();
//   sync_info->sync_uuid.assign(msg_sync_info.sync_uuid());
//   sync_info->sync_name.assign(msg_sync_info.sync_name());
//   sync_info->last_sync = msg_sync_info.last_sync();
//   sync_info->trees.clear();
//   sync_info->trees.resize(msg_sync_info.trees_size());
//   for (int i = 0; i < msg_sync_info.trees_size(); i ++) {
//     ParseMsgTreeInfo(msg_sync_info.trees(i), &sync_info->trees[i]);
//   }
// }

err_t ZiSyncKernel::QuerySyncInfo(QuerySyncInfoResult* result) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QuerySyncInfo before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return QueryCache::GetInstance()->QuerySyncInfo(result);
}

err_t ZiSyncKernel::QuerySyncInfo(int32_t sync_id, SyncInfo *sync_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QuerySyncInfo before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return QueryCache::GetInstance()->QuerySyncInfo(sync_id, sync_info);
}

err_t ZiSyncKernel::QueryHistoryInfo(QueryHistoryResult *histories
    , int32_t offset, int32_t limit, int32_t backup_type) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryHistoryInfo before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (!GetPermission()->Verify(USER_PERMISSION_FUNC_HISTROY)) {
    ZSLOG_ERROR("You have no permission to use histroy.");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
  return GetHistoryManager()->QueryHistories(histories, offset, limit, backup_type);
}

err_t ZiSyncKernel::CreateSync( const char* sync_name,
                               SyncInfo* sync_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call CreateSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  int32_t count = GetCurrentTreeCount();
  if (count == -1 || !GetPermission()->Verify(
          USER_PERMISSION_CNT_CREATE_TREE, &count)) {
    ZSLOG_ERROR("You have no permission to create sync");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  NormalSync sync;
  err_t zisync_ret = sync.Create(sync_name);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Create Sync fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  
  IssuePushSyncInfo(sync.id());
  // QueryCache::GetInstance()->NotifySyncModify();

  sync.ToSyncInfo(sync_info);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::DestroySync(int32_t sync_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DestroySync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  err_t zisync_ret = Sync::DeleteById(sync_id);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("DestroySync fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  // QueryCache::GetInstance()->NotifySyncModify();
  IssuePushSyncInfo(sync_id);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::ImportSync( const string& blob,
                               SyncInfo* sync_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ImportSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  assert(false);
  MsgSyncBlob sync_blob;
  if (!sync_blob.ParseFromString(blob)) {
    ZSLOG_ERROR("Invalid blob.");
    return ZISYNC_ERROR_INVALID_SYNCBLOB;
  }

  return ZISYNC_SUCCESS;;

  // SyncAddRequest request;
  // MsgSyncAddRequest *request_msg = request.mutable_request();
  // request_msg->set_sync_name(sync_blob.sync_name());
  // request_msg->set_sync_uuid(sync_blob.sync_uuid());
  // request_msg->set_sync_type(ST_NORMAL);
  // err_t zisync_ret = request.Handle(NULL);
  // if (zisync_ret  != ZISYNC_SUCCESS) {
  //   ZSLOG_ERROR("Import Sync fail : %s", zisync_strerror(zisync_ret));
  //   return zisync_ret;
  // }

  // ParseMsgSyncInfo(request.response().sync(), sync_info);
  // return ZISYNC_SUCCESS;
}

// static inline bool GetListableDevice(int32_t device_id, int32_t *route_port) {
//   if(device_id == TableDevice::LOCAL_DEVICE_ID) {
//     return false;
//   }
//   IContentResolver *resolver = GetContentResolver();
//   const char *device_projs[] = {
//     TableDevice::COLUMN_TYPE, TableDevice::COLUMN_ROUTE_PORT,
//   };
//   unique_ptr<ICursor2> device_cursor(resolver->Query(
//           TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
//           "%s = %" PRId32 " AND %s = %d", 
//           TableDevice::COLUMN_ID, device_id,
//           TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
//   if (device_cursor->MoveToNext()) {
//     Platform platform = static_cast<Platform>(device_cursor->GetInt32(0));
//     if (!IsMobileDevice(platform)) {
//       if (route_port != NULL) {
//         *route_port = device_cursor->GetInt32(1);
//       }
//       return true;
//     }
//   }
//   return false;
// }
// 
// static inline void FindAndStoreRemoteMeta(
//     const string &sync_uuid, int32_t sync_id, 
//     const string &local_tree_uuid, string *remote_tree_uuid) {
//   IContentResolver *resolver = GetContentResolver();
//   const char *tree_projs[] = {
//     TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
//   };
//   const char* device_ip_projs[] = {
//     TableDeviceIP::COLUMN_IP,
//   };
// 
//   unique_ptr<ICursor2> tree_cursor(resolver->Query(
//           TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
//           "%s = %" PRId32 " AND %s = %d AND %s != %d", 
//           TableTree::COLUMN_SYNC_ID, sync_id,
//           TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, 
//           TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
//   string remote_route_uri;
//   IssueRequests<IssueFindRequest> issuse_find_requests(
//       WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
//   while (tree_cursor->MoveToNext()) {
//     int32_t device_id = tree_cursor->GetInt32(1);
//     const char *tree_uuid = tree_cursor->GetString(0);
//     int32_t remote_route_port;
//     if (GetListableDevice(device_id, &remote_route_port)) {
//       int64_t remote_since = 0;
//       err_t zisync_ret = GetTreeMaxUsnFromContent(tree_uuid, &remote_since);
//       if (zisync_ret != ZISYNC_SUCCESS) {
//         ZSLOG_ERROR("GetTreeMaxUsnFromContent(%s) fail : %s", 
//                     tree_uuid, zisync_strerror(zisync_ret));
//         continue;
//       }
// 
//       unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
//               TableDeviceIP::URI, device_ip_projs, 
//               ARRAY_SIZE(device_ip_projs), "%s = %d",
//               TableDeviceIP::COLUMN_DEVICE_ID, device_id));
// 
//       while (device_ip_cursor->MoveToNext()) {
//         IssueFindRequest *find_req = new IssueFindRequest(
//             sync_id, device_ip_cursor->GetString(0), 
//             remote_route_port, tree_uuid);
//         MsgFindRequest *msg_request = find_req->request.mutable_request();
//         msg_request->set_local_tree_uuid(local_tree_uuid);
//         msg_request->set_remote_tree_uuid(tree_uuid);
//         msg_request->set_sync_uuid(sync_uuid);
//         msg_request->set_limit(zs::FIND_LIMIT);
//         msg_request->set_since(remote_since);
//         issuse_find_requests.IssueOneRequest(find_req);
//       }
//     }
//   }
// 
//   while (true) {
//     if (zs::IsAborted()) {
//       return;
//     }
//     IssueFindRequest *find_request = 
//         issuse_find_requests.RecvNextResponsedRequest();
//     if (find_request == NULL) {
//       issuse_find_requests.UpdateDeviceInDatabase();
//       break;
//     } else {
//       if (find_request->error() != ZISYNC_SUCCESS) {
//         continue;
//       }
//     }
//     err_t zisync_ret = StoreRemoteMeta(
//         sync_id, find_request->remote_tree_uuid.c_str(), 
//         find_request->response.response().remote_meta());
//     if (zisync_ret != ZISYNC_SUCCESS) {
//       ZSLOG_WARNING("StoreFindResponse fail : %s", 
//                     zisync_strerror(zisync_ret));
//       continue;
//     }
//     ContentValues cv(1);
//     cv.Put(TableTree::COLUMN_LAST_FIND, zs::OsTimeInS());
//     resolver->Update(TableTree::URI, &cv, "%s = '%s'", 
//                      TableTree::COLUMN_UUID, 
//                      find_request->remote_tree_uuid.c_str());
// 
//     *remote_tree_uuid = find_request->remote_tree_uuid;
//     break;
//   }
// }
static inline void AddFindRequestsForDevice(
    IssueRequests<IssueFindRequest> *reqs, 
    const MsgFindRequest &request_base, int32_t device_id,
    int32_t sync_id, int32_t remote_route_port, 
    const string &remote_tree_uuid, int32_t remote_since) {
  const char* device_ip_projs[] = {
    TableDeviceIP::COLUMN_IP,
  };
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
          TableDeviceIP::URI, device_ip_projs, 
          ARRAY_SIZE(device_ip_projs), "%s = %d",
          TableDeviceIP::COLUMN_DEVICE_ID, device_id));

  while (device_ip_cursor->MoveToNext()) {
    IssueFindRequest *find_req = new IssueFindRequest(
        sync_id, device_ip_cursor->GetString(0), 
        remote_route_port, remote_tree_uuid.c_str());
    MsgFindRequest *msg_request = find_req->request.mutable_request();
    msg_request->CopyFrom(request_base);
    msg_request->set_remote_tree_uuid(remote_tree_uuid);
    msg_request->set_since(remote_since);

    reqs->IssueOneRequest(find_req);
  }
}

static inline IssueFindRequest* GetFirstResponse(
    IssueRequests<IssueFindRequest> *reqs, 
    const MsgFindRequest &request_base, const Sync &sync) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
  };
  int64_t remote_since;
  {
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %" PRId32 " AND %s = %d AND %s != %d", 
            TableTree::COLUMN_SYNC_ID, sync.id(),
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, //todo: should filter out root_remoted
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
    while (tree_cursor->MoveToNext()) {
      int32_t device_id = tree_cursor->GetInt32(1);
      const char *remote_tree_uuid = tree_cursor->GetString(0);
      int32_t remote_route_port;
      if (GetListableDevice(device_id, &remote_route_port)) {
        err_t zisync_ret = GetTreeMaxUsnFromContent(
            remote_tree_uuid, &remote_since);
        if (zisync_ret != ZISYNC_SUCCESS) {
          ZSLOG_ERROR("GetTreeMaxUsnFromContent(%s) fail : %s", 
                      remote_tree_uuid, zisync_strerror(zisync_ret));
          continue;
        }
        AddFindRequestsForDevice(
            reqs, request_base, device_id, sync.id(), remote_route_port, 
            remote_tree_uuid, remote_since);
      }
    }
  }

  IssueFindRequest *find_request;
  while (true) {
    if (zs::IsAborted()) {
      return NULL;
    }
    find_request = reqs->RecvNextResponsedRequest();
    if (find_request == NULL) {
      reqs->UpdateDeviceInDatabase();
      return NULL;
    } else {
      if (find_request->error() != ZISYNC_SUCCESS) {
        continue;
      }
    }
    break;
  }

  assert(find_request != NULL);
  return find_request;
}

#ifdef __APPLE__
static inline void UpdateRemoteMeta(
    const string local_tree_uuid, const Sync &sync, NSDictionary *userInfo, const string &list_path) {
#else
static inline void UpdateRemoteMeta(
    const string local_tree_uuid, const Sync &sync) {
#endif
  string remote_device_ip;
  int32_t remote_device_route_port;
  string remote_tree_uuid;
  int64_t remote_since;
  bool has_find = false;
  
#ifdef __APPLE__
  NSMutableArray *listFile = userInfo[kZSNotificationUserInfoData];
  assert(listFile);
#endif
  
  MsgFindRequest request_base;
  if (!local_tree_uuid.empty()) {
    request_base.set_local_tree_uuid(local_tree_uuid);
  }
  request_base.set_device_uuid(Config::device_uuid());
  request_base.set_limit(zs::FIND_LIMIT);
  request_base.set_sync_uuid(sync.uuid());
  request_base.set_is_list_sync(true);
  
  while (true) {
    IssueRequests<IssueFindRequest> issue_find_requests(
        WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
    IssueFindRequest *find_request;
    if (remote_device_ip.empty()) {
      find_request = GetFirstResponse(
          &issue_find_requests, request_base, sync);
    } else {
      IssueFindRequest *find_req = new IssueFindRequest(
          sync.id(), remote_device_ip.c_str(), 
          remote_device_route_port, remote_tree_uuid.c_str());
      MsgFindRequest *msg_request = find_req->request.mutable_request();
      msg_request->CopyFrom(request_base);
      msg_request->set_remote_tree_uuid(remote_tree_uuid);
      msg_request->set_sync_uuid(sync.uuid());
      msg_request->set_since(remote_since);
      issue_find_requests.IssueOneRequest(find_req);
      find_request = issue_find_requests.RecvNextResponsedRequest();
    }
    if (find_request == NULL) {
      std::string ip_list;
      ZSLOG_ERROR("Recv Reponse timeout: %s", 
                  issue_find_requests.GetRemoteIpString(&ip_list));
      issue_find_requests.UpdateDeviceInDatabase();
      break;
    } 
    if (find_request->error() != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Find fail : %s", zisync_strerror(find_request->error()));
      return;
    }
#ifdef __APPLE__
    const MsgRemoteMeta &remoteMeta =
        find_request->response.response().remote_meta();
    bool more_from_remote_stat = false;
    for (int i = 0; i < remoteMeta.stats_size(); i++) {
      const MsgStat &stat = remoteMeta.stats(i);
      if (stat.has_path()) {
        __block BOOL alreadyHave = NO;
        const char *pathInStat = stat.path().c_str();
        [listFile enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
          NSString *aPath = [(FileMetaObjc*)obj name];
          if (strcmp(aPath.UTF8String, pathInStat) == 0 ) {
            *stop = YES;
            alreadyHave = YES;
          }
        }];

        if (!alreadyHave) {
          more_from_remote_stat = true;
          [listFile addObject:
              [[FileMetaObjc alloc] initWithMsgStat:stat
              syncUuid:sync.uuid()
              superDirectory:list_path]];
        }
      }

    }

    if (more_from_remote_stat) {
      [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameUpdateListSync
          object:nil
          userInfo:userInfo];
    }

#endif
    err_t zisync_ret = StoreRemoteMeta(
        sync.id(), find_request->remote_tree_uuid.c_str(), 
        find_request->response.response().remote_meta());
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("StoreFindResponse fail : %s", 
                    zisync_strerror(zisync_ret));
      return;
    }
    has_find = true;
    if (remote_device_ip.empty()) {
      remote_device_ip = find_request->remote_ip();
      remote_device_route_port = find_request->remote_route_port();
      remote_tree_uuid = find_request->remote_tree_uuid;
    }
    const MsgRemoteMeta &remote_meta = 
        find_request->response.response().remote_meta();
    if (remote_meta.stats_size() == 0) {
      break;
    }
    remote_since = remote_meta.stats(remote_meta.stats_size() - 1).usn();
  }

  if (has_find) {
    IContentResolver *resolver = GetContentResolver();
    ContentValues cv(1);
    cv.Put(TableTree::COLUMN_LAST_FIND, zs::OsTimeInS());
    resolver->Update(TableTree::URI, &cv, "%s = '%s'", 
                     TableTree::COLUMN_UUID, remote_tree_uuid.c_str());
  }
}

static void inline ICursorToFileMeta(
    const string &sync_uuid, const string &path, ICursor2 *file_cursor, 
    FileMeta *file_meta) {
  const char *file_path = file_cursor->GetString(0);
  file_meta->name = file_path + path.size() + 1;
  file_meta->type = OsFileTypeToFileMetaType(
      static_cast<OsFileType>(file_cursor->GetInt32(1)));
  file_meta->length = file_meta->type == FILE_META_TYPE_REG ? 
      file_cursor->GetInt64(2) : 0;
  file_meta->mtime = file_cursor->GetInt64(3);
  file_meta->cache_path = zs::GenDownloadTmpPath(sync_uuid, file_path);
  file_meta->has_download_cache = IDownload::GetInstance()->
      HasDownloadCache(file_meta->cache_path, file_meta->mtime, 
                       file_meta->length);
}
  
#ifdef __APPLE__

static void LambdaListSync (void *ctx) {
  int32_t sync_id;
  string path_;
  void *userInfoC;
  NSDictionary *userInfo;
  std::tie(sync_id, path_, userInfoC) =
      *((std::tuple<int32_t, string, void*>*)ctx);
  userInfo = CFBridgingRelease(userInfoC);

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync || sync->IsUnusable() || 
      sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return;
  }

  @autoreleasepool {
    UpdateRemoteMeta(string(), *sync, userInfo, path_);
  }
  return;

  const char *sync_projs[] = {
    TableSync::COLUMN_UUID,
  };


  IContentResolver *resolver = zs::GetContentResolver();
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %" PRId32 " AND %s = %d", TableSync::COLUMN_ID, sync->id(),
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  if (!sync_cursor->MoveToNext()) {
    return;
  }

  string remote_tree_uuid;
  { 
    const char *tree_projs[] = {
      TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
    };

    string sort_order;
    StringFormat(&sort_order, "%s DESC", TableTree::COLUMN_LAST_FIND);

    Selection selection(
        "%s = %" PRId32 " AND %s = %d AND %s != %d AND %s != %" PRId64, 
        TableTree::COLUMN_SYNC_ID, sync_id,
        TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, 
        TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID, 
        TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
    unique_ptr<ICursor2> tree_cursor(resolver->sQuery(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            &selection, sort_order.c_str()));
    // @TODO JOIN
    while(tree_cursor->MoveToNext()) {
      int32_t device_id = tree_cursor->GetInt32(1);
      if (IsListableDevice(device_id)) {
        remote_tree_uuid = tree_cursor->GetString(0);
        break;
      }
    }
  }

  const char *file_projs[] = {
    TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
    TableFile::COLUMN_LENGTH, TableFile::COLUMN_MTIME,
  };

  string path;
  if (path_ != "/") {
    path = path_;
  }
  string fixed_path = zs::GenFixedStringForDatabase(path);

  if (remote_tree_uuid.size() != 0) {
    unique_ptr<ICursor2> file_cursor(resolver->Query(
            TableFile::GenUri(remote_tree_uuid.c_str()),
            file_projs, ARRAY_SIZE(file_projs), 
            "%s = %d AND %s LIKE '%s' || '/%%' "
            "AND %s NOT LIKE '%s' || '/%%/%%'",
            TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
            TableFile::COLUMN_PATH, fixed_path.c_str(), 
            TableFile::COLUMN_PATH, fixed_path.c_str()));
    while (file_cursor->MoveToNext()) {
      FileMeta file_meta;
      ICursorToFileMeta(sync->uuid(), path, file_cursor.get(), &file_meta);
    }
  }
}

err_t ZiSyncKernel::ListSync(int32_t sync_id, const std::string &path_, int64_t *list_sync_id_) {
  int64_t list_sync_id = zs::DefaultNotificationCenter()
      ->GetEventId(ZSNotificationNameUpdateListSync);
  *list_sync_id_ = list_sync_id;

  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ExportSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  //  if (!GetPermission()->Verify(USER_PERMISSION_CNT_BROWSE_REMOTE)) {
  //    ZSLOG_ERROR("You have no permission to use online scanf");
  //    return ZISYNC_ERROR_PERMISSION_DENY;
  //  }

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync || sync->IsUnusable() || 
      sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  NSMutableArray *listFiles = [NSMutableArray array];
  NSDictionary *userInfo = [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithInteger:list_sync_id]
      , kZSNotificationUserInfoEventId
      , listFiles, kZSNotificationUserInfoData
      , nil];

  IContentResolver *resolver = GetContentResolver();
  string local_tree_uuid;
  {
    const char *tree_projs[] = {
      TableTree::COLUMN_UUID,
    };
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d AND %s = %d",
            TableTree::COLUMN_SYNC_ID, sync_id, 
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
    if (tree_cursor->MoveToNext()) {
      local_tree_uuid = tree_cursor->GetString(0);
    }
  }

  //result->files.clear();
  string path;
  if (path_ != "/") {
    path = path_;
  }
  string fixed_path = zs::GenFixedStringForDatabase(path);

  const char *file_projs[] = {
    TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
    TableFile::COLUMN_LENGTH, TableFile::COLUMN_MTIME,
  };

  if (!local_tree_uuid.empty()) {
    unique_ptr<ICursor2> file_cursor(resolver->Query(
            TableFile::GenUri(local_tree_uuid.c_str()),
            file_projs, ARRAY_SIZE(file_projs), 
            "%s = %d AND %s LIKE '%s' || '/%%' "
            "AND %s NOT LIKE '%s' || '/%%/%%'",
            TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
            TableFile::COLUMN_PATH, fixed_path.c_str(), 
            TableFile::COLUMN_PATH, fixed_path.c_str()));
    while (file_cursor->MoveToNext()) {
      //todo: use objective c structure
      FileMeta file_meta;
      const char *file_path = file_cursor->GetString(0);
      file_meta.name = file_path + path.size() + 1;

      ICursorToFileMeta(sync->uuid(), path, file_cursor.get(), &file_meta);
      [listFiles addObject:[[FileMetaObjc alloc] initWithFileMeta:file_meta]];
    }
  }

  string remote_tree_uuid;
  { 
    const char *tree_projs[] = {
      TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
    };

    string sort_order;
    StringFormat(&sort_order, "%s DESC", TableTree::COLUMN_LAST_FIND);

    Selection selection(
        "%s = %" PRId32 " AND %s = %d AND %s != %d AND %s != %" PRId64, 
        TableTree::COLUMN_SYNC_ID, sync_id,
        TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, 
        TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID, 
        TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
    unique_ptr<ICursor2> tree_cursor(resolver->sQuery(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            &selection, sort_order.c_str()));
    // @TODO JOIN
    while(tree_cursor->MoveToNext()) {
      int32_t device_id = tree_cursor->GetInt32(1);
      if (IsListableDevice(device_id)) {
        remote_tree_uuid = tree_cursor->GetString(0);
        break;
      }
    }
  }

  if (remote_tree_uuid.size() != 0) {
    unique_ptr<ICursor2> file_cursor(resolver->Query(
            TableFile::GenUri(remote_tree_uuid.c_str()),
            file_projs, ARRAY_SIZE(file_projs), 
            "%s = %d AND %s LIKE '%s' || '/%%' "
            "AND %s NOT LIKE '%s' || '/%%/%%'",
            TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
            TableFile::COLUMN_PATH, fixed_path.c_str(), 
            TableFile::COLUMN_PATH, fixed_path.c_str()));
    while (file_cursor->MoveToNext()) {
      FileMeta file_meta;
      ICursorToFileMeta(sync->uuid(), path, file_cursor.get(), &file_meta);
      [listFiles addObject:[[FileMetaObjc alloc] initWithFileMeta:file_meta]];
    }
  }

  [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameUpdateListSync
      object:nil
      userInfo:userInfo];
  void *ctx = new std::tuple<int32_t, string, void*>(
      sync_id, path_
      , (void*)CFBridgingRetain(userInfo));
  GetEventBaseDb()->DispatchAsync(LambdaListSync, ctx, NULL);
  return ZISYNC_SUCCESS;
}

#else

err_t ZiSyncKernel::ListSync(
    int32_t sync_id, const std::string& path_, ListSyncResult *result) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ExportSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

//  if (!GetPermission()->Verify(USER_PERMISSION_CNT_BROWSE_REMOTE)) {
//    ZSLOG_ERROR("You have no permission to use online scanf");
//    return ZISYNC_ERROR_PERMISSION_DENY;
//  }

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync || sync->IsUnusable() || 
      sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  IContentResolver *resolver = GetContentResolver();
  string local_tree_uuid;
  {
    const char *tree_projs[] = {
      TableTree::COLUMN_UUID,
    };
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d AND %s = %d",
            TableTree::COLUMN_SYNC_ID, sync_id, 
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
    if (tree_cursor->MoveToNext()) {
      local_tree_uuid = tree_cursor->GetString(0);
    }
  }

  UpdateRemoteMeta(local_tree_uuid, *sync);

  result->files.clear();
  const char *sync_projs[] = {
    TableSync::COLUMN_UUID,
  };

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %" PRId32 " AND %s = %d", TableSync::COLUMN_ID, sync_id,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  if (!sync_cursor->MoveToNext()) {
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  string remote_tree_uuid;
  { 
    const char *tree_projs[] = {
      TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
    };

    string sort_order;
    StringFormat(&sort_order, "%s DESC", TableTree::COLUMN_LAST_FIND);

    Selection selection(
        "%s = %" PRId32 " AND %s = %d AND %s != %d AND %s != %" PRId64, 
        TableTree::COLUMN_SYNC_ID, sync_id,
        TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, 
        TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID, 
        TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
    unique_ptr<ICursor2> tree_cursor(resolver->sQuery(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            &selection, sort_order.c_str()));
    // @TODO JOIN
    while(tree_cursor->MoveToNext()) {
      int32_t device_id = tree_cursor->GetInt32(1);
      if (IsListableDevice(device_id)) {
        remote_tree_uuid = tree_cursor->GetString(0);
        break;
      }
    }
  }

  string path;
  if (path_ != "/") {
    path = path_;
  }
  const char *file_projs[] = {
    TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
    TableFile::COLUMN_LENGTH, TableFile::COLUMN_MTIME,
  };

  string fixed_path = zs::GenFixedStringForDatabase(path);
  if (remote_tree_uuid.size() != 0) {
    unique_ptr<ICursor2> file_cursor(resolver->Query(
            TableFile::GenUri(remote_tree_uuid.c_str()),
            file_projs, ARRAY_SIZE(file_projs), 
            "%s = %d AND %s LIKE '%s' || '/%%' "
            "AND %s NOT LIKE '%s' || '/%%/%%'",
            TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
            TableFile::COLUMN_PATH, fixed_path.c_str(), 
            TableFile::COLUMN_PATH, fixed_path.c_str()));
    while (file_cursor->MoveToNext()) {
      FileMeta file_meta;
      ICursorToFileMeta(sync->uuid(), path, file_cursor.get(), &file_meta);
      result->files.push_back(file_meta);
    }
  }

  if (!local_tree_uuid.empty()) {
    unique_ptr<ICursor2> file_cursor(resolver->Query(
            TableFile::GenUri(local_tree_uuid.c_str()),
            file_projs, ARRAY_SIZE(file_projs), 
            "%s = %d AND %s LIKE '%s' || '/%%' "
            "AND %s NOT LIKE '%s' || '/%%/%%'",
            TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
            TableFile::COLUMN_PATH, fixed_path.c_str(), 
            TableFile::COLUMN_PATH, fixed_path.c_str()));
    while (file_cursor->MoveToNext()) {
      FileMeta file_meta;
      const char *file_path = file_cursor->GetString(0);
      file_meta.name = file_path + path.size() + 1;
      if (std::find_if(
              result->files.begin(), result->files.end(),
              [ file_meta ] (const FileMeta &meta) 
              { return file_meta.name == meta.name; }) 
          == result->files.end()) {
        ICursorToFileMeta(sync->uuid(), path, file_cursor.get(), &file_meta);
        result->files.push_back(file_meta);
      }
    }
  }

  return ZISYNC_SUCCESS;
}

#endif
  
err_t ZiSyncKernel::ExportSync(int32_t sync_id, std::string* blob) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ExportSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  assert(false);
  // err_t zisync_ret = ZISYNC_SUCCESS;

  // SyncInfo sync_info;
  // zisync_ret = QueryOneSyncInfo(sync_id, &sync_info, false);
  // if (zisync_ret != ZISYNC_SUCCESS) {
  //   ZSLOG_ERROR("Query Sync(%d) fail : %s", sync_id,
  //               zisync_strerror(zisync_ret));
  //   return zisync_ret;
  // }

  // MsgSyncBlob sync_blob;
  // sync_blob.set_sync_name(sync_info.sync_name);
  // sync_blob.set_sync_uuid(sync_info.sync_uuid);
  // blob->assign(sync_blob.SerializeAsString());

  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::CreateTree(
     int32_t sync_id,
     const char* tree_root_,
    TreeInfo* tree_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call CreateTree before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  int32_t count = GetCurrentTreeCount();
  if (count == -1 || !GetPermission()->Verify(
          USER_PERMISSION_CNT_CREATE_TREE, &count)) {
    ZSLOG_ERROR("You have no permission to create tree");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  string tree_root = tree_root_;
  if (!IsAbsolutePath(tree_root) || !NormalizePath(&tree_root)) {
    ZSLOG_ERROR("Invalid path of tree_root(%s)", tree_root_);
    return ZISYNC_ERROR_INVALID_PATH;
  }

  SyncTree tree;
  err_t zisync_ret = tree.Create(sync_id, tree_root);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  
  // QueryCache::GetInstance()->NotifySyncModify();
  IssuePushTreeInfo(tree.id());
  // IssueRefresh(tree.id());
  // IssueSyncWithLocalTree(sync_id, tree.id());

  if (tree_info) {
    tree.ToTreeInfo(tree_info);
  }

  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::DestroyTree(int32_t tree_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DestroyTree before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  err_t zisync_ret = Tree::DeleteById(tree_id);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Tree::DeleteById(%d) fail : %s", 
                tree_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  // QueryCache::GetInstance()->NotifySyncModify();
  IssuePushTreeInfo(tree_id);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::QueryTreeInfo(int32_t tree_id, TreeInfo  *tree_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryTreeInfo before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  unique_ptr<Tree> tree(Tree::GetByIdWhereStatusNormal(tree_id));
  if (!tree) {
    return ZISYNC_ERROR_TREE_NOENT;
  }
  tree->ToTreeInfo(tree_info);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetTreeRoot(int32_t tree_id, const char *tree_root) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetTreeRoot before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  
  string tree_root_ = tree_root;
  if (!IsAbsolutePath(tree_root_) || !NormalizePath(&tree_root_)) {
    ZSLOG_ERROR("Invalid path of app_data(%s)", tree_root);
    return ZISYNC_ERROR_INVALID_PATH;
  }

  unique_ptr<Tree> tree(Tree::GetByIdWhereStatusNormal(tree_id));

  if (tree_root_ == tree->root()) {
    return ZISYNC_SUCCESS;
  }
  
  err_t zisync_ret = tree->SetRoot(tree_root_);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("tree->SetRoot(%s) fail : %s", tree_root_.c_str(),
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  IContentResolver *resolver = GetContentResolver();
  resolver->Delete(TableFile::GenUri(tree->uuid().c_str()), NULL);
  IssueRefresh(tree_id);
  IssuePushTreeInfo(tree_id);

  // if (tree->type() == TableTree::BACKUP_NONE) {
  //   QueryCache::GetInstance()->NotifySyncModify();
  // } else {
  //   QueryCache::GetInstance()->NotifyBackupModify();
  // }
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::AddFavorite(
    int32_t tree_id, const char* favorite_)  {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call AddFavorite before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  string favorite = favorite_;
  if (!IsRelativePath(favorite) || !NormalizePath(&favorite)) {
    ZSLOG_ERROR("Invalid path of favorite(%s)", favorite_);
    return ZISYNC_ERROR_INVALID_PATH;
  }
  err_t zisync_ret = SyncList::Insert(tree_id, favorite.c_str());
  if (zisync_ret == ZISYNC_ERROR_SYNC_LIST_EXIST) {
    return ZISYNC_ERROR_FAVOURITE_EXIST;
  } else if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  /*  Issue a sync */
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = push.Connect(zs::router_sync_fronter_uri);
  int32_t sync_id = Tree::GetSyncIdByIdWhereStatusNormal(tree_id);
  if (sync_id != -1) {
    IssueSyncWithLocalTree(sync_id, tree_id);
  }
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::DelFavorite(
    int32_t tree_id, const char* favorite_)  {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DelFavorite before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  string favorite = favorite_;
  if (!IsRelativePath(favorite) || !NormalizePath(&favorite)) {
    ZSLOG_ERROR("Invalid path of favorite(%s)", favorite_);
    return ZISYNC_ERROR_INVALID_PATH;
  }
  err_t zisync_ret = SyncList::Remove(tree_id, favorite.c_str());
  if ( zisync_ret == ZISYNC_ERROR_SYNC_LIST_NOENT) {
    return ZISYNC_ERROR_FAVOURITE_NOENT;
  } else if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  IContentResolver *resolver = GetContentResolver();
  unique_ptr<Tree> tree(Tree::GetByIdWhereStatusNormal(tree_id));
  if (tree) {
    string path = tree->root();
    path += favorite;
    string fixed_favorite = GenFixedStringForDatabase(favorite);
    if (favorite == "/") {
      zs::OsDeleteDirectories(path, false);
      resolver->Delete(
          TableFile::GenUri(tree->uuid().c_str()), 
          "%s = '%s' OR %s LIKE '%s' || '%%'",
          TableFile::COLUMN_PATH, fixed_favorite.c_str(), 
          TableFile::COLUMN_PATH, fixed_favorite.c_str());
    } else {
      if (zs::OsDirExists(path)) {
        zs::OsDeleteDirectories(path);
      }
      if (zs::OsFileExists(path)) {
        zs::OsDeleteFile(path, false);
      }

      resolver->Delete(
          TableFile::GenUri(tree->uuid().c_str()), 
          "%s = '%s' OR %s LIKE '%s' || '/%%'",
          TableFile::COLUMN_PATH, fixed_favorite.c_str(), 
          TableFile::COLUMN_PATH, fixed_favorite.c_str());
    }
  }
  return ZISYNC_SUCCESS;
}

int ZiSyncKernel::GetFavoriteStatus(int32_t tree_id, const char *path_) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call IsFavorite before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  string path = path_;
  if (!IsRelativePath(path) || !NormalizePath(&path)) {
    ZSLOG_ERROR("Invalid path of path(%s)", path_);
    return FAVORITE_NOT;
  }
  SyncListPathType type = SyncList::GetSyncListPathType(tree_id, path.c_str());
  switch (type) {
    case SYNC_LIST_PATH_TYPE_STRANGER:
    case SYNC_LIST_PATH_TYPE_PARENT:
      return FAVORITE_NOT;
    case SYNC_LIST_PATH_TYPE_CHILD:
      return FAVORITE_UNCANCELABLE;
    case SYNC_LIST_PATH_TYPE_SELF:
      return FAVORITE_CANCELABLE;
    default:
      assert(false);
      return FAVORITE_NOT;
  }
}

bool ZiSyncKernel::HasFavorite(int32_t tree_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call HasFavorite before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  vector<string> favorites;
  err_t zisync_ret = SyncList::List(tree_id, &favorites);
  return zisync_ret == ZISYNC_SUCCESS && !favorites.empty();
}

err_t ZiSyncKernel::SyncOnce(int32_t sync_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SyncOnce before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return SyncOnceIntern(sync_id, true);
}

err_t ZiSyncKernel::QueryTreeStatus(int32_t tree_id,
                                    TreeStatus* tree_status) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryTreeStatus before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (tree_id != -1 && !Tree::ExistsWhereStatusNormalDeviceLocal(tree_id)) {
    ZSLOG_ERROR("Tree(%d) does not exist", tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  return GetTreeManager()->QueryTreeStatus(tree_id, tree_status);
}

err_t ZiSyncKernel::QueryTreePairStatus(
    int32_t local_tree_id, int32_t remote_tree_id, 
    TreePairStatus* tree_status) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryTreePairStatus before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (!Tree::ExistsWhereStatusNormalDeviceLocal(local_tree_id)) {
    ZSLOG_ERROR("Tree(%d) does not exist", local_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }
  if (!Tree::ExistsWhereStatusNormal(remote_tree_id)) {
    ZSLOG_ERROR("Tree(%d) does not exist", remote_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  return GetTreeManager()->QueryTreePairStatus(
      local_tree_id, remote_tree_id, tree_status);
}

err_t ZiSyncKernel::QueryTransferList(
    int32_t tree_id, TransferListStatus *transfer_list,
    int32_t offset, int32_t max_num) {
  assert(transfer_list != NULL);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call query transfer list before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (!GetPermission()->Verify(USER_PERMISSION_FUNC_TRANSFER_LIST)) {
    ZSLOG_ERROR("You have no permission to use transfer list");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  return GetTreeManager()->QueryTransferList(
      tree_id, transfer_list, offset, max_num);
}

err_t ZiSyncKernel::StartupDiscoverDevice(
    int32_t sync_id, int32_t *discover_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call StartupDiscoverDevice before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (sync_id != -1) {
	  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormalTypeNotBackup(sync_id));
	  if (!sync) {
		  ZSLOG_ERROR("Nonent Sync(%d)", sync_id);
		  return ZISYNC_ERROR_SYNC_NOENT;
	  }
  } else if (!GetPermission()->Verify(USER_PERMISSION_FUNC_DEVICE_SWITCH)) {
    ZSLOG_ERROR("You have no permission to show nearby devices.");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  return IDiscoverDeviceHandler::GetInstance()->StartupDiscover(
      sync_id, discover_id);
}

err_t ZiSyncKernel::GetDiscoveredDevice(
    int32_t discover_id, DiscoverDeviceResult *result) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetDiscoveredDevice before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  result->devices.clear();
  return IDiscoverDeviceHandler::GetInstance()->GetDiscoveredDevice(
      discover_id, result);
}

err_t ZiSyncKernel::ShutdownDiscoverDevice(int32_t discover_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ShutdownDiscoverDevice before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IDiscoverDeviceHandler::GetInstance()->ShutDownDiscover(discover_id);
}

err_t ZiSyncKernel::ShareSync(
    int32_t discover_id, int32_t device_id, int32_t sync_perm) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ShareSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  int count = GetCurrentShareCount();
  if (!GetPermission()->Verify(USER_PERMISSION_CNT_CREATE_SHARE, &count)) {
    ZSLOG_ERROR("You have no permission to create share");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  switch (sync_perm) {
    case SYNC_PERM_RDONLY:
      if (!GetPermission()->Verify(USER_PERMISSION_FUNC_SHARE_READ)) {
        ZSLOG_ERROR("You have no permission to create readonly share");
        return ZISYNC_ERROR_PERMISSION_DENY;
      }
      break;
    case SYNC_PERM_WRONLY:
      if (!GetPermission()->Verify(USER_PERMISSION_FUNC_SHARE_WRITE)) {
        ZSLOG_ERROR("You have no permission to create writeonly share");
        return ZISYNC_ERROR_PERMISSION_DENY;
      }
      break;
    case SYNC_PERM_RDWR:
      if (!GetPermission()->Verify(USER_PERMISSION_FUNC_SHARE_READWRITE)) {
        ZSLOG_ERROR("You have no permission to create WR share");
        return ZISYNC_ERROR_PERMISSION_DENY;
      }
      break;
    default:
      assert(0);
  }
  DiscoveredDevice discovered_device;
  err_t zisync_ret = 
      IDiscoverDeviceHandler::GetInstance()->GetDiscoveredDevice(
      discover_id, device_id, &discovered_device);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetDiscoveredDevice(discover_id = %d, device_id = %d)"
                " fail : %s", discover_id, device_id, 
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  assert(!discovered_device.is_mine);

  int32_t sync_id;
  zisync_ret = IDiscoverDeviceHandler::GetInstance()->GetSyncId(
      discover_id, &sync_id);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetSyncId(discover_id = %d) fail : %s", 
                discover_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync || sync->type() == TableSync::TYPE_BACKUP) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  if (sync->device_id() != TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("ShareSync with is not created by local is not allowed");
    return ZISYNC_ERROR_NOT_SYNC_CREATOR;
  }

  if (sync->type() == TableSync::TYPE_NORMAL) {
    IContentResolver *resolver = GetContentResolver();
    ContentValues sync_cv(1);
    sync_cv.Put(TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED);
    int affected_row_num = resolver->Update(
        TableSync::URI, &sync_cv, "%s = %d",
        TableSync::COLUMN_ID, sync_id);
    if (affected_row_num != 1) {
      ZSLOG_ERROR("Update Sync(%d) from NORMAL to SHARED faile", sync_id);
      return ZISYNC_ERROR_CONTENT;
    }
  }
  
  ShareSyncRequest request;
  MsgDevice *device = request.mutable_request()->mutable_device();
  SetDeviceMetaInMsgDevice(device);
  MsgSync *msg_sync = device->add_syncs();
  sync->ToMsgSync(msg_sync);
  // since sync is get before update type to SHARED, so we need to set
  // msg_sync.type() manually
  msg_sync->set_type(ST_SHARED);
  msg_sync->set_perm(SyncPermToMsgSyncPerm(sync_perm));
  SetTreesInMsgSync(sync_id, msg_sync);

  ZmqContext context;
  ZmqSocket req(context, ZMQ_REQ);
  string uri;
  StringFormat(&uri, "tcp://%s:%d", discovered_device.ip.c_str(), 
               discovered_device.route_port);
  err_t no_ret = req.Connect(uri.c_str());
  assert(no_ret == ZISYNC_SUCCESS);
  no_ret = request.SendTo(req);
  assert(no_ret == ZISYNC_SUCCESS);

  ShareSyncResponse response;
  string src_uuid;
  zisync_ret = response.RecvFrom(
      req, WAIT_RESPONSE_TIMEOUT_IN_S * 1000, &src_uuid);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("ShareSync(discover_id = %d, device_id = %d) "
                "fail : %s", discover_id, device_id, 
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  const MsgDevice &remote_device = response.response().device();
  int32_t remote_device_id = StoreDeviceIntoDatabase(
      remote_device, discovered_device.ip.c_str(), 
      discovered_device.is_mine, discovered_device.is_ipv6, false);
  if (remote_device_id == -1) {
    ZSLOG_WARNING("StoreDeviceIntoDatabase(%s) fail", 
                discovered_device.ip.c_str());
    return ZISYNC_SUCCESS;
  }
  
  zisync_ret = zs::SetShareSyncPerm(remote_device_id, sync_id, sync_perm);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SetSyncPerm fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  // no need to store sync and sync, since the remote only has sync, no tree
  // IssuePushSyncInfo(sync_id);
  // QueryCache::GetInstance()->NotifySyncModify();
  
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::CancelShareSync(
    int32_t device_id, int32_t sync_id) {
  return this->SetShareSyncPerm(device_id, sync_id, TableSync::PERM_DISCONNECT);
}

err_t ZiSyncKernel::DisconnectShareSync(int32_t sync_id) {
  RwLockRdAuto rd_auto(&rwlock);
  assert(false);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetShareSyncPerm before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  unique_ptr<Sync> sync(Sync::GetBy(
          "%s = %d AND %s = %d AND %s = %d",
          TableSync::COLUMN_ID, sync_id, 
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
          TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED));
  if (!sync) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  if (Device::IsMyDevice(sync->device_id())) {
    ZSLOG_ERROR("Can not call DisconnectShareSync with a Sync who's device has"
                " same token with creator");
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  if (sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("You can not call DisconnectShareSync in Creator");
    return ZISYNC_ERROR_SYNC_CREATOR;
  }

  if (sync->perm() == TableSync::PERM_DISCONNECT) {
    return ZISYNC_SUCCESS;
  }

  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableSync::COLUMN_PERM, TableSync::PERM_DISCONNECT);
  int affected_row_num = resolver->Update(
      TableSync::URI, &cv, "%s = %d", TableSync::COLUMN_ID, sync_id);
  if (affected_row_num != 1) {
    ZSLOG_ERROR("Update Sync(%d) to TableSync::PERM_DISCONNECT fail", sync_id);
    return ZISYNC_ERROR_CONTENT;
  }

  Tree::DeleteBy("%s = %d AND %s = %d", TableTree::COLUMN_SYNC_ID, sync_id, 
                 TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  
  IssuePushSyncInfo(sync_id, sync->device_id());
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetShareSyncPerm(
    int32_t device_id, int32_t sync_id, int32_t sync_perm) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetShareSyncPerm before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (!GetPermission()->Verify(USER_PERMISSION_FUNC_EDIT_SHARE_PERMISSION)) {
    ZSLOG_ERROR("You have no permission to change permission");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  switch (sync_perm) {
    case SYNC_PERM_RDONLY:
      if (!GetPermission()->Verify(USER_PERMISSION_FUNC_SHARE_READ)) {
        ZSLOG_ERROR("You have no permission to create readonly share");
        return ZISYNC_ERROR_PERMISSION_DENY;
      }
      break;
    case SYNC_PERM_WRONLY:
      if (!GetPermission()->Verify(USER_PERMISSION_FUNC_SHARE_WRITE)) {
        ZSLOG_ERROR("You have no permission to create writeonly share");
        return ZISYNC_ERROR_PERMISSION_DENY;
      }
      break;
    case SYNC_PERM_RDWR:
      if (!GetPermission()->Verify(USER_PERMISSION_FUNC_SHARE_READWRITE)) {
        ZSLOG_ERROR("You have no permission to create WR share");
        return ZISYNC_ERROR_PERMISSION_DENY;
      }
      break;
    default:
      break;
  }

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  if (sync->device_id() != TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("SetShareSyncPermSync with is not created by local is not allowed");
    return ZISYNC_ERROR_NOT_SYNC_CREATOR;
  }

  if (device_id == TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("Can not SetShareSyncPerm with device_id == LOCAL_DEVICE_ID");
    return ZISYNC_ERROR_DEVICE_NOENT;
  }

  unique_ptr<Device> device(Device::GetById(device_id));
  if (!device || device->is_mine()) {
    ZSLOG_ERROR("Noent Device(%d)", device_id);
    return ZISYNC_ERROR_DEVICE_NOENT;
  }
  
  int32_t pre_sync_perm;
  err_t zisync_ret = zs::GetShareSyncPerm(device_id, sync_id, &pre_sync_perm);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetSyncPerm(%d, %d) fail : %s",
                device_id, sync_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  if (pre_sync_perm == sync_perm) {
    return ZISYNC_SUCCESS;
  }
  assert(pre_sync_perm != TableSync::PERM_TOKEN_DIFF);
  if (pre_sync_perm == TableSync::PERM_DISCONNECT) {
    ZSLOG_ERROR("You can SetShareSyncPerm for a discconet Sync");
    return ZISYNC_ERROR_SHARE_SYNC_DISCONNECT;
  }

  zisync_ret = zs::SetShareSyncPerm(device_id, sync_id, sync_perm);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SetShareSyncPerm(%d, %d) fail : %s",
                device_id, sync_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  if (sync_perm == TableSync::PERM_DISCONNECT) {
    Tree::DeleteBy("%s = %d AND %s = %d", TableTree::COLUMN_SYNC_ID, sync_id, 
                   TableTree::COLUMN_DEVICE_ID, device_id);
  }

  IssuePushSyncInfo(sync_id, device_id);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::GetShareSyncPerm(
    int32_t device_id, int32_t sync_id, int32_t *sync_perm) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetShareSyncPerm before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return zs::GetShareSyncPerm(device_id, sync_id, sync_perm);
}

err_t ZiSyncKernel::StartupDownload(
    int32_t sync_id, const string& relative_path_, string* target_path, 
    int32_t *task_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call StartupDownload before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (!GetPermission()->Verify(USER_PERMISSION_FUNC_REMOTE_DOWNLOAD)) {
    ZSLOG_ERROR("You have no permission to download files");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  string relative_path = relative_path_;
  if (!IsRelativePath(relative_path) || !NormalizePath(&relative_path)) {
    ZSLOG_ERROR("Invalid relative_path of relative_path(%s)", 
                relative_path_.c_str());
    return ZISYNC_ERROR_INVALID_PATH;
  }
  return IDownload::GetInstance()->Startup(
      sync_id, relative_path, target_path, task_id);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::ShutdownDownload(int32_t task_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ShutdownDownload before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IDownload::GetInstance()->Shutdown(task_id);
}

err_t ZiSyncKernel::QueryDownloadStatus(int32_t task_id, DownloadStatus *status) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryDownloadStatus before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IDownload::GetInstance()->QueryStatus(task_id, status);
}

err_t ZiSyncKernel::StartupUpload(
    int32_t sync_id, const string& relative_path_, const string &target_path, 
    int32_t *task_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call StartupUpload before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (!GetPermission()->Verify(USER_PERMISSION_FUNC_REMOTE_UPLOAD)) {
    ZSLOG_ERROR("You have no permission to upload files");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
  string relative_path = relative_path_;
  if (!IsRelativePath(relative_path) || !NormalizePath(&relative_path)) {
    ZSLOG_ERROR("Invalid relative_path of relative_path(%s)", 
                relative_path_.c_str());
    return ZISYNC_ERROR_INVALID_PATH;
  }
  return IUpload::GetInstance()->Startup(
      sync_id, relative_path, target_path, task_id);
}

err_t ZiSyncKernel::ShutdownUpload(int32_t task_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ShutdownUpload before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IUpload::GetInstance()->Shutdown(task_id);
}

err_t ZiSyncKernel::QueryUploadStatus(int32_t task_id, UploadStatus *status) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryUploadStatus before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  return IUpload::GetInstance()->QueryStatus(task_id, status);
}

static IssueRemoveRemoteFileRequest * SendRequestsGetFirstResponse(
    IssueRequests<IssueRemoveRemoteFileRequest> &reqs
    , const MsgRemoveRemoteFileRequest &req, int32_t sync_id) {

  std::vector<unique_ptr<Tree> > trees;
  Tree::QueryBySyncIdWhereStatusNormal(sync_id, &trees);

  const char* device_ip_projs[] = {
    TableDeviceIP::COLUMN_IP,
  };

  for (auto it = trees.begin(); it != trees.end(); ++it) {
    if ((*it)->device_id() == TableDevice::LOCAL_DEVICE_ID) continue;

    IContentResolver *resolver = GetContentResolver();
    unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
          TableDeviceIP::URI, device_ip_projs, 
          ARRAY_SIZE(device_ip_projs), "%s = %d",
          TableDeviceIP::COLUMN_DEVICE_ID, (*it)->device_id()));

    int32_t remote_route_port;
    if (!GetListableDevice((*it)->device_id(), &remote_route_port)) {
      continue;
    }

    while (device_ip_cursor->MoveToNext()) {
      IssueRemoveRemoteFileRequest *rm_req = new IssueRemoveRemoteFileRequest(
          device_ip_cursor->GetString(0), remote_route_port);

      MsgRemoveRemoteFileRequest *msg_request 
        = rm_req->request.mutable_request();
      msg_request->set_sync_uuid(req.sync_uuid());
      msg_request->set_relative_path(req.relative_path());

      reqs.IssueOneRequest(rm_req);
    }
  }

  return reqs.RecvNextResponsedRequest();
}

err_t ZiSyncKernel::RemoveRemoteFile(
    int32_t sync_id, const string &relative_path) {
  
  std::unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  MsgRemoveRemoteFileRequest request_base;
  request_base.set_sync_uuid(sync->uuid());
  request_base.set_relative_path(relative_path);

  IssueRequests<IssueRemoveRemoteFileRequest> issue_rm_requests(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  IssueRemoveRemoteFileRequest *rm_request = SendRequestsGetFirstResponse(
      issue_rm_requests, request_base, sync_id);

  MsgRemoveRemoteFileError error = E_NONE;
  while (rm_request) {
    const MsgRemoveRemoteFileResponse &msg_rm_resp 
      = ((RemoveRemoteFileResponse*)rm_request->mutable_response())->response();
    MsgRemoveRemoteFileError err = msg_rm_resp.error();

    if (err == E_NONE) {
      ZSLOG_INFO("Remove Remote File Done.");
      return ZISYNC_SUCCESS;
    }else if (error != E_RM_FAIL) {
      error = err;
    }

    //    err_t zisync_ret = StoreRemoteMeta(
    //        sync.id(), find_request->remote_tree_uuid.c_str(), 
    //        find_request->response.response().remote_meta());
    //    if (zisync_ret != ZISYNC_SUCCESS) {
    //      ZSLOG_ERROR("StoreFindResponse fail : %s", 
    //                    zisync_strerror(zisync_ret));
    //      return;
    //    }
    //    has_find = true;
    //    if (remote_device_ip.empty()) {
    //      remote_device_ip = find_request->remote_ip();
    //      remote_device_route_port = find_request->remote_route_port();
    //      remote_tree_uuid = find_request->remote_tree_uuid;
    //    }
    //    const MsgRemoteMeta &remote_meta = 
    //        find_request->response.response().remote_meta();
    //    if (remote_meta.stats_size() == 0) {
    //      break;
    //    }
    rm_request = issue_rm_requests.RecvNextResponsedRequest();
  }

  if (error == E_NONE) {
    ZSLOG_ERROR("Failed Removing Remote File: No Device/Tree Responded");
  }else if (error == E_NOENT) {
    ZSLOG_ERROR("Failed Removing Remote File: Remote Device/Tree Returned ENOENT");
  }else {
    ZSLOG_ERROR("Failed Removing Remote File: Remote Device/Tree Failed To Rm");
  }
  return ZISYNC_ERROR_GENERAL;
}

err_t ZiSyncKernel::SetBackground(int32_t interval) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetBackground before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  if (interval <= 0) {
    ZSLOG_WARNING("interval = 0, will not enter background mode");
    return ZISYNC_SUCCESS;

  }
  if (is_background == true) {
    ZSLOG_ERROR("already in background mode");
    return ZISYNC_SUCCESS;
  }
  ZSLOG_INFO("Start SetBackground");
  is_background = true;
  IssueRouteShutdown();
  IssueDiscoverSetBackground();
  device_info_timer.CleanUp();
  if (sync_timer) {
    sync_timer->CleanUp();
  }
  device_ip_check_timer.CleanUp();
  background_timer.reset(new OsTimer(interval, &background_on_timer));
  background_timer->Initialize();
  
  ZSLOG_INFO("END SetBackground Interval:%d", interval);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetForeground() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetBackground before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  if (is_background == false) {
    ZSLOG_ERROR("not in background mode");
    return ZISYNC_SUCCESS;
  }

  ZSLOG_INFO("Start SetForeground");
  is_background = false;
  background_timer->CleanUp();
  IssueRouteStartup();
  IssueDiscoverSetForeground();
  device_info_on_timer.OnTimer();
  device_info_timer.Initialize();
  if (sync_timer) {
    sync_on_timer.OnTimer();
    sync_timer->Initialize();
  }
  device_ip_check_timer.Initialize();

  ZSLOG_INFO("End SetForeground");
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::QueryBackupInfo(QueryBackupInfoResult* result) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryBackupInfo before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return QueryCache::GetInstance()->QueryBackupInfo(result);
}

err_t ZiSyncKernel::QueryBackupInfo(int32_t sync_id, BackupInfo *backup_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryBackupInfo before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return QueryCache::GetInstance()->QueryBackupInfo(sync_id, backup_info);
}

err_t ZiSyncKernel::CreateBackup(
    const char* backup_name, const char *root_, 
    BackupInfo* backup_info) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call CreateBackup before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  string root = root_;
  if (!IsAbsolutePath(root) || !NormalizePath(&root)) {
    ZSLOG_ERROR("Invalid root of root(%s)", root_);
    return ZISYNC_ERROR_INVALID_PATH;
  }

  unique_ptr<Tree> tree(Tree::GetBy(
          "%s = '%s' AND %s = %d AND %s = %d",
          TableTree::COLUMN_ROOT, 
          GenFixedStringForDatabase(root).c_str(),
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));//todo: should also list root_moved tree
  if (tree) {
    if (tree->type() == TableTree::BACKUP_NONE) {
      return ZISYNC_ERROR_TREE_EXIST;
    } else {
      unique_ptr<Backup> sync(Backup::GetByIdWhereStatusNormal(
              tree->sync_id()));
      if (!sync) {
        ZSLOG_ERROR("Should Not Happen : Noent Sync(%d)",
                    tree->sync_id());
        return ZISYNC_ERROR_GENERAL;
      }
      sync->ToBackupInfo(backup_info);
      if (tree->type() == TableTree::BACKUP_DST) {
        return ZISYNC_ERROR_BACKUP_DST_EXIST;
      } else {
        return ZISYNC_ERROR_BACKUP_SRC_EXIST;
      }
    }
  }

  int32_t count = GetCurrentBackupCount();
  if (count == -1 || !GetPermission()->Verify(
          USER_PERMISSION_CNT_BACKUP, &count)) {
    ZSLOG_ERROR("You have no permission to create backup.");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  Backup backup;
  err_t zisync_ret = backup.Create(backup_name, root);;
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Create backup fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  backup.ToBackupInfo(backup_info);
  // QueryCache::GetInstance()->NotifyBackupModify();
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::DestroyBackup(int32_t sync_id) { 
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DestroyBackup before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  err_t zisync_ret = Backup::DeleteById(sync_id);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Backup::DeleteById(%d) fail : %s", 
                sync_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  IssuePushSyncInfo(sync_id);
  // QueryCache::GetInstance()->NotifyBackupModify();
  return ZISYNC_SUCCESS;
}


static inline void SetLocalDeviceBackupRoot(string *backup_root) {
  string backup_root_;
  ContentValues cv(1);
  err_t zisync_ret = GenDeviceRootForBackup(
      Config::device_name().c_str(), &backup_root_);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("CreateDeviceRootForBackup for Local Device fail: %s",
                zisync_strerror(zisync_ret));
    return;
  }
  cv.Put(TableDevice::COLUMN_BACKUP_DST_ROOT, backup_root_);
  int32_t row_id = GetContentResolver()->Update(
      TableDevice::URI, &cv, "%s = %d", 
      TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  if (row_id < 0) {
    ZSLOG_WARNING("Update Local Device backup_root fail.");
  } else {
    *backup_root = backup_root_;
  }
}

err_t ZiSyncKernel::ListBackupTargetDevice(
      int32_t backup_id, ListBackupTargetDeviceResult *result) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call ListBackupTargetDevice "
                "before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  result->devices.clear();
  ZSLOG_INFO("ListBackupTargetDevice(%d)", backup_id);

  IContentResolver *resolver = GetContentResolver();

  const char *device_projs[] = {
    TableDevice::COLUMN_ID, TableDevice::COLUMN_NAME, 
    TableDevice::COLUMN_TYPE, TableDevice::COLUMN_STATUS,
    TableDevice::COLUMN_BACKUP_DST_ROOT,
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
          "%s = %d", 
          TableDevice::COLUMN_IS_MINE, true));
  while (device_cursor->MoveToNext()) {
    Platform device_platform = static_cast<Platform>(
        device_cursor->GetInt32(2));
    if (IsMobileDevice(device_platform)) {
      continue;
    }
    int32_t device_id = device_cursor->GetInt32(0);
    DeviceInfo device_info;
    {
      const char *tree_projs[] = {
        TableTree::COLUMN_ID,
      };
      unique_ptr<ICursor2> tree_cursor(resolver->Query(
              TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
              "%s = %d AND %s = %d AND %s = %d AND %s = %d", 
              TableTree::COLUMN_DEVICE_ID, device_id,
              TableTree::COLUMN_SYNC_ID, backup_id,
              TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_DST,
              TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
      device_info.is_backup = tree_cursor->MoveToNext();
    }
    device_info.is_online = 
        device_cursor->GetInt32(3) == TableDevice::STATUS_ONLINE;
    device_info.device_id = device_id;
    device_info.device_name = device_cursor->GetString(1);
    device_info.device_type = MsgDeviceTypeToDeviceType(
        PlatformToMsgDeviceType(device_platform));
    device_info.is_mine = true;
    const char *backup_target_root = device_cursor->GetString(4);
    if (backup_target_root != NULL) {
      device_info.backup_root = backup_target_root;
    } else {
      if (device_id == TableDevice::LOCAL_DEVICE_ID &&
          !IsMobileDevice()) {
        SetLocalDeviceBackupRoot(&device_info.backup_root);
      }
    }
    if (device_info.is_backup || device_info.is_online) {
      result->devices.push_back(device_info);
    }
  }

  return ZISYNC_SUCCESS;
}

static inline err_t SetBackupSrcTreeInMsgSync(int32_t sync_id, MsgSync *sync) {
  IContentResolver *resolver = GetContentResolver();
  const char* tree_projs[] = {
    TableTree::COLUMN_ROOT, TableTree::COLUMN_UUID,
    TableTree::COLUMN_STATUS,
  };

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_SRC));
  while (tree_cursor->MoveToNext()) {
    MsgTree *tree = sync->add_trees();
    assert(tree_cursor->GetInt32(2) != TableTree::STATUS_VCLOCK);
    tree->set_root(tree_cursor->GetString(0));
    tree->set_uuid(tree_cursor->GetString(1));
    tree->set_is_normal(tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL);
  }

  return ZISYNC_SUCCESS;
}

static inline err_t AddBackupTargetRemoteDevice(
    int32_t backup_id, int32_t device_id, 
    TreeInfo *remote_backup_tree, const char *backup_root) {
  int32_t device_route_port;
  string remote_device_uuid;
  IContentResolver *resolver = GetContentResolver();
  {
    const char *device_projs[] = {
      TableDevice::COLUMN_UUID, TableDevice::COLUMN_ROUTE_PORT,
    };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
            "%s = %d", TableDevice::COLUMN_ID, device_id));
    if (!device_cursor->MoveToNext()) {
      return ZISYNC_ERROR_DEVICE_NOENT;
    }
    device_route_port = device_cursor->GetInt32(1);
    remote_device_uuid = device_cursor->GetString(0);
  }

  PushBackupInfoRequest request;
  MsgDevice *device = request.mutable_request()->mutable_device();
  SetDeviceMetaInMsgDevice(device);
  MsgSync *sync = device->add_syncs();
  unique_ptr<Backup> backup(
      Backup::GetByIdWhereStatusNormal(backup_id));
  if (!backup) {
    ZSLOG_ERROR("Noent Backup(%d)", backup_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  backup->ToMsgSync(sync);
  err_t zisync_ret = SetBackupSrcTreeInMsgSync(backup_id, sync);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SetBackupSrcTreesInMsgSync(%d) fail : %s", 
                  backup_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  if (sync->trees_size() != 1) {
    ZSLOG_ERROR("sync->trees_size() should be 1 but is %d",
                sync->trees_size());
    return ZISYNC_ERROR_TREE_NOENT;
  }
  if (backup_root != NULL) {
    request.mutable_request()->set_root(backup_root);
  }

  class IssuePushBackupInfoReq : public IssueRequest {
   public:
    IssuePushBackupInfoReq(
        int32_t remote_device_id, const string remote_device_uuid, 
        const char *remote_ip, int32_t remote_route_port, 
        PushBackupInfoRequest *request_):
        IssueRequest(remote_device_id, remote_device_uuid, 
                     remote_ip, remote_route_port, true), 
        request(request_) {}
    virtual Request* mutable_request() { return request; }
    virtual Response* mutable_response() { return &response; }

    PushBackupInfoRequest *request;
    PushBackupInfoResponse response;
   private:
    IssuePushBackupInfoReq(IssuePushBackupInfoReq&);
    void operator=(IssuePushBackupInfoReq&);
  };

  IssueRequests<IssuePushBackupInfoReq> reqs(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  {
    const char *device_ip_projs[] = {
      TableDeviceIP::COLUMN_IP,
    };
    unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
            TableDeviceIP::URI, device_ip_projs, 
            ARRAY_SIZE(device_ip_projs), "%s = %d",
            TableDeviceIP::COLUMN_DEVICE_ID, device_id));

    while (device_ip_cursor->MoveToNext()) {
      IssuePushBackupInfoReq *req = new IssuePushBackupInfoReq(
          device_id, remote_device_uuid, device_ip_cursor->GetString(0), 
          device_route_port, &request);
      reqs.IssueOneRequest(req);
    }
  }

  IssuePushBackupInfoReq *req = reqs.RecvNextResponsedRequest();
  if (req == NULL) {
    reqs.UpdateDeviceInDatabase();
    ZSLOG_ERROR("PushBackupInfoReq fail : %s",
                zisync_strerror(ZISYNC_ERROR_TIMEOUT));
    return ZISYNC_ERROR_TIMEOUT;
  }
  if (req->error() != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("PushBackupInfoReq fail : %s",
                zisync_strerror(req->error()));
    return req->error();
  }

  const MsgDevice &remote_device = req->response.response().device();
  if (remote_device.syncs_size() != 1) {
    ZSLOG_ERROR("remote_device.syncs_size() should be 1 but is %d",
                remote_device.syncs_size());
    return ZISYNC_ERROR_INVALID_MSG;
  }
  const MsgSync &remote_sync = remote_device.syncs(0);
  if (remote_sync.trees_size() != 1) {
    ZSLOG_ERROR("remote_sync.trees_size() should be = 1 but is %d",
                remote_sync.trees_size());
    return ZISYNC_ERROR_INVALID_MSG;
  }
  int32_t remote_device_id = StoreDeviceIntoDatabase(
      remote_device, NULL, true, false, false);
  if (remote_device_id <= 0) {
    ZSLOG_ERROR("Store Remote Tree fail");
    return ZISYNC_ERROR_CONTENT;
  }

  unique_ptr<Tree> remote_backup_tree_(
      Tree::GetByUuidWhereStatusNormal(remote_sync.trees(0).uuid()));
  assert(remote_backup_tree_);
  assert(remote_backup_tree_->type() == TableTree::BACKUP_DST);
  if (!remote_backup_tree_ || 
      remote_backup_tree_->type() != TableTree::BACKUP_DST) {
    ZSLOG_ERROR("Tree(%s) does not exist",
                remote_sync.trees(0).uuid().c_str());
    return ZISYNC_ERROR_GENERAL;
  }
  remote_backup_tree_->ToTreeInfo(remote_backup_tree);

  return ZISYNC_SUCCESS;
}

static inline err_t AddBackupTargetLocalDevice(
    int32_t backup_id,
    TreeInfo *remote_backup_tree, const char *backup_root) {
  BackupDstTree tree;
  string backup_root_;
  if (backup_root != NULL) {
    backup_root_ = backup_root;
  }
  err_t zisync_ret = tree.Create(
      TableDevice::LOCAL_DEVICE_ID, backup_id, backup_root_);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  IssueRefresh(tree.id());
  tree.ToTreeInfo(remote_backup_tree);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::AddBackupTargetDevice(
    int32_t backup_id, int32_t device_id, 
    TreeInfo *remote_backup_tree, const char *backup_root /* = NULL */) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call AddBackupTragetDevice before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  
  string backup_root_;
  if (backup_root != NULL) {
    backup_root_ = backup_root;
    if (!IsAbsolutePath(backup_root_) || !NormalizePath(&backup_root_)) {
      ZSLOG_ERROR("Invalid path of backup_root(%s)", backup_root);
      return ZISYNC_ERROR_INVALID_PATH;
    }
    backup_root = backup_root_.c_str();
  }
  
  IContentResolver *resolver = GetContentResolver();

  {
    const char *sync_projs[] = {
      TableSync::COLUMN_ID,
    };
    unique_ptr<ICursor2> sync_cursor(resolver->Query(
            TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
            "%s = %d AND %s = %d AND %s = %d", TableSync::COLUMN_ID, backup_id, 
            TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP, 
            TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
    if (!sync_cursor->MoveToNext()) {
      return ZISYNC_ERROR_SYNC_NOENT;
    }
  }
  
  err_t zisync_ret;
  if (device_id != TableDevice::LOCAL_DEVICE_ID) {
    zisync_ret = AddBackupTargetRemoteDevice(
        backup_id, device_id, remote_backup_tree, backup_root);
  } else {
    zisync_ret = AddBackupTargetLocalDevice(
        backup_id, remote_backup_tree, backup_root);
  }
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  // QueryCache::GetInstance()->NotifyBackupModify();
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::DelBackupTargetDevice(
    int32_t backup_id, int32_t device_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DelBackupDevice before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  
  int32_t dst_tree_id = -1;
  {
    IContentResolver *resolver = GetContentResolver();
    const char* tree_projs[] = {
      TableTree::COLUMN_UUID, TableTree::COLUMN_ID,
    };
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %" PRId32 " AND %s = %d AND %s = %d", 
            TableTree::COLUMN_SYNC_ID, backup_id,
            TableTree::COLUMN_DEVICE_ID, device_id,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));//todo: should be able to del root_moved device
    if (tree_cursor->MoveToNext()) {
      dst_tree_id = tree_cursor->GetInt32(1);
    }
  }
    
  if (dst_tree_id != -1) {
    DelBackupTarget(dst_tree_id);
  }
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::AddBackupTarget(
    int32_t backup_id, int32_t device_id, 
    TreeInfo *remote_backup_tree, const char *backup_root /* = NULL */) {
  return AddBackupTargetDevice(
      backup_id, device_id, remote_backup_tree, backup_root);
}

err_t ZiSyncKernel::DelBackupTarget(int32_t dst_tree_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DelBackupDevice before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  unique_ptr<Tree> dst_tree(Tree::GetByIdWhereStatusNormal(dst_tree_id));//todo: should include root_moved trees
  if (!dst_tree || dst_tree->type() != TableTree::BACKUP_DST) {
    ZSLOG_ERROR("Noent BackupDstTree(%d)", dst_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }
  
  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(dst_tree->sync_id()));
  if (!sync || sync->type() != TableSync::TYPE_BACKUP) {
    ZSLOG_ERROR("Noent Backup(%d)", dst_tree->sync_id());
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  
  err_t zisync_ret = Tree::DeleteById(dst_tree_id);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Tree::DeleteById(%d) fail : %s",
                dst_tree_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  if (sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    IContentResolver *resolver = GetContentResolver();
    const char* tree_projs[] = {
      TableTree::COLUMN_ID,
    };
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d AND %s = %d", 
            TableTree::COLUMN_SYNC_ID, dst_tree->sync_id(),
            TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_SRC,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
    if (tree_cursor->MoveToNext()) {
      int32_t src_tree_id = tree_cursor->GetInt32(1);
      resolver->Delete(
          TableSyncMode::URI, "%s = %d AND %s = %d",
          TableSyncMode::COLUMN_LOCAL_TREE_ID, src_tree_id, 
          TableSyncMode::COLUMN_REMOTE_TREE_ID, dst_tree_id);
    }
  } 
  
  int32_t device_id;
  if (dst_tree->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    // delete in dst
    device_id = sync->device_id();
  } else {
    // delete in src
    device_id = dst_tree->device_id();
  }
  if (device_id != TableDevice::LOCAL_DEVICE_ID) {
    // TODO only need to Push a tree
    IssuePushSyncInfo(dst_tree->sync_id(), device_id);
  }
  // QueryCache::GetInstance()->NotifyBackupModify();
  
  return ZISYNC_SUCCESS;
}

string ZiSyncKernel::GetBackupRoot() {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetBackupRoot before call startup");
    return string();
  }
  return Config::backup_root();
}

err_t ZiSyncKernel::SetBackupRoot(const string &root_) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetBackupRoot before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  string root = root_;
  if (!IsAbsolutePath(root) || !NormalizePath(&root)) {
    ZSLOG_ERROR("Invalid root of root(%s)", 
                root_.c_str());
    return ZISYNC_ERROR_INVALID_PATH;
  }

  if (root == Config::backup_root()) {
    return ZISYNC_SUCCESS;
  }
  
  IContentResolver* resolver = GetContentResolver();

  err_t zisync_ret = SaveBackupRoot(root.c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SaveBackupRoot(%s) fail : %s", 
                root.c_str(), zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  
  const char *device_projs[] = {
    TableDevice::COLUMN_ID, TableDevice::COLUMN_NAME,
  };
  std::unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), NULL));
  while (device_cursor->MoveToNext()) {
    const int32_t device_id = device_cursor->GetInt32(0);
    const char *device_name = device_cursor->GetString(1);
    string backup_root;
    err_t zisync_ret = GenDeviceRootForBackup(
        device_name, &backup_root);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("GenDeviceRootForBackup(%s) fail : %s",
                  device_name, zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    ContentValues cv(1);
    cv.Put(TableDevice::COLUMN_BACKUP_ROOT, backup_root.c_str());
    string where;
    int affected_row_num = resolver->Update(
        TableDevice::URI, &cv, "%s = %d", TableDevice::COLUMN_ID, device_id);
    if (affected_row_num != 1) {
      ZSLOG_ERROR("Update Device(%d) backup_root fail.", device_id);
      return ZISYNC_ERROR_CONTENT;
    }
  }

  IssuePushDeviceMeta();
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::DisableSync(int32_t tree_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DisableSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  
  IContentResolver* resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID
  };

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ID, tree_id, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));//todo: may change root_moved tree/sync into disabled
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Noent tree(%d)", tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  ContentValues cv(1);
  cv.Put(TableTree::COLUMN_IS_ENABLED, false);
  resolver->Update(
      TableTree::URI, &cv, "%s = %d", 
      TableTree::COLUMN_ID, tree_id);
  
  // zs::AbortDelSyncLocalTree(tree_id);
  
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::EnableSync(int32_t tree_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DisableSync before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  IContentResolver* resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID
  };

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ID, tree_id, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Noent tree(%d)", tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  ContentValues cv(1);
  cv.Put(TableTree::COLUMN_IS_ENABLED, true);
  resolver->Update(
      TableTree::URI, &cv, "%s = %d", TableTree::COLUMN_ID, tree_id);

  // zs::AbortAddSyncLocalTree(tree_id);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetSyncMode(
    int32_t local_tree_id, int32_t remote_tree_id, int sync_mode, 
    int32_t sync_time_in_s /* = -1 */) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetSyncMode before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  assert(local_tree_id != remote_tree_id);
  int32_t local_tree_type;
  if (!Tree::GetTypeWhereStatusNormalDeviceLocal(
          local_tree_id, &local_tree_type)) {
    ZSLOG_ERROR("Tree(%d) does not exist", local_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  /* set SyncMode in BackupDst Device is not allowed */
  assert(local_tree_type != TableTree::BACKUP_DST);

  unique_ptr<Tree> remote_tree(
      Tree::GetByIdWhereStatusNormal(remote_tree_id));
  if (!remote_tree) {
    ZSLOG_ERROR("Tree(%d) does not exist", remote_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  int pre_sync_mode;
  zs::GetSyncMode(
      local_tree_id, local_tree_type, remote_tree_id, 
      &pre_sync_mode, NULL);
  err_t zisync_ret = zs::SetSyncMode(
      local_tree_id, remote_tree_id, sync_mode, sync_time_in_s);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SetSyncMode(%d, %d) fail : %s", 
                local_tree_id, remote_tree_id, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  
  if (sync_mode == SYNC_MODE_AUTO) {
    if (pre_sync_mode != SYNC_MODE_AUTO) {
      zs::AbortAddSyncTree(local_tree_id, remote_tree_id);
      IssueSync(local_tree_id, remote_tree_id);
    }
  } else {
    zs::AbortDelSyncTree(local_tree_id, remote_tree_id);
  }

  if (remote_tree->device_id() != TableDevice::LOCAL_DEVICE_ID) {
    IssuePushSyncInfo(
        remote_tree->sync_id(), remote_tree->device_id());
  }

  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::GetSyncMode(
    int32_t local_tree_id, int32_t remote_tree_id, int *sync_mode, 
    int32_t *sync_time_in_s /* = NULL */) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetSyncMode before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  assert(local_tree_id != remote_tree_id);
  int32_t local_tree_type;
  if (!Tree::GetTypeWhereStatusNormalDeviceLocal(
          local_tree_id, &local_tree_type)) {
    ZSLOG_ERROR("Tree(%d) does not exist", local_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }
  if (!Tree::ExistsWhereStatusNormal(remote_tree_id)) {
    ZSLOG_ERROR("Tree(%d) does not exist", remote_tree_id);
    return ZISYNC_ERROR_TREE_NOENT;
  }

  zs::GetSyncMode(
      local_tree_id, local_tree_type, remote_tree_id, 
      sync_mode, sync_time_in_s);
  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::SetLocalDeviceAsCreator(int32_t sync_id) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetSyncMode before call startup");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }

  if (sync->device_id() != TableDevice::NULL_DEVICE_ID) {
    ZSLOG_ERROR("Sync(%d) already has creator", sync_id);
    return ZISYNC_ERROR_SYNC_CREATOR_EXIST;
  }

  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableSync::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  int affected_row_num = resolver->Update(
      TableSync::URI, &cv, "%s = %d AND %s = %d",
      TableSync::COLUMN_ID, sync_id, 
      TableSync::COLUMN_DEVICE_ID, TableDevice::NULL_DEVICE_ID);
  if (affected_row_num != 1) {
    ZSLOG_ERROR("SetLocalDeviceAsCreator in Database fail");
    return ZISYNC_ERROR_CONTENT;
  }

  vector<unique_ptr<Tree>> remote_trees;
  Tree::QueryBy(&remote_trees, "%s = %d AND %s != %d", 
                TableTree::COLUMN_SYNC_ID, sync_id, 
                TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  for (auto iter = remote_trees.begin(); 
       iter != remote_trees.end(); iter ++) {
    if (!Device::IsMyDevice((*iter)->device_id())) {
      err_t zisync_ret = ZISYNC_SUCCESS;
      if ((*iter)->status() == TableTree::STATUS_REMOVE) {
         zisync_ret = zs::SetShareSyncPerm(
            (*iter)->device_id(), sync_id, SP_DISCONNECT);
      } else if ((*iter)->status() == TableTree::STATUS_NORMAL) {
        zisync_ret = zs::SetShareSyncPerm(
            (*iter)->device_id(), sync_id, SP_RDWR);
      }
      if (zisync_ret != ZISYNC_SUCCESS) {
        ZSLOG_WARNING("SetShareSyncPerm fail : %s", 
                      zisync_strerror(zisync_ret));
      }
    }
  }
  IssuePushSyncInfo(sync_id);

  return ZISYNC_SUCCESS;
}

err_t ZiSyncKernel::GetStaticPeers(__OUT ListStaticPeers* peers) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call GetStaticPeers before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return IDiscoverServer::GetInstance()->GetStaticPeers(&(peers->peers));
}

err_t ZiSyncKernel::QueryUserPermission(std::map<UserPermission_t, int32_t> *perms) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call QueryUserPermission before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return GetPermission()->QueryPermission(perms);
}

err_t ZiSyncKernel::AddStaticPeers(__IN const ListStaticPeers& peers) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call AddStaticPeers before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return IDiscoverServer::GetInstance()->AddStaticPeers(peers.peers);
}

err_t ZiSyncKernel::DeleteStaticPeers(__IN const ListStaticPeers& peers) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call DeleteStaticPeers before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return IDiscoverServer::GetInstance()->DeleteStaticPeers(peers.peers);
}

err_t ZiSyncKernel::SaveStaticPeers(__IN const ListStaticPeers &peers) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SaveStaticPeers before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return IDiscoverServer::GetInstance()->SaveStaticPeers(peers.peers);
}

err_t ZiSyncKernel::VerifyCDKey(__IN const std::string &key) {
  if (VerifyKey(key)) {
    return ZISYNC_SUCCESS;
  }
  return ZISYNC_ERROR_CDKEY;
}

err_t ZiSyncKernel::Verify(const std::string &key) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call Verify before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return GetPermission()->Verify(key);
}

VerifyStatus_t ZiSyncKernel::VerifyStatus() {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call VerifyStatus before call startup.");
    return VS_UNKNOW_ERROR;
  }

  return GetPermission()->VerifyStatus();
}


err_t ZiSyncKernel::QueryLicencesInfo(
    struct LicencesInfo *licences) {
  assert(licences != NULL);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call VerifyIdentifyInfo before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return GetLicences()->QueryLicencesInfo(licences);
}

err_t ZiSyncKernel::Bind(const std::string &key) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call Bind before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return GetPermission()->Bind(key);
}

err_t ZiSyncKernel::Unbind() {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call Unbind before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  return GetPermission()->Unbind();
}

bool ZiSyncKernel::CheckPerm(__IN UserPermission_t perm, __IN int32_t data) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call CheckPerm before call startup.");
    return false;
  }

  return GetPermission()->Verify(perm, &data);
}

err_t ZiSyncKernel::SetSyncPerm(int32_t sync_id, int32_t perm) {
  RwLockRdAuto rd_auto(&rwlock);
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call SetSyncPerm before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
  if (!sync) {
    ZSLOG_ERROR("Failed to set sync perm, no sync with id: %" PRId32, sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  if (sync->type() == TableSync::TYPE_BACKUP) {
    ZSLOG_ERROR("Cannot set sync perm for backup.");
    return ZISYNC_ERROR_GENERAL;
  }
  unique_ptr<Device> creator(Device::GetById(sync->device_id()));
  if (creator && !creator->is_mine()) {
    ZSLOG_ERROR("Cannot set sync perm for shared sync.");
    return ZISYNC_ERROR_GENERAL;
  }
  if (sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_ERROR("Cannot set sync perm for creator's sync.");
    return ZISYNC_ERROR_GENERAL;
  }

  int32_t last_perm = sync->perm();
  if (perm == last_perm) {
    return ZISYNC_SUCCESS;
  }

  ContentValues cv(1);
  cv.Put(TableSync::COLUMN_PERM, perm);
  int32_t n_upated = GetContentResolver()->Update(
      TableSync::URI, &cv, "%s=%d",
      TableSync::COLUMN_ID, sync_id);
  if (n_upated != 1) {
    return ZISYNC_ERROR_CONTENT;
  }

  if (last_perm  == TableSync::PERM_RDONLY) {
    IssueRefreshWithSyncId(sync->id());
  }

  return ZISYNC_SUCCESS;

}

err_t ZiSyncKernel::Feedback(const std::string &type,
                      const std::string &version,
                      const std::string &message,
                      const std::string &contact) {
  if (!HasStartup()) {
    ZSLOG_ERROR("You can not call Feedback before call startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }

  ReportDataServer::GetInstance()->Feedback(type, version, message, contact, std::string());

  return ZISYNC_SUCCESS;
}

}  // namespace zs

