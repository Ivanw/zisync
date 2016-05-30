/* Copyright [2014] <zisync.com> */
#ifndef ZISYNC_KERNEL_H_
#define ZISYNC_KERNEL_H_

#if defined _WIN32 || defined _WIN64

#define __IN  __in
#define __OUT __out

#else

#define __IN
#define __OUT

#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&);               \
void operator=(const TypeName&)

#ifdef __APPLE__
#import <Foundation/Foundation.h>
@protocol EventIdInstalDelegation<NSObject>
- (void) event:(NSString*)eventName didAllocateId: (int64_t)event_id;
@end
#endif

namespace zs {

class IpPort;

typedef enum {
  ZISYNC_SUCCESS  = 0,
  ZISYNC_ERROR_NOT_STARTUP, 
  ZISYNC_ERROR_GENERAL,
  ZISYNC_ERROR_CONFIG,
  ZISYNC_ERROR_DEVICE_EXIST,
  ZISYNC_ERROR_DEVICE_NOENT,
  ZISYNC_ERROR_SYNC_EXIST,
  ZISYNC_ERROR_SYNC_NOENT,
  ZISYNC_ERROR_TREE_EXIST,
  ZISYNC_ERROR_TREE_NOENT,
  ZISYNC_ERROR_NESTED_TREE,
  ZISYNC_ERROR_DIR_NOENT,
  ZISYNC_ERROR_FAVOURITE_EXIST,
  ZISYNC_ERROR_FAVOURITE_NOENT,
  ZISYNC_ERROR_SYNC_LIST_EXIST,
  ZISYNC_ERROR_SYNC_LIST_NOENT,
  ZISYNC_ERROR_SYNCDIR_MISMATCH,
  ZISYNC_ERROR_ZMQ,
  ZISYNC_ERROR_TIMEOUT,
  ZISYNC_ERROR_MEMORY,
  ZISYNC_ERROR_CIPHER,
  ZISYNC_ERROR_BAD_PATH,
  ZISYNC_ERROR_OS_IO,
  ZISYNC_ERROR_OS_SOCKET,
  ZISYNC_ERROR_OS_THREAD,
  ZISYNC_ERROR_OS_TIMER,
  ZISYNC_ERROR_OS_EVENT,
  ZISYNC_ERROR_INVALID_MSG,
  ZISYNC_ERROR_PERMISSION_DENY,
  ZISYNC_ERROR_CONTENT,
  ZISYNC_ERROR_SQLITE,
  ZISYNC_ERROR_ADDRINUSE,
  ZISYNC_ERROR_INVALID_PORT,
  ZISYNC_ERROR_INVALID_SYNCBLOB,
  ZISYNC_ERROR_INVALID_UUID,
  ZISYNC_ERROR_UNTRUSTED,
  ZISYNC_ERROR_MONITOR,
  ZISYNC_ERROR_SYNC_LIST, 
  ZISYNC_ERROR_INVALID_METHOD,
  ZISYNC_ERROR_LIBEVENT,

  ZISYNC_ERROR_CANCEL,
  ZISYNC_ERROR_PUT_FAIL,
  ZISYNC_ERROR_TAR,
  ZISYNC_ERROR_REFUSED,
  ZISYNC_ERROR_GETNEWTMPDIR,
  ZISYNC_ERROR_GETTREEROOT,
  ZISYNC_ERROR_INVALID_FORMAT,
  ZISYNC_ERROR_SSL,
  ZISYNC_ERROR_GET_CERTIFICATE,
  ZISYNC_ERROR_GET_PRIVATE_KEY,
  ZISYNC_ERROR_PRIVATE_KEY_CHECK,
  ZISYNC_ERROR_GET_CA,
  ZISYNC_ERROR_INVALID_ID,
  ZISYNC_ERROR_HTTP_RETURN_ERROR,

  /*  for DiscoverDevice */
  ZISYNC_ERROR_DISCOVER_LIMIT,
  ZISYNC_ERROR_DISCOVER_NOENT,

  /*  for Download */
  ZISYNC_ERROR_DOWNLOAD_NOENT,
  ZISYNC_ERROR_FILE_NOENT,

  ZISYNC_ERROR_VERSION_INCOMPATIBLE,
  ZISYNC_ERROR_INVALID_PATH,

  ZISYNC_ERROR_NOT_DIR,

  ZISYNC_ERROR_BACKUP_DST_EXIST,
  ZISYNC_ERROR_BACKUP_SRC_EXIST,

  ZISYNC_ERROR_SHARE_SYNC_NOENT,
  ZISYNC_ERROR_SHARE_SYNC_DISCONNECT,
  ZISYNC_ERROR_NOT_SYNC_CREATOR,
  ZISYNC_ERROR_SYNC_CREATOR,
  ZISYNC_ERROR_SYNC_CREATOR_EXIST,

  ZISYNC_ERROR_DOWNLOAD_FILE_TOO_LARGE,
  ZISYNC_ERROR_AGAIN,
  ZISYNC_ERROR_FILE_EXIST,
  ZISYNC_ERROR_INVALID_KEY_CODE,
  ZISYNC_ERROR_LIMITED_KEY_BIND,

  ZISYNC_ERROR_CDKEY,
  ZISYNC_ERROR_BIND,
  ZISYNC_ERROR_UNBIND,
  ZISYNC_ERROR_VERIFY,
  ZISYNC_ERROR_MACTOKEN_MISMATCH,
  ZISYNC_ERROR_ROOT_MOVED,
  
  /*  for file access */
  ZISYNC_ERROR_SHA1_FAIL,

  ZISYNC_ERROR_NUM,
} err_t;

const char* zisync_strerror(err_t err);

class IpPort {
 public:
  IpPort() {}
  IpPort(const char *ip, int32_t port): ip_(ip), port_(port) {}
  std::string ip_;
  int32_t port_;
};

/*
 * Sync and Tree management
 */
class DeviceInfo {
 public:
  int32_t device_id;
  std::string device_name;
  int device_type;
  bool is_mine;
  bool is_backup;
  bool is_online;
  bool is_shared;
  std::string backup_root;
  int32_t version;

