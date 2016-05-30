/* Copyright [2014] <zisync.com> */
#ifndef ZISYNC_KERNEL_DATABASE_TABLE_H_
#define ZISYNC_KERNEL_DATABASE_TABLE_H_

#include "zisync/kernel/database/icontent.h"

namespace zs {

class TableDevice {
 public:
  virtual ~TableDevice() = 0;
  static const Uri URI;
  static const int32_t LOCAL_DEVICE_ID, NULL_DEVICE_ID;
  static const int32_t STATUS_ONLINE, STATUS_OFFLINE;
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[], 
               CREATE_IS_MINE_INDEX_SQL[], CREATE_STATUS_INDEX_SQL[],
               COLUMN_ID[], COLUMN_UUID[], COLUMN_NAME[], COLUMN_ROUTE_PORT[],
               COLUMN_DATA_PORT[], COLUMN_STATUS[], COLUMN_TYPE[], 
               COLUMN_IS_MINE[], COLUMN_BACKUP_ROOT[], 
               COLUMN_BACKUP_DST_ROOT[], COLUMN_VERSION[];
};

class TableDeviceIP {
 public:
  virtual ~TableDeviceIP() = 0;
  static const Uri URI;
  static const int64_t EARLIEST_NO_RESP_TIME_NONE;
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[],
               CREATE_DEVICE_ID_INDEX_SQL[],
               CREATE_EARLIEST_NO_RESP_TIME_INDEX_SQL[],
               COLUMN_ID[], COLUMN_DEVICE_ID[], COLUMN_IP[], COLUMN_IS_IPV6[],
               COLUMN_EARLIEST_NO_RESP_TIME[];
};

class TableStaticPeer {
 public:
  virtual ~TableStaticPeer() = 0;
  static const Uri URI;
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[],
               CREATE_IP_INDEX_SQL[],
               COLUMN_ID[], COLUMN_IP[], COLUMN_PORT[];
};

class TableSync {
 public:
  virtual ~TableSync() = 0;
  static const int32_t TYPE_NORMAL, TYPE_BACKUP, TYPE_SHARED;
  static const int32_t STATUS_NORMAL, STATUS_REMOVE;
  static const int32_t RESTORE_SHARE_PERM_NULL;
  static const int32_t PERM_RDONLY, PERM_WRONLY, PERM_RDWR, 
               PERM_CREATOR_DELETE, PERM_TOKEN_DIFF, PERM_DISCONNECT;
  static const int64_t LAST_SYNC_NONE;
  static const Uri URI;
  static const char CREATE_STATUS_INDEX_SQL[], CREATE_TYPE_INDEX_SQL[],
               CREATE_DEVICE_ID_INDEX_SQL[], 
               CREATE_RESTORE_SHARE_PERM_INDEX_SQL[];
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[], 
               COLUMN_ID[], COLUMN_UUID[], COLUMN_NAME[], COLUMN_LAST_SYNC[],
               COLUMN_TYPE[], COLUMN_STATUS[], COLUMN_DEVICE_ID[], 
               COLUMN_PERM[], COLUMN_RESTORE_SHARE_PERM[];
};

class TableTree {
 public:
  virtual ~TableTree() = 0;
  static const Uri URI;
  static const int32_t STATUS_NORMAL, STATUS_REMOVE, STATUS_VCLOCK;
  static const int32_t BACKUP_NONE, BACKUP_SRC, BACKUP_DST;
  static const int64_t LAST_FIND_NONE;
  static const int32_t ROOT_STATUS_NORMAL, ROOT_STATUS_REMOVED;
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[], 
               CREATE_DEVICE_ID_INDEX_SQL[],
               CREATE_SYNC_ID_INDEX_SQL[], CREATE_STATUS_INDEX_SQL[],
               COLUMN_ID[], COLUMN_UUID[], COLUMN_ROOT[], COLUMN_DEVICE_ID[],
               COLUMN_SYNC_ID[], COLUMN_STATUS[], COLUMN_LAST_FIND[],
               COLUMN_BACKUP_TYPE[], COLUMN_IS_ENABLED[], COLUMN_ROOT_STATUS[];
};

class TableConfig {
 public:
  virtual ~TableConfig() = 0;
  static const Uri URI;
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[], 
               COLUMN_NAME[], COLUMN_VALUE[];
  static const char NAME_USERNAME[], NAME_PASSWD[], NAME_DISCOVER_PORT[],
               NAME_SYNC_INTERVAL[], NAME_BACKUP_ROOT[], 
               NAME_TREE_ROOT_PREFIX[], NAME_REPORT_HOST[], NAME_CA_CERT[], NAME_MAC_TOKEN[];
};

class TableFile {
 public:
  virtual ~TableFile() = 0;
  static const Uri URI;
  static const int32_t STATUS_NORMAL, STATUS_REMOVE;
  static const char CREATE_USN_INDEX_SQL[];
  static const char PATH[], NAME[], CREATE_SQL[], DROP_SQL[], 
               COLUMN_ID[], COLUMN_TYPE[], COLUMN_STATUS[], COLUMN_MTIME[],
               COLUMN_LENGTH[], COLUMN_USN[], COLUMN_SHA1[],
               COLUMN_WIN_ATTR[], COLUMN_UNIX_ATTR[], COLUMN_ANDROID_ATTR[],
               COLUMN_LOCAL_VCLOCK[], COLUMN_REMOTE_VCLOCK[], COLUMN_PATH[],
               COLUMN_UID[], COLUMN_GID[], COLUMN_ALIAS[],
               COLUMN_MODIFIER[], COLUMN_TIME_STAMP[];
  static const Uri GenUri(const char *tree_uuid);
  static const std::string GenAuthority(const char *tree_uuid);
};

class TableDHTPeer {
 public:
  static const Uri URI;
  static const char NAME[];
  static const char INFO_HASH_STRANGER[];
  static const char COLUMN_INFO_HASH[];
  static const char COLUMN_PEER_HOST[];
  static const char COLUMN_PEER_PORT[];
  static const char COLUMN_PEER_IS_LAN[];
  static const char COLUMN_PEER_IS_IPV6[];
  static const char SCHEMA[], DROP_SQL[];
};

class TableSyncList {
 public:
  static const Uri URI;
  static const char NAME[];
  static const char COLUMN_TREE_ID[], COLUMN_PATH[];
  static const char SCHEMA[], DROP_SQL[],
               CREATE_TREE_ID_INDEX_SQL[];
};

class TableSyncMode {
 public:
  static const Uri URI;
  static const char NAME[], CREATE_SQL[], DROP_SQL[];
  static const char COLUMN_ID[], 
               COLUMN_LOCAL_TREE_ID[], COLUMN_REMOTE_TREE_ID[],
               COLUMN_SYNC_MODE[], COLUMN_SYNC_TIME[];
};

class TableShareSync {
 public:
  static const Uri URI;
  static const char NAME[], CREATE_SQL[], DROP_SQL[];
  static const char COLUMN_ID[], 
               COLUMN_SYNC_ID[], COLUMN_DEVICE_ID[], COLUMN_SYNC_PERM[];
};

class TableHistory {
 public:
  static const Uri URI;
  static const char NAME[], PATH[], CREATE_SQL[], DROP_SQL[],
                    CREATE_TIME_INDEX_SQL[], CREATE_SRCPATH_INDEX_SQL[],
                    CREATE_DSTPATH_INDEX_SQL[];
  static const char COLUMN_ID[], COLUMN_MODIFIER[], 
                    COLUMN_TREE_ID[], COLUMN_TIME_STAMP[], COLUMN_CODE[],
                    COLUMN_SRCPATH[], COLUMN_DSTPATH[], COLUMN_ERROR[]
                      , COLUMN_BACKUP_TYPE[];
};

class TablePermission {
 public:
  static const Uri URI;
  static const char NAME[], CREATE_SQL[], DROP_SQL[];
  static const char COLUMN_ID[],
               COLUMN_KEY[], COLUMN_VALUE[];
};

class TableLicences {
 public:
  static const Uri URI;
  static const char NAME[], CREATE_SQL[], DROP_SQL[];
  static const char COLUMN_NAME[], COLUMN_VALUE[];
  static const char NAME_MAC_ADDRESS[], NAME_PERM_KEY[],
               NAME_EXPIRED_TIME[], NAME_CREATED_TIME[],
               NAME_LAST_CONTACT_TIME[],
               NAME_EXPIRED_OFFLINE_TIME[], NAME_ROLE[], NAME_STATUS[],
               NAME_QRCODE[];
};

class TableMisc {
  public:
    static const Uri URI;
    static const char NAME[], PATH[], CREATE_SQL[], DROP_SQL[];
    static const char COLUMN_KEY[], COLUMN_VALUE[];
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_DATABASE_TABLE_H_
