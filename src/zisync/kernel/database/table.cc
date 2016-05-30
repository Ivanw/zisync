/* Copyright [2014] <zisync.com> */
#include <string>

#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/icore.h"

namespace zs {
const char TableDevice::PATH[] = "/Device";
const char TableDevice::NAME[] = "Device";
const char TableDevice::CREATE_SQL[] =
    "CREATE TABLE IF NOT EXISTS Device ("
    " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    " uuid        VARCHAR(40) UNIQUE NOT NULL,"
    " name        TEXT NOT NULL,"
    " route_port  INTEGER NOT NULL,"
    " data_port   INTEGER NOT NULL,"
    " status      INTEGER NOT NULL,"
    " type        INTEGER NOT NULL,"
    " is_mine     INTEGER NOT NULL,"
    " version     INTEGER NOT NULL DEFAULT 0,"
    " backup_root TEXT,"
    " backup_dst_root TEXT)";
const char TableDevice::DROP_SQL[] = "DROP TABLE IF EXISTS Device";
const Uri TableDevice::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, PATH);
const char TableDevice::CREATE_IS_MINE_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS IsMineIndex ON Device (is_mine)";
const char TableDevice::CREATE_STATUS_INDEX_SQL[] = 
    "CREATE INDEX IF NOT EXISTS StatusIndex on Device (status)";
const int32_t TableDevice::NULL_DEVICE_ID = -1;
const int32_t TableDevice::LOCAL_DEVICE_ID = 0;
const int32_t TableDevice::STATUS_ONLINE = 1;
const int32_t TableDevice::STATUS_OFFLINE = 2;
const char TableDevice::COLUMN_ID[] = "id";
const char TableDevice::COLUMN_UUID[] = "uuid";
const char TableDevice::COLUMN_NAME[] = "name";
const char TableDevice::COLUMN_ROUTE_PORT[] = "route_port";
const char TableDevice::COLUMN_DATA_PORT[] = "data_port";
const char TableDevice::COLUMN_STATUS[] = "status";
const char TableDevice::COLUMN_TYPE[] = "type";  // use platform
const char TableDevice::COLUMN_IS_MINE[] = "is_mine";
// the local dir to store backup
const char TableDevice::COLUMN_BACKUP_ROOT[] = "backup_root"; 
const char TableDevice::COLUMN_BACKUP_DST_ROOT[] = "backup_dst_root";
const char TableDevice::COLUMN_VERSION[] = "version";

const Uri TableDeviceIP::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, PATH);
const char TableDeviceIP::PATH[] = "/DeviceIP";
const char TableDeviceIP::NAME[] = "DeviceIP";
const char TableDeviceIP::CREATE_SQL[] = 
    "CREATE TABLE IF NOT EXISTS DeviceIP ("
    " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    " device_id   INTEGER NOT NULL,"
    " ip          TEXT NOT NULL,"
    " is_ipv6     INTEGER NOT NULL,"
    " earliest_no_response_time INTEGER NOT NULL,"
    " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE,"
    " UNIQUE(ip))";
const char TableDeviceIP::DROP_SQL[] = "DROP TABLE IF EXISTS DeviceIP";
const char TableDeviceIP::CREATE_EARLIEST_NO_RESP_TIME_INDEX_SQL[] = 
    "CREATE INDEX IF NOT EXISTS EarliestNoRespTimeIndex on DeviceIP "
    "(earliest_no_response_time)";
const char TableDeviceIP::CREATE_DEVICE_ID_INDEX_SQL[] = 
    "CREATE INDEX IF NOT EXISTS DeviceIdIndex on DeviceIP "
    "(device_id)";
const int64_t TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE = -1;
const char TableDeviceIP::COLUMN_ID[] = "id";
const char TableDeviceIP::COLUMN_DEVICE_ID[] = "device_id";
const char TableDeviceIP::COLUMN_IP[] = "ip";
const char TableDeviceIP::COLUMN_IS_IPV6[] = "is_ipv6";
// the earliest time that requset have not recv response, 
// if has a request recv response, reset the time to EARLIEST_NO_RESP_TIME_NONE
const char TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME[] = 
    "earliest_no_response_time";  // in S