  std::string ip;
  int32_t discover_port;
  bool is_static_peer;
};

enum TreeRootStatus {
  TreeRootStatusNormal,
  TreeRootStatusRemoved,
};

class TreeInfo {
 public:
  int32_t tree_id;
  std::string tree_uuid;
  std::string tree_root;
  DeviceInfo device;
  bool is_local;
  bool is_sync_enabled;
//only set for local tree
  TreeRootStatus root_status;
};

class ShareSyncInfo {
 public:
  DeviceInfo device;
  int32_t    sync_perm;
};

class SyncInfo {
 public:
  int32_t                        sync_id;
  std::string                    sync_uuid;
  std::string                    sync_name;
  int32_t                        sync_perm;
  int64_t                        last_sync;
  bool                           is_share;
  DeviceInfo                     creator;
  std::vector<TreeInfo>          trees;
  std::vector<ShareSyncInfo>     share_syncs;
};

class QuerySyncInfoResult {
 public:
  std::vector<SyncInfo> sync_infos;
};

/**
 * Phone backup management
 */
class BackupInfo {
 public:
  int32_t   backup_id;
  std::string backup_name;
  int64_t   last_sync;
  TreeInfo  src_tree;
  std::vector<TreeInfo> target_trees;
};

class QueryBackupInfoResult {
 public:
  std::vector<BackupInfo> backups;
};

typedef enum {
    FILE_TRANSFER_STATUS_DOWN,
    FILE_TRANSFER_STATUS_UP,
    FILE_TRANSFER_STATUS_WAITUP,
    FILE_TRANSFER_STATUS_WAITDOWN,
    FILE_TRANSFER_STATUS_INDEX,
} FileTransferStatus;

typedef enum {
    FILE_TRANSFER_ERROR_NONE,
    FILE_TRANSFER_ERROR_UNKNOWN,
    FILE_TRANSFER_ERROR_NETWORK,
    FILE_TRANSFER_ERROR_CONFLICT,
    FILE_TRANSFER_ERROR_REMOTE_IO,
    FILE_TRANSFER_ERROR_LOCAL_IO,
    FILE_TRANSFER_ERROR_SHA1,

}FileTransferError;

typedef enum {
  USER_PERMISSION_FUNC_DEVICE_SWITCH,
  USER_PERMISSION_FUNC_SHARE_READWRITE,
  USER_PERMISSION_FUNC_SHARE_READ,
  USER_PERMISSION_FUNC_SHARE_WRITE,
  USER_PERMISSION_FUNC_REMOTE_DOWNLOAD,
  USER_PERMISSION_FUNC_REMOTE_UPLOAD,
  USER_PERMISSION_FUNC_REMOTE_OPEN,
  USER_PERMISSION_FUNC_TRANSFER_LIST,
  USER_PERMISSION_FUNC_HISTROY,
  USER_PERMISSION_FUNC_EDIT_SHARE_PERMISSION,
  USER_PERMISSION_FUNC_REMOVE_SHARE_DEVICE,

  USER_PERMISSION_CNT_CREATE_TREE,
  USER_PERMISSION_CNT_CREATE_SHARE,
  USER_PERMISSION_CNT_BROWSE_REMOTE,
  USER_PERMISSION_CNT_STATICIP,
  USER_PERMISSION_CNT_BACKUP,

  USER_PERMISSION_NUM
}UserPermission_t;

typedef enum {
  VS_OK,
  VS_WAITING,
  VS_INVALID_KEY_CODE,
  VS_LIMITED_KEY_BIND,
  VS_KEY_EXPIRED,

  VS_DEVICE_NOT_EXISTS,
  VS_DEVICE_NOT_BIND,

  VS_PERMISSION_DENY,
  VS_NETWORK_ERROR,
  VS_UNKNOW_ERROR,

  VS_NUM,
}VerifyStatus_t;

typedef enum {
  LS_OK,
  LS_EXPIRED_OFFLINE_TIME,
  LS_EXPIRED_TIME,
  LS_INVALID,

  LS_NUM,
}LS_t;
  
typedef enum {
  LT_DEFAULT,
  LT_TRIAL,
  LT_PREMIUM,
}LT_t;

struct LicencesInfo {
  std::string role;
  std::string qrcode;
  int64_t expired_time;
  int64_t created_time;
  int64_t expired_offline_time;
  int64_t last_contact_time;
  int64_t left_time;
  LS_t status;
  LT_t license_type;
};

struct FileTransferStat {
    std::string local_path;
    std::string remote_path;
    int64_t bytes_file_size;
    int64_t bytes_to_transfer;
    int64_t speed;
    FileTransferStatus transfer_status;
};

struct TransferListStatus {
  std::vector<FileTransferStat> list_;
};

struct TreeStatus {
 public:
  int32_t tree_id;
  int32_t num_file_to_index;
  int32_t num_file_to_download;
  int64_t num_byte_to_download;
  int64_t speed_download;
  int32_t num_file_to_upload;
  int64_t num_byte_to_upload;
  int64_t speed_upload;
  bool is_transfering;
  std::string file_indexing;
};

struct TreePairStatus {
 public:
  int32_t local_tree_id;
  int32_t remote_tree_id;
  int32_t static_num_file_to_download; // before sync
  int32_t static_num_file_to_upload;
  int64_t static_num_byte_to_download; // before sync
  int64_t static_num_byte_to_upload;
  int32_t static_num_file_consistent;
  int64_t static_num_byte_consistent;

  int32_t num_file_to_download; // in sync
  int32_t num_file_to_upload;
  int64_t num_byte_to_download; // in sync
  int64_t num_byte_to_upload;
  int64_t speed_download;
  int64_t speed_upload;
  std::string download_file; // relative path
  std::string upload_file;
  bool is_transfering;
};

enum FileMetaType {
  FILE_META_TYPE_REG,
  FILE_META_TYPE_DIR,
};

class FileMeta {
 public:
  std::string  name;  // relative path
  FileMetaType type;
  int64_t      length;
  int64_t      mtime;  // is ms
  bool         has_download_cache;
  std::string  cache_path;
};

class ListSyncResult {
 public:
  std::vector<FileMeta> files;
};

class IEventListener {
 public:
  virtual ~IEventListener() {}
  virtual void NotifySyncStart(int32_t sync_id, int32_t sync_type) {}
  virtual void NotifySyncFinish(int32_t sync_id, int32_t sync_type) {}
  virtual void NotifySyncModify() {}
  virtual void NotifyBackupModify() {}
  virtual void NotifyDownloadFileNumber(int num) {}
  virtual void NotifyIndexStart(int32_t sync_id, int32_t sync_type) {}
  virtual void NotifyIndexFinish(int32_t sync_id, int32_t sync_type) {}
};

class DiscoverDeviceResult {
 public:
  bool is_done;
  std::vector<DeviceInfo> devices;
};

const int DOWNLOAD_PREPARE = 1,
      DOWNLOAD_PRECESS = 2,
      DOWNLOAD_DONE = 3,
      DOWNLOAD_FAIL = 4;
class DownloadStatus {
 public :
  int status;
  int64_t total_num_byte;
  int64_t num_byte_to_download; // the byte that has not download ye
  int64_t speed_download; // bytes/s
  err_t error_code;
};

const int UPLOAD_PREPARE = 1,
      UPLOAD_PRECESS = 2,
      UPLOAD_DONE = 3,
      UPLOAD_FAIL = 4;
class UploadStatus {
 public :
  int status;
  int64_t total_num_byte;
  int64_t num_byte_to_upload; // the byte that has not download ye
  int64_t speed_upload; // bytes/s
  err_t error_code;
};

class ListBackupTargetDeviceResult {
 public:
  std::vector<DeviceInfo> devices;
};

class ListStaticPeers {
 public:
  std::vector<IpPort> peers;
};

const int FAVORITE_NOT = 0, FAVORITE_CANCELABLE = 1, 
      FAVORITE_UNCANCELABLE = 2;
const int SYNC_MODE_AUTO = 0, SYNC_MODE_MANUAL = 1,
      SYNC_MODE_TIMER = 2;
const int32_t LOCAL_DEVICE_ID = 0, NULL_DEVICE_ID = -1;
const int DEVICE_TYPE_ANDROID = 1, 
      DEVICE_TYPE_IPHONE = 2,
      DEVICE_TYPE_WINDOWS = 3,
      DEVICE_TYPE_MAC = 4,
      DEVICE_TYPE_UNKOWN = 5;
const int32_t SYNC_PERM_RDONLY = 1,
      SYNC_PERM_WRONLY = 2, SYNC_PERM_RDWR = 3,
      // SYNC_PERM_DISCONNECT = 4,
      // SYNC_PERM_CREATOR_DELETE = 5,
      // SYNC_PERM_TOKEN_DIFF = 6;
      SYNC_PERM_DISCONNECT_RECOVERABLE = 4, 
      SYNC_PERM_DISCONNECT_UNRECOVERABLE = 5;

const int FILE_OPERATION_CODE_ADD = 1;
const int FILE_OPERATION_CODE_DELETE = 2;
const int FILE_OPERATION_CODE_MODIFY = 3;
const int FILE_OPERATION_CODE_RENAME = 4;
const int FILE_OPERATION_CODE_MKDIR = 5;
const int FILE_OPERATION_CODE_ATTRIB = 6;

const int FILE_OPERATION_ERROR_NONE = 0;
const int FILE_OPERATION_ERROR_CONFLICT = 1;
const int FILE_OPERATION_ERROR_TRANSFER = 2;
const int FILE_OPERATION_ERROR_IO = 3;
const int FILE_OPERATION_ERROR_MOD_RDONLY = 4;

const int32_t BACKUP_TYPE_NONE = 1;//tree for sync
const int32_t BACKUP_TYPE_SRC = 2;//tree as backup source
const int32_t BACKUP_TYPE_DST = 4;//tree as backup destination
const int32_t BACKUP_TYPE_ANY = -1;//any tree

const int32_t SYNC_TYPE_NORMAL = 1;
const int32_t SYNC_TYPE_BACKUP = 2;
const int32_t SYNC_TYPE_SHARED = 3;

/* for permission */
const int32_t PERMISSION_NO_LIMIT = -1;
const int32_t PERMISSION_DENY = 0;

class History {
  public:
    std::string modifier;
    int32_t tree_id;
    int32_t backup_type;
    std::string frompath;
    std::string topath;
    int64_t time_stamp;
    int code;
    int error;
};

class QueryHistoryResult {
  public:
    std::vector<History> histories;
};

#ifdef __APPLE__
#define kTaskCompleteNotification @"kTaskCompleteNotification"
#define kTaskType @"kTaskType"
#define kTaskReturnCode @"kTaskReturnCode"
#define kTaskCompleteNotificationDetailString @"kTaskCompleteNotificationDetailString"
  
enum TaskType_t {
  TaskTypeDownload,
  TaskTypeUpload,
  TaskTypeSync,
  TaskTypeBackup,
  TaskTypeTransfer,
};
  
#endif
  
class IZiSyncKernel {
 public:
  virtual ~IZiSyncKernel() { /* virtual destructor */ }