const Uri TableStaticPeer::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, PATH);
const char TableStaticPeer::PATH[] = "/StaticPeer";
const char TableStaticPeer::NAME[] = "StaticPeer";
const char TableStaticPeer::COLUMN_IP[] = "ip";
const char TableStaticPeer::COLUMN_PORT[] = "port";
const char TableStaticPeer::CREATE_SQL[] =
    "CREATE TABLE IF NOT EXISTS StaticPeer ("
    "ip           TEXT NOT NULL,"
    "port         INTEGER NOT NULL,"
	"PRIMARY KEY(ip,port))";
	//"UNIQUE(ip))";
const char TableStaticPeer::DROP_SQL[] =
    "DROP TABLE IF EXISTS StaticPeer";
const char TableStaticPeer::CREATE_IP_INDEX_SQL[] =
    "CREATE INDEX IF NOT EXISTS IpIndex on StaticPeer(ip)";


const char TableSync::PATH[] = "/Sync";
const char TableSync::NAME[] = "Sync";
const char TableSync::CREATE_SQL[] =
    "CREATE TABLE IF NOT EXISTS Sync ("
        " id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
        " uuid               VARCHAR(40) UNIQUE NOT NULL,"
        " name               TEXT NOT NULL,"
        " last_sync          INTEGER NOT NULL,"
        " type               INTEGER NOT NULL,"
        " status             INTEGER NOT NULL,"
        " device_id          INTEGER NOT NULL DEFAULT 0,"
        " permission         INTEGER NOT NULL DEFAULT 3,"
        " restore_share_perm INTEGER NOT NULL DEFAULT -1," // DISCONNECT_RECOVERABLE
        " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE)";
const char TableSync::DROP_SQL[] = "DROP TABLE IF EXISTS Sync";
const int64_t TableSync::LAST_SYNC_NONE = -1;
const int32_t TableSync::TYPE_NORMAL = 1;
const int32_t TableSync::TYPE_BACKUP = 2;
const int32_t TableSync::TYPE_SHARED = 3;
const int32_t TableSync::STATUS_NORMAL = 1;
const int32_t TableSync::STATUS_REMOVE = 2;
const int32_t TableSync::RESTORE_SHARE_PERM_NULL = -1;
const int32_t TableSync::PERM_RDONLY = SYNC_PERM_RDONLY;
const int32_t TableSync::PERM_WRONLY = SYNC_PERM_WRONLY;
const int32_t TableSync::PERM_RDWR = SYNC_PERM_RDWR;
const int32_t TableSync::PERM_CREATOR_DELETE = 4;
const int32_t TableSync::PERM_TOKEN_DIFF = 5;
const int32_t TableSync::PERM_DISCONNECT = 6;
const Uri TableSync::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, PATH);
const char TableSync::CREATE_DEVICE_ID_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS DeviceIdIndex ON Sync (device_id)";
const char TableSync::CREATE_TYPE_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS TypeIndex ON Sync (type)";
const char TableSync::CREATE_STATUS_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS StatusIndex ON Sync (status)";
const char TableSync::CREATE_RESTORE_SHARE_PERM_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS RestoreSharePermIndex ON Sync (restore_share_perm)";
const char TableSync::COLUMN_ID[] = "id";
const char TableSync::COLUMN_UUID[] = "uuid";
const char TableSync::COLUMN_NAME[] = "name";
const char TableSync::COLUMN_LAST_SYNC[] = "last_sync";
const char TableSync::COLUMN_TYPE[] = "type";
const char TableSync::COLUMN_STATUS[] = "status";
const char TableSync::COLUMN_DEVICE_ID[] = "device_id";
const char TableSync::COLUMN_PERM[] = "permission";
const char TableSync::COLUMN_RESTORE_SHARE_PERM[] = "restore_share_perm";