  /**
   * Initialize, Startup and Shutdown zisync framework.
   *
   * @param appdata specify where to store sqlite database for the application
   * If the Startup() returns ZISYNC_ERROR_CONFIG means the database maybe not
   * initialized, the application SHOULD try to re-initialize it with
   * Initialize(), and then re-call Startup() again.
   *
   * Startup() MUST be called before any other Kernel function. All the other
   * Kernel function (except Initialize() and Shutdown()) SHOULD only be called
   * after Startup() return ZISYNC_SUCCESS, otherwise the result is undermined.
   *
   * Initialize() will use 8848, 9527, 9526 as discover port, route port and
   * data port. If the required port is not available, the Kernel will choose
   * a port randomly by itsself. You can change the port by calling
   * SetDiscover(Route/Data)Port() later.
   *
   * @return: ZISYNC_ERROR_INVALID_PATH 
   */
  virtual err_t Initialize(__IN const char* appdata,
                           __IN const char* username,
                           __IN const char* password,
                           __IN const char* backup_root = NULL,
                           __IN const std::vector<std::string>* mactokens = NULL) = 0;
  /*  if discover_port = -1, use the discover port stored in database */
  virtual err_t Startup(__IN const char* appdata, 
                        __IN int32_t discover_port,
                        __IN IEventListener *listener,
                        __IN const char* tree_root_prefix = NULL,
                        __IN const std::vector<std::string>* mactokens = NULL) = 0;
  virtual void Shutdown() = 0;
  /**
   * return the device uuid and device name, should never failed.
   */
  virtual const std::string GetDeviceUuid() = 0;
  virtual const std::string GetDeviceName() = 0;
  virtual err_t SetDeviceName(__IN const char* device_name) = 0;