const int32_t TableTree::STATUS_NORMAL = 1;
const int32_t TableTree::STATUS_REMOVE = 2;
const int32_t TableTree::STATUS_VCLOCK = 3;
const int32_t TableTree::ROOT_STATUS_NORMAL = 1;
const int32_t TableTree::ROOT_STATUS_REMOVED = 2;
const int32_t TableTree::BACKUP_NONE = 0;
const int32_t TableTree::BACKUP_SRC = 1;
const int32_t TableTree::BACKUP_DST = 2;
const char TableTree::PATH[] = "/Tree";
const char TableTree::NAME[] = "Tree";
const char TableTree::CREATE_SQL[] =
    "CREATE TABLE IF NOT EXISTS Tree ("
        " id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        " uuid         VARCHAR(40) UNIQUE NOT NULL,"
        " root         TEXT,"
        " device_id    INTEGER,"
        " sync_id      INTEGER NOT NULL,"
        " status       INTEGER NOT NULL,"
        " last_find    INTEGER NOT NULL," // in s
        " backup_type  INTEGER NOT NULL DEFAULT 0,"
        " is_enabled   INTEGER NOT NULL DEFAULT 1,"
        " root_status  INTEGER NOT NULL DEFAULT 1,"
        " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE, "
        " FOREIGN KEY(sync_id)   REFERENCES Sync(id)   ON DELETE CASCADE)";
const char TableTree::DROP_SQL[] = "DROP TABLE IF EXISTS Tree";
const char TableTree::CREATE_DEVICE_ID_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS DeviceIdIndex ON Tree (device_id)";
const char TableTree::CREATE_SYNC_ID_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS SyncIdIndex ON Tree (sync_id)";
const char TableTree::CREATE_STATUS_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS StatusIndex ON Tree (status)";
const Uri TableTree::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, PATH);
const int64_t TableTree::LAST_FIND_NONE = -1;
const char TableTree::COLUMN_ID[]          = "id";
const char TableTree::COLUMN_UUID[]        = "uuid";
const char TableTree::COLUMN_ROOT[]        = "root";
const char TableTree::COLUMN_DEVICE_ID[]   = "device_id";
const char TableTree::COLUMN_SYNC_ID[]     = "sync_id";
const char TableTree::COLUMN_STATUS[]      = "status";
const char TableTree::COLUMN_LAST_FIND[]   = "last_find";
const char TableTree::COLUMN_BACKUP_TYPE[] = "backup_type";
const char TableTree::COLUMN_IS_ENABLED[]  = "is_enabled";
const char TableTree::COLUMN_ROOT_STATUS[]   = "root_status";

const char TableConfig::PATH[] = "/Config";
const char TableConfig::NAME[] = "Config";
const char TableConfig::CREATE_SQL[] =
    "CREATE TABLE IF NOT EXISTS Config ("
        " name      TEXT PRIMARY KEY,"
        " value     TEXT)";
const char TableConfig::DROP_SQL[] = "DROP TABLE IF EXISTS Config";
const Uri TableConfig::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, PATH);
const char TableConfig::COLUMN_NAME[] = "name";
const char TableConfig::COLUMN_VALUE[] = "value";
const char TableConfig::NAME_USERNAME[] = "username";
const char TableConfig::NAME_PASSWD[] = "passwd";
const char TableConfig::NAME_DISCOVER_PORT[] = "discover_port";
const char TableConfig::NAME_SYNC_INTERVAL[] = "sync_interval";
const char TableConfig::NAME_BACKUP_ROOT[] = "backup_root";
const char TableConfig::NAME_TREE_ROOT_PREFIX[] = "tree_root_prefix";
const char TableConfig::NAME_REPORT_HOST[] = "report_host";
const char TableConfig::NAME_CA_CERT[] = "cacert";
const char TableConfig::NAME_MAC_TOKEN[] = "mac_token";