  /**
   * Account management
   *
   * Set @param password to NULL if you don't want to change password.
   */
  virtual const std::string GetAccountName() = 0;
  virtual err_t SetAccount(__IN const char* username,
                           __IN const char* password) = 0;

  /**
   * Network Configuration
   *
   * All the getter should never failed.
   *
   */
  virtual int32_t GetDiscoverPort() = 0;
  virtual int32_t GetRoutePort() = 0;
  virtual int32_t GetDataPort() = 0;

  /**
   * Set the network port
   *
   * @return : ZISYNC_ERROR_ADDRINUSE, ZISYNC_ERROR_INVALID_PORT
   */
  virtual err_t SetDiscoverPort(__IN int32_t new_port) = 0;
  virtual err_t SetRoutePort(__IN int32_t new_port) = 0;
  virtual err_t SetDataPort(__IN int32_t new_port) = 0;

  /**
   * Transfer thread number
   */
  virtual int32_t GetTransferThreadCount() = 0;
  virtual err_t SetTransferThreadCount(__IN int32_t new_thread_count) = 0;

  /**
   * Speed limitation
   */
  virtual int64_t GetUploadLimit() = 0;
  virtual int64_t GetDownloadLimit() = 0;
  virtual err_t SetUploadLimit(int64_t bytes_per_second) = 0;
  virtual err_t SetDownloadLimit(int64_t bytes_per_second) = 0;

  /**
   * Sync interval
   */
  virtual int32_t GetSyncInterval() = 0;
  virtual err_t SetSyncInterval(int32_t interval_in_second) = 0;
  /**
   * List all sync folder as well as trees linked to each syncfolder.
   *
   * This function should return ZISYNC_SUCCESS if the framework
   * startup properly.
   */
  virtual err_t QuerySyncInfo(__OUT QuerySyncInfoResult* result) = 0;
  virtual err_t QuerySyncInfo(
      __IN int32_t sync_id, __OUT SyncInfo *sync_info) = 0;

  /*
   * Create A new syncfolder without any tree linked to it.
   *
   * both @param sync_id and @param sync_info.sync_uuid can be used to identify
   * the syncfolder uniquely. @param sync_id is used within the application,
   * while sync_uuid is used in communication of different instance of the
   * application.
   *
   * the sync_id, sync_uuid, tree_id, tree_uuid of the created syncfolder is
   * returned in @param sync_info. Passing NULL to @param sync_info if
   * information of the new created syncford is not needed.
   *
   * @param sync_name is a string used as display name for this syncfolder,
   *  usually the last segment of @param tree_root
   */
  virtual err_t CreateSync(__IN  const char* sync_name,
                           __OUT SyncInfo* sync_info) = 0;

  /**
   * @return: ZISYNC_ERROR_SYNC_NOENT
   */
  virtual err_t DestroySync(__IN int32_t sync_id) = 0;
  /**
   * @return: ZISYNC_ERROR_INVALID_SYNCBLOB, ZISYNC_ERROR_SYNC_EXIST;
   */
  virtual err_t ImportSync(__IN  const std::string& blob,
                           __OUT SyncInfo* sync_info) = 0;
  /* @return: ZISYNC_ERROR_SYNC_NOENT */
  virtual err_t ExportSync(__IN int32_t sync_id, __OUT std::string* blob) = 0;

  /*
   * For Default the favorites is empty
   * Create A new Tree which link to the given syncfolder identified by
   * @param favorites use the relative path, for example the root path is "/"
   * @return ZISYNC_ERROR_TREE_EXIST, ZISYNC_ERROR_NETSTED_TREE, ZISYNC_ERROR_SYNC_NOENT,
   *         ZISYNC_ERROR_DIR_NOENT, ZISYNC_ERROR_SYNC_LIST 
   */
  virtual err_t CreateTree(__IN  int32_t sync_id,
                           __IN  const char* tree_root,
                           __OUT TreeInfo* tree_info) = 0;
  /* @return ZISYNC_ERROR_TREE_NOENT */
  virtual err_t DestroyTree(__IN int32_t tree_id) = 0;
  virtual err_t QueryTreeInfo(
      __IN int32_t tree_id, __OUT TreeInfo *tree_info) = 0;
  virtual err_t SetTreeRoot(
      __IN int32_t tree_id, __IN const char* tree_root) = 0;

  /* @return ZISYNC_ERROR_CONENT, ZISYNC_ERROR_FAVORITE_EXIST */
  virtual err_t AddFavorite(
      __IN int32_t tree_id, __IN const char* favorite) = 0;
  /* @return ZISYNC_ERROR_CONENT, ZISYNC_ERROR_FAVORITE_NOENT */
  virtual err_t DelFavorite(
      __IN int32_t tree_id, __IN const char* favorite) = 0;
  /* path is the relative path to tree root */
  virtual int GetFavoriteStatus(__IN int32_t tree_id, __IN const char *path) = 0;
  virtual bool HasFavorite(__IN int32_t tree_id) = 0;

  /*
   * Send a sync once request to the given syncfolder identified by sync_id;
   *
   * The request is handled asynchronously. The function returns after the
   * request is queued for processing.
   */
  virtual err_t SyncOnce(__IN int32_t sync_id) = 0;

  /**
   * Query status of the given tree identified by @param tree_id .
   *
   * set tree_id = -1 if you want total status for all tree.
   * max_file_num = 0 dont query transfer list
   * max_file_num = -1 no max limit
   */
   virtual err_t QueryTreeStatus(
       __IN int32_t tree_id, __OUT TreeStatus* tree_status) = 0;
  virtual err_t QueryTreePairStatus(
      __IN int32_t local_tree_id, __IN int32_t remote_tree_id, 
      __OUT TreePairStatus* tree_status) = 0;
   virtual err_t QueryTransferList(
       __IN int32_t tree_id, __OUT TransferListStatus* transfer_list,
       __IN int32_t offset, __IN int32_t max_num) = 0;
  /* @return ZISYNC_ERROR_OS_THREAD, ZISYNC_ERROR_DISCOVER_LIMIT */
  virtual err_t StartupDiscoverDevice(__IN int32_t sync_id, __OUT int32_t *discover_id) = 0;
  /* @return ZISYNC_ERROR_DISCOVER_NOENT */
  virtual err_t GetDiscoveredDevice(
      __IN int32_t discover_id, __OUT DiscoverDeviceResult *result) = 0;
  /* @return ZISYNC_ERROR_DISCOVER_NOENT */
  virtual err_t ShutdownDiscoverDevice(__IN int32_t discover_id) = 0;
  /* @return ZISYNC_ERROR_DISCOVER_NOENT, ZISYNC_ERROR_DEVICE_NOENT,
   *         ZISYNC_ERROR_ERROR_SYNC_NOENT*/
  virtual err_t ShareSync(
      __IN int32_t discover_id, __IN int32_t device_id, 
      __IN int32_t sync_perm) = 0;
  /* Call in Creator
   * @return ZISYNC_ERROR_DEVICE_NOENT, ZISYNC_ERROR_SYNC_NOENT,
   *         ZISYNC_ERROR_SHARE_SYNC_NOENT*/
  virtual err_t CancelShareSync(
      __IN int32_t device_id, __IN int32_t sync_id) = 0;
  /* Call in shared device 
   * @return ZISYNC_ERROR_SYNC_NOENT,
   *         ZISYNC_ERROR_SYNC_CREATOR */
  virtual err_t DisconnectShareSync(__IN int32_t sync_id) = 0;
  /* @return ZISYNC_ERROR_DEVICE_NOENT, ZISYNC_ERROR_SYNC_NOENT,
   *         ZISYNC_ERROR_SHARE_SYNC_NOENT*/
  virtual err_t SetShareSyncPerm(
      __IN int32_t device_id, __IN int32_t sync_id, 
      __IN int32_t sync_perm) = 0;
  /* device_id can be LOCAL_DEVICE_ID */
  /* @return ZISYNC_ERROR_SYNC_NOENT,
   *         ZISYNC_ERROR_SHARE_SYNC_NOENT */
  virtual err_t GetShareSyncPerm(
      __IN int32_t device_id, __IN int32_t sync_id, 
      __OUT int32_t *sync_perm) = 0;