const char TableFile::PATH[] = "/File";
const char TableFile::NAME[] = "File";
const char TableFile::CREATE_SQL[] =
    "CREATE TABLE IF NOT EXISTS File ("
    " id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    " type          INTEGER NOT NULL,"
    " status        INTEGER NOT NULL,"
    " mtime         BIGINT NOT NULL, "
    " length        BIGINT NOT NULL, "
    " usn           BIGINT NOT NULL, "
    " sha1          TEXT NOT NULL,   "
    " unix_attr     INTEGER NOT NULL,"
    " android_attr  INTEGER NOT NULL,"
    " win_attr      INTEGER NOT NULL,"
    " local_vclock  INTEGER NOT NULL,"
    " remote_vclock BLOB,   "
    " path          TEXT UNIQUE NOT NULL,"
    " uid           BIGINT,"
    " gid           BIGINT,"
    " alias         TEXT,"
    " modifier      TEXT,"
    " time_stamp    BIGINT)";
const char TableFile::DROP_SQL[] = "DROP TABLE IF EXISTS File";
const char TableFile::CREATE_USN_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS UsnIndex ON FILE (usn)";
const char TableFile::COLUMN_ID[]             = "id";
const char TableFile::COLUMN_TYPE[]           = "type";
const char TableFile::COLUMN_STATUS[]         = "status";
const char TableFile::COLUMN_MTIME[]          = "mtime";
const char TableFile::COLUMN_LENGTH[]         = "length";
const char TableFile::COLUMN_USN[]            = "usn";
const char TableFile::COLUMN_SHA1[]           = "sha1";
const char TableFile::COLUMN_WIN_ATTR[]       = "win_attr";
const char TableFile::COLUMN_UNIX_ATTR[]      = "unix_attr";
const char TableFile::COLUMN_ANDROID_ATTR[]   = "android_attr";
const char TableFile::COLUMN_LOCAL_VCLOCK[]   = "local_vclock";
const char TableFile::COLUMN_REMOTE_VCLOCK[]  = "remote_vclock";
const char TableFile::COLUMN_PATH[]           = "path";
const char TableFile::COLUMN_UID[]            = "uid";
const char TableFile::COLUMN_GID[]            = "gid";
const char TableFile::COLUMN_ALIAS[]          = "alias";
const int32_t TableFile::STATUS_NORMAL = 1;
const int32_t TableFile::STATUS_REMOVE = 2;
const char TableFile::COLUMN_MODIFIER[] = "modifier";
const char TableFile::COLUMN_TIME_STAMP[]= "time_stamp";

const Uri TableFile::GenUri(const char *tree_uuid) {
  std::string authority;
  StringFormat(&authority, "%s/%s", TreeProvider::AUTHORITY, tree_uuid);
  return Uri(SCHEMA_CONTENT, authority.c_str(), PATH);
}
const std::string TableFile::GenAuthority(const char *tree_uuid) {
  std::string authority;
  StringFormat(&authority, "%s/%s", TreeProvider::AUTHORITY, tree_uuid);
  return authority;
}