  /**
   * @param path is the relative path to the tree root
   * @return: ZISYNC_ERROR_SYNC_NOENT, ZISYNC_ERROR_TIMEOUT, 
   *          ZISYNC_ERROR_CONTENT */
#ifdef __APPLE__
  virtual err_t ListSync(__IN int32_t sync_id, __IN const std::string& path
                         , int64_t *list_sync_id) = 0;
#else
  virtual err_t ListSync(__IN int32_t sync_id, __IN const std::string& path,
                         __OUT ListSyncResult *result) = 0;
#endif
  /**
   * @param relative_path : relative to the tree root
   * @param target_path: the path where to put the download file
   *
   * @return: ZISYNC_ERROR_SYNC_NOENT, ZISYNC_ERROR_OS_THREAD,
   *          ZISYNC_ERROR_DOWNLOAD_FILE_TOO_LARGE,
   */
  virtual err_t StartupDownload(
      __IN int32_t sync_id, __IN const std::string& relative_path, 
      __OUT std::string* target_path, __OUT int32_t *task_id) = 0;
  /**
   * @return: ZISYNC_ERROR_DOWNLOAD_NOENT
   * */
  virtual err_t ShutdownDownload(__IN int32_t task_id) = 0;
  /**
   * @return: ZISYNC_ERROR_DOWNLOAD_NOENT
   * */
  virtual err_t QueryDownloadStatus(
      __IN int32_t task_id, __OUT DownloadStatus *status) = 0;

  virtual err_t SetDownloadCacheVolume(int64_t new_volume) = 0;
  virtual int64_t GetDownloadCacheVolume() = 0;
  virtual err_t CleanUpDownloadCache() = 0;
  virtual int64_t GetDownloadCacheAmount() = 0;

  /**
   * @param relative_path : relative to the tree root where to put the file
   * @param target_path: the path of the upload file
   *
   * @return: ZISYNC_ERROR_SYNC_NOENT, ZISYNC_ERROR_OS_THREAD
   */
  virtual err_t StartupUpload(
      __IN int32_t sync_id, __IN const std::string& relative_path, 
      __OUT const std::string& target_path, __OUT int32_t *task_id) = 0;
  /**
   * @return: ZISYNC_ERROR_UPLOAD_NOENT
   * */
  virtual err_t ShutdownUpload(__IN int32_t task_id) = 0;
  /**
   * @return: ZISYNC_ERROR_UPLOAD_NOENT
   * */
  virtual err_t QueryUploadStatus(
      __IN int32_t task_id, __OUT UploadStatus *status) = 0;

  virtual err_t RemoveRemoteFile(
      __IN int32_t sync_id, __IN const std::string &relative_path) = 0;

  virtual err_t SetBackground(__IN int32_t interval = 300) = 0;
  virtual err_t SetForeground() = 0;

  /**
   * List all phone backup folder information as well as trees linked to it.
   *
   * This function should return ZISYNC_SUCCESS if the framework startup properly.
   * Usually, there should only one tree for each backup.
   */
  virtual err_t QueryBackupInfo(__OUT QueryBackupInfoResult* result) = 0;
  virtual err_t QueryBackupInfo(
      __IN int32_t backup_id, __OUT BackupInfo *backup_info) = 0;

  virtual err_t QueryHistoryInfo(__OUT QueryHistoryResult *histories
      , __IN int32_t offset = 0, __IN int32_t limit = -1, __IN int32_t backup_type = -1) = 0;

  /**
   * Create A new backupfolder base on the given @param tree_root.
   *
   * both @param sync_id and @param sync_info.sync_uuid can be used to identify
   * the syncfolder uniquely. @param sync_id is used within the application,
   * while sync_uuid is used in communication of different instance of the
   * application.
   *
   * the sync_id, sync_uuid, tree_id, tree_uuid of the created syncfolder is
   * returned in @param sync_info. Passing NULL to @param sync_info if
   * information of the new created syncford is not needed.
   *
   * @param sync_name is a string used as display name for this syncfolder,
   *  usually the last segment of @param tree_root
   * 
   * @return: ZISYNC_ERROR_TREE_EIXST if already a sync
   *          ZISYNC_ERROR_BACKUP_DST_EXIST
   *          ZISYNC_ERROR_BACKUP_SRC_EXIST
   */
  virtual err_t CreateBackup(
      __IN  const char* backup_name, __IN const char *root, 
      __OUT BackupInfo* backup_info) = 0;
  virtual err_t DestroyBackup(__IN int32_t backup_id) = 0;

  /**
   * @return: ZISYNC_ERROR_SYNC_NOENT, ZISYNC_ERROR_DEVICE_NOENT,
   *          if BackupDevice exist, return SUCCESS
   *          if remote device create backup_root fail, return
   *          ZISYNC_ERROR_OS_IO;
   *          if remote_device has file with path of backup_root, return 
   *          ZISYNC_ERROR_NOT_DIR;
   *          if the backup_root is already a sync dir, return 
   *          ZISYNC_ERROR_TREE_EXIST
   */
  virtual err_t AddBackupTargetDevice(
      __IN int32_t backup_id, __IN int32_t device_id,
      __OUT TreeInfo *remote_backup_tree,
      __IN const char *backup_root = NULL) = 0;
  /**
   * @return: if BackupDevice noent, return SUCCESS
   */
  virtual err_t DelBackupTargetDevice(
      __IN int32_t backup_id, __IN int32_t device_id) = 0;

  virtual err_t AddBackupTarget(
      __IN int32_t backup_id, __IN int32_t device_id,
      __OUT TreeInfo *remote_backup_tree,
      __IN const char *backup_root = NULL) = 0;
  virtual err_t DelBackupTarget(__IN int32_t dst_tree_id) = 0;

  /**
   * @return: ZISYNC_ERROR_SYNC_NOENT
   */
  virtual err_t ListBackupTargetDevice(
      __IN int32_t backup_id, __OUT ListBackupTargetDeviceResult *result) = 0;

  virtual err_t EnableSync(__IN int32_t tree_id) = 0;
  virtual err_t DisableSync(__IN int32_t tree_id) = 0;

  virtual std::string GetBackupRoot() = 0;
  virtual err_t SetBackupRoot(__OUT const std::string &root) = 0;

  /**
   * @param sync_time_in_s: for example , sync in 1:00, => 3600 
   *
   * @return: 
   */
  virtual err_t SetSyncMode(
      __IN int32_t local_tree_id, __IN int32_t remote_tree_id, 
      __IN int sync_mode, __IN int32_t sync_time_in_s = -1) = 0;
  virtual err_t GetSyncMode(
      __IN int32_t local_tree_id, __IN int32_t remote_tree_id, 
      __OUT int *sync_mode, __OUT int32_t *sync_time_in_s = NULL) = 0;

  /* @return: ZISYNC_ERROR_SYNC_NOENT, ZISYNC_ERROR_SYNC_CREATOR_EXIST */
  virtual err_t SetLocalDeviceAsCreator(__IN int32_t sync_id) = 0;
  virtual int32_t GetVersion() = 0;

  virtual err_t GetStaticPeers(__OUT ListStaticPeers* peers) = 0;
  virtual err_t AddStaticPeers(__IN const ListStaticPeers& peers) = 0;
  virtual err_t DeleteStaticPeers(__IN const ListStaticPeers& peers) = 0;
  virtual err_t SaveStaticPeers(__IN const ListStaticPeers& peers) = 0;

  virtual err_t VerifyCDKey(__IN const std::string &key) = 0;
  /* @return: ZISYNC_SUCCESS */
  virtual err_t QueryUserPermission(
      __OUT std::map<UserPermission_t, int32_t> *perms) = 0;

  /* @return: ZISYNC_SUCCESS */
  virtual err_t Verify(__IN const std::string &key) = 0;

  /* @return verify status */
  virtual VerifyStatus_t VerifyStatus() = 0;

  /* @return verify identify info */
  virtual err_t QueryLicencesInfo(
      __OUT struct LicencesInfo *licences) = 0;

  /* @return ZISYNC_SUCCESS */
  virtual err_t Bind(const std::string &key) = 0;

  /* @return ZISYNC_SUCCESS */
  virtual err_t Unbind() = 0;

  /* @return true or false */
  virtual bool CheckPerm(__IN UserPermission_t perm, __IN int32_t data) = 0;
  virtual err_t SetSyncPerm(int32_t sync_id, int32_t perm) = 0;

  virtual err_t Feedback(__IN const std::string &type,
                         __IN const std::string &version,
                         __IN const std::string &message,
                         __IN const std::string &contact) = 0;
};

/*  type can be "virtual" or "actual" */
IZiSyncKernel* GetZiSyncKernel(const char* type);

class ILogger {
 public:
  virtual ~ILogger() { /* virtual destructor */ }

  virtual void AppendToLog(
      int severity,
      const char* full_filename, int line,
      const char* log_time, int thread_id,
      const char* message, size_t message_len) = 0;
};

void LogInitialize(class ILogger* provider);
void LogCleanUp();

class DefaultLogger : public ILogger {
 public:
  explicit DefaultLogger(const char* log_dir);
  virtual ~DefaultLogger();

  // never call it twice, you should call this before call LogInitialize
  void Initialize();
  void CleanUp();

  virtual void AppendToLog(
      int severity,
      const char* full_filename, int line,
      const char* log_time, int thread_id,
      const char* message, size_t message_len);

  bool info_to_stdout;
  bool warning_to_stdout;
  bool error_to_stderr;
  bool fatal_to_stderr;

 private:
  std::string log_dir_;
  FILE* log_fp_;

};

}  // namespace zs

#endif  // ZISYNC_KERNEL_H_