const Uri TableDHTPeer::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, "/DHTPeer");
const char TableDHTPeer::NAME[] = "DHTPeer";
const char TableDHTPeer::INFO_HASH_STRANGER[] = "STRANGER";
const char TableDHTPeer::COLUMN_INFO_HASH[] = "info_hash";
const char TableDHTPeer::COLUMN_PEER_HOST[] = "peer_host";
const char TableDHTPeer::COLUMN_PEER_PORT[] = "peer_port";
const char TableDHTPeer::COLUMN_PEER_IS_LAN[] = "peer_is_lan";
const char TableDHTPeer::COLUMN_PEER_IS_IPV6[] = "peer_is_ipv6";
const char TableDHTPeer::SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS DHTPeer ("
    " info_hash     VARCHAR(40) NOT NULL,"
    " peer_host     VARCHAR(40) NOT NULL,"
    " peer_port     INTEGER NOT NULL,"
    " peer_is_lan   INTEGER NOT NULL, "
    " peer_is_ipv6  INTEGER NOT NULL, "
    " PRIMARY KEY(info_hash, peer_host, peer_port, peer_is_lan))";
const char TableDHTPeer::DROP_SQL[] = "DROP TABLE IF EXISTS DHTPeer";

const Uri TableSyncList::URI(
    SCHEMA_CONTENT, ContentProvider::AUTHORITY, "/SyncList");
const char TableSyncList::NAME[] = "SyncList";
const char TableSyncList::COLUMN_TREE_ID[] = "tree_id";
const char TableSyncList::COLUMN_PATH[] = "path";
const char TableSyncList::SCHEMA[] = 
    "CREATE TABLE IF NOT EXISTS SyncList ("
    " tree_id       INTEGER NOT NULL,"
    " path          TEXT NOT NULL,"
    " PRIMARY KEY(tree_id, path),"
    " FOREIGN KEY(tree_id) REFERENCES Tree(id) ON DELETE CASCADE) ";
const char TableSyncList::DROP_SQL[] = "DROP TABLE IF EXISTS SyncList";
const char TableSyncList::CREATE_TREE_ID_INDEX_SQL[] = 
  "CREATE INDEX IF NOT EXISTS TreeIdIndex ON SyncList (tree_id)";

const Uri TableSyncMode::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, "/SyncMode");
const char TableSyncMode::NAME[] = "SyncMode";
const char TableSyncMode::CREATE_SQL[] = 
   "CREATE TABLE IF NOT EXISTS SyncMode ("
   " id             INTEGER PRIMARY KEY AUTOINCREMENT,"
   " local_tree_id  INTEGER,"
   " remote_tree_id INTEGER,"
   " sync_mode      INTEGER,"
   " sync_time  INTEGER,"
   " FOREIGN KEY(local_tree_id) REFERENCES Tree(id) ON DELETE CASCADE,"
   " FOREIGN KEY(remote_tree_id) REFERENCES Tree(id) ON DELETE CASCADE,"
   " UNIQUE(local_tree_id, remote_tree_id))";
const char TableSyncMode::DROP_SQL[] = "DROP TABLE IF EXISTS SyncMode";
const char TableSyncMode::COLUMN_ID[] = "id";
const char TableSyncMode::COLUMN_LOCAL_TREE_ID[] = "local_tree_id";
const char TableSyncMode::COLUMN_REMOTE_TREE_ID[] = "remote_tree_id";
const char TableSyncMode::COLUMN_SYNC_MODE[] = "sync_mode";
const char TableSyncMode::COLUMN_SYNC_TIME[] = "sync_time";

const Uri TableShareSync::URI(SCHEMA_CONTENT, ContentProvider::AUTHORITY, "/ShareSync");
const char TableShareSync::NAME[] = "ShareSync";
const char TableShareSync::CREATE_SQL[] =
   "CREATE TABLE IF NOT EXISTS ShareSync ("
   " id        INTEGER PRIMARY KEY AUTOINCREMENT,"
   " sync_id   INTEGER,"
   " device_id INTEGER,"
   " sync_perm INTEGER,"
   " FOREIGN KEY(sync_id) REFERENCES Sync(id) ON DELETE CASCADE,"
   " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE,"
   " UNIQUE(sync_id, device_id))";
const char TableShareSync::DROP_SQL[] = "DROP TABLE IF EXISTS ShareSync";
const char TableShareSync::COLUMN_ID[] = "id";
const char TableShareSync::COLUMN_SYNC_ID[] = "sync_id";
const char TableShareSync::COLUMN_DEVICE_ID[] = "device_id";
const char TableShareSync::COLUMN_SYNC_PERM[] = "sync_perm";

const char TableHistory::CREATE_SQL[] = 
   "CREATE TABLE IF NOT EXISTS History("
   "id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
   "modifier             TEXT NOT NULL,"
   "tree_id              INTEGER NOT NULL,"
   "backup_type          INTEGER NOT NULL,"
   "srcpath              TEXT NOT NULL,"
   "dstpath              TEXT ,"
   "time_stamp           BIGINT NOT NULL,"
   "code                 INTEGER NOT NULL,"
   "error                INTEGER NOT NULL)";
const char TableHistory::DROP_SQL[] = "DROP TABLE IF EXISTS History";
const char TableHistory::CREATE_TIME_INDEX_SQL[] = 
   "CREATE INDEX IF NOT EXISTS TimeIndex ON History (time_stamp)";
const char TableHistory::PATH[] = "/History";
const char TableHistory::NAME[] = "History";
const Uri TableHistory::URI(SCHEMA_CONTENT, HistoryProvider::AUTHORITY, PATH);
const char TableHistory::COLUMN_ID[] = "id";
const char TableHistory::COLUMN_MODIFIER[] = "modifier";
const char TableHistory::COLUMN_TREE_ID[] = "tree_id";
const char TableHistory::COLUMN_TIME_STAMP[] = "time_stamp";
const char TableHistory::COLUMN_CODE[] = "code";
const char TableHistory::COLUMN_SRCPATH[] = "srcpath";
const char TableHistory::COLUMN_DSTPATH[] = "dstpath";
const char TableHistory::COLUMN_ERROR[] = "error";
const char TableHistory::COLUMN_BACKUP_TYPE[] = "backup_type";
// TablePermission
const Uri TablePermission::URI(
    SCHEMA_CONTENT, ContentProvider::AUTHORITY, "/Permission");
const char TablePermission::NAME[] = "Permission";
const char TablePermission::CREATE_SQL[] = 
  "CREATE TABLE IF NOT EXISTS Permission ("
  "id         INTEGER PRIMARY KEY AUTOINCREMENT,"
  "key        INTEGER,"
  "value      INTEGER)";
const char TablePermission::DROP_SQL[] = 
  "DROP TABLE IF EXISTS Permission";
const char TablePermission::COLUMN_ID[] = "id";
const char TablePermission::COLUMN_KEY[] = "key";
const char TablePermission::COLUMN_VALUE[] = "value";

// TableLicences
const Uri TableLicences::URI(
    SCHEMA_CONTENT, ContentProvider::AUTHORITY, "/Licences");
const char TableLicences::NAME[] = "Licences";
const char TableLicences::CREATE_SQL[] =
  "CREATE TABLE IF NOT EXISTS Licences ("
  "name       TEXT PRIMARY KEY,"
  "value      TEXT)";
const char DROP_SQL[] = "DROP TABLE IF EXISTS Licences";
const char TableLicences::NAME_MAC_ADDRESS[] = "mac_address";
const char TableLicences::NAME_PERM_KEY[] = "perm_key";
const char TableLicences::NAME_EXPIRED_TIME[] = "expired_time";
const char TableLicences::NAME_CREATED_TIME[] = "created_time";
const char TableLicences::NAME_LAST_CONTACT_TIME[] = "last_contact_time";
const char TableLicences::NAME_EXPIRED_OFFLINE_TIME[] = "expired_offline_time";
const char TableLicences::NAME_ROLE[] = "role";
const char TableLicences::COLUMN_NAME[] = "name";
const char TableLicences::COLUMN_VALUE[] = "value";
const char TableLicences::NAME_STATUS[] = "status";
const char TableLicences::NAME_QRCODE[] = "qrcode";

const Uri TableMisc::URI(SCHEMA_CONTENT, HistoryProvider::AUTHORITY, PATH);
const char TableMisc::NAME[] = "Misc";
const char TableMisc::PATH[] = "/Misc";
const char TableMisc::CREATE_SQL[] = 
"CREATE TABLE IF NOT EXISTS Misc("
"key TEXT NOT NULL PRIMARY KEY,"
"value TEXT NOT NULL)";
const char TableMisc::DROP_SQL[] = "DROP TABLE IF EXISTS Misc";
const char TableMisc::COLUMN_KEY[] = "key";
const char TableMisc::COLUMN_VALUE[] = "value";

}  // namespace zs
