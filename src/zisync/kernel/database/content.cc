// Copyright 2014, zisync.com

#include <algorithm>

#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/monitor/monitor.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/base64.h"
#include "zisync/kernel/permission.h"

namespace zs {

using std::unique_ptr;
using std::vector;

static bool IsBackupSrc(int32_t backup_id, int32_t device_id);

err_t DatabaseInit() {
  //  getc(stdin);
  IContentResolver* resolver = GetContentResolver();

  AbortClear();
  SyncList::Clear();
  const char *tree_projs[] = {
    TableTree::COLUMN_UUID, TableTree::COLUMN_ROOT, 
    TableTree::COLUMN_DEVICE_ID, TableTree::COLUMN_ID, 
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
          "%s = %" PRId32,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  vector<string> del_provider_uuids;
  while (tree_cursor->MoveToNext()) {
    const char *tree_uuid = tree_cursor->GetString(0);
    const char *tree_root = tree_cursor->GetString(1);
    
    del_provider_uuids.push_back(tree_uuid);
    int device_id = tree_cursor->GetInt32(2);
    if (device_id == TableDevice::LOCAL_DEVICE_ID) {
      err_t zisync_ret = Monitor::GetMonitor()->DelWatchDir(tree_root); 
      if (zisync_ret != ZISYNC_SUCCESS && 
          zisync_ret != ZISYNC_ERROR_NOT_STARTUP) {
        ZSLOG_ERROR("DelWatchDir(%s) fail : %s",
                    tree_root, zisync_strerror(zisync_ret));
        return zisync_ret;
      }
    }
    if (!resolver->DelProvider(
        TableFile::GenAuthority(tree_uuid).c_str(), true)) {
      ZSLOG_WARNING("DelProvider(%s) fail.", tree_uuid);
    }
  }
  tree_cursor.reset(NULL);
  resolver->Clear(ContentProvider::URI);
  resolver->Clear(HistoryProvider::URI);
  /* call AbortInit to re registerContentObserver */
  AbortInit();
  // resolver->Cleanup();

  /* insert device self into content*/
  std::string hostname;
  int rc = OsGetHostname(&hostname);
  assert(rc == 0);

  Config::set_device_name(hostname);
  ContentValues cv(6);
  string uuid;
  OsGenUuid(&uuid);
  Config::set_device_uuid(uuid);
  string device_name = Config::device_name();

  string backup_root;
  if (!IsMobileDevice()) {
    err_t zisync_ret = GenDeviceRootForBackup(
        Config::device_name().c_str(), &backup_root);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("CreateDeviceRootForBackup for Local Device fail: %s",
                  zisync_strerror(zisync_ret));
      return zisync_ret;
    }
    assert(backup_root.size() != 0);
    cv.Put(TableDevice::COLUMN_BACKUP_DST_ROOT, backup_root);
    cv.Put(TableDevice::COLUMN_BACKUP_ROOT, backup_root);
  }
  cv.Put(TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  cv.Put(TableDevice::COLUMN_NAME, device_name.c_str());
  cv.Put(TableDevice::COLUMN_UUID, uuid.c_str());
  cv.Put(TableDevice::COLUMN_ROUTE_PORT, Config::route_port());
  cv.Put(TableDevice::COLUMN_DATA_PORT, Config::data_port());
  cv.Put(TableDevice::COLUMN_TYPE, GetPlatform());
  cv.Put(TableDevice::COLUMN_IS_MINE, true);
  cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE);

  int32_t row_id = resolver->Insert(TableDevice::URI, &cv, zs::AOC_REPLACE);
  if (row_id != 0) {
    ZSLOG_ERROR("Insert Local Device into Database fail.");
    return ZISYNC_ERROR_CONFIG;
  }

  ContentValues null_cv(6);
  null_cv.Put(TableDevice::COLUMN_ID, TableDevice::NULL_DEVICE_ID);
  null_cv.Put(TableDevice::COLUMN_UUID, "NULL");
  null_cv.Put(TableDevice::COLUMN_NAME, "NULL");
  null_cv.Put(TableDevice::COLUMN_DATA_PORT, 0);
  null_cv.Put(TableDevice::COLUMN_ROUTE_PORT, 0);
  null_cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE);
  null_cv.Put(TableDevice::COLUMN_TYPE, PLATFORM_WINDOWS);
  null_cv.Put(TableDevice::COLUMN_IS_MINE, true);
  row_id = resolver->Insert(TableDevice::URI, &null_cv, zs::AOC_REPLACE);
  if (row_id != -1) {
    ZSLOG_ERROR("Insert Null Device into Database fail.");
    return ZISYNC_ERROR_CONFIG;
  }

  SaveBackupRoot(Config::backup_root().c_str());
  
  return ZISYNC_SUCCESS;
}

err_t SaveDiscoverPort(int32_t port) {
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_DISCOVER_PORT);
  cv.Put(TableConfig::COLUMN_VALUE, port);
  int32_t row_id = resolver->Insert(TableConfig::URI, &cv, AOC_REPLACE);
  if (row_id < 0) {
    ZSLOG_ERROR("Save Discover port to Content fail");
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

err_t SaveRoutePort(int32_t port) {
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(1);
  cv.Put(TableDevice::COLUMN_ROUTE_PORT, port);
  int affected_row_num = resolver->Update(
      TableDevice::URI, &cv, "%s = %d", 
      TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  if (affected_row_num != 1) {
    ZSLOG_ERROR("Save Route port to Content fail");
    return ZISYNC_ERROR_CONTENT;
  }
  return ZISYNC_SUCCESS;
}

err_t SaveDataPort(int32_t port) {
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(1);
  cv.Put(TableDevice::COLUMN_DATA_PORT, port);
  int affected_row_num = resolver->Update(
      TableDevice::URI, &cv, "%s = %d", 
      TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  if (affected_row_num != 1) {
    ZSLOG_ERROR("Save Data port to Content fail");
    return ZISYNC_ERROR_CONTENT;
  }
  return ZISYNC_SUCCESS;
}

err_t SaveAccount(const char *username, const char *password) {
  IContentResolver* resolver = GetContentResolver();

  ContentValues username_cv(2);
  username_cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_USERNAME);
  username_cv.Put(TableConfig::COLUMN_VALUE, username);

  string passwd_sha1;
  Sha1Hex(password, &passwd_sha1);
  ContentValues passwd_cv(2);
  passwd_cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_PASSWD);
  passwd_cv.Put(TableConfig::COLUMN_VALUE, passwd_sha1.c_str());

  ContentValues *cvs[] = { &username_cv, &passwd_cv };

  int num_affected_row = resolver->BulkInsert(
      TableConfig::URI, cvs, ARRAY_SIZE(cvs), AOC_REPLACE);
  //not reporting duplicate name, illegal character, vacant or too simple pw ?
  if (num_affected_row != 2) {
    ZSLOG_ERROR("Insert username and passwd into Provider fail. Maybe you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  Config::set_account_name(username);
  Config::set_account_passwd(password);
  return ZISYNC_SUCCESS;
}

err_t SaveBackupRoot(const char *root) {
  IContentResolver* resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_BACKUP_ROOT);
  cv.Put(TableConfig::COLUMN_VALUE, root);

  int row_id = resolver->Insert(
      TableConfig::URI, &cv, AOC_REPLACE);
  //not reporting duplicate name, illegal character, vacant or too simple pw ?
  if (row_id <= 0) {
    ZSLOG_ERROR("Insert Backup Root into Provider fail. Maybe you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  ContentValues device_cv(1);
  device_cv.Put(TableDevice::COLUMN_BACKUP_ROOT, "");
  resolver->Update(TableDevice::URI, &device_cv, NULL);

  Config::set_backup_root(root);
  return ZISYNC_SUCCESS;
}

err_t SaveTreeRootPrefix(const char *tree_root_prefix) {
  IContentResolver* resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_TREE_ROOT_PREFIX);
  cv.Put(TableConfig::COLUMN_VALUE, tree_root_prefix);

  int row_id = resolver->Insert(
      TableConfig::URI, &cv, AOC_REPLACE);
  //not reporting duplicate name, illegal character, vacant or too simple pw ?
  if (row_id <= 0) {
    ZSLOG_ERROR("Insert tree_root_prefix into Provider fail. Maybe you have "
                "not initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }

  Config::set_tree_root_prefix(tree_root_prefix);
  return ZISYNC_SUCCESS;
}

err_t SaveReportHost(const std::string &report_host) {
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_REPORT_HOST);
  cv.Put(TableConfig::COLUMN_VALUE, report_host);

  int affected_row_num = resolver->Insert(
      TableConfig::URI, &cv, AOC_REPLACE);
  if (affected_row_num <= 0) {
    ZSLOG_ERROR("Insert report_host(%s) into Provider fail. Maybe you have "
                "not initialize database.", report_host.c_str());
    return ZISYNC_ERROR_CONTENT;
  }
  Config::set_report_host(report_host);

  return ZISYNC_SUCCESS;
}

err_t SaveCAcert(const std::string &ca_cert) {
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_CA_CERT);
  cv.Put(TableConfig::COLUMN_VALUE, ca_cert);
  int num_affected_row = resolver->Insert(
      TableConfig::URI, &cv, AOC_REPLACE);
  if (num_affected_row <= 0) {
    ZSLOG_ERROR("Insert ca_cert into provider fail. May be you have "
                "not Initialized database.");
    return ZISYNC_ERROR_CONTENT;
  }
  Config::set_ca_cert(base64_decode(ca_cert));

  return ZISYNC_SUCCESS;
}

err_t SaveMacToken(const std::string &mac_token) {
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(2);
  cv.Put(TableConfig::COLUMN_NAME, TableConfig::NAME_MAC_TOKEN);
  cv.Put(TableConfig::COLUMN_VALUE, mac_token);
  int num_affected_row = resolver->Insert(
      TableConfig::URI, &cv, AOC_REPLACE);
  if (num_affected_row <= 0) {
    ZSLOG_ERROR("Insert mac_token(%s) into provider fail. May be you have "
                "not Initialized database.", mac_token.c_str());
    return ZISYNC_ERROR_CONTENT;
  }
  Config::set_mac_token(mac_token);

  return ZISYNC_SUCCESS;
}

err_t SaveDeviceName(const char *device_name) {
  IContentResolver* resolver = GetContentResolver();

  ContentValues cv(1);
  cv.Put(TableDevice::COLUMN_NAME, device_name);
  int num_affected_row = resolver->Update(
      TableDevice::URI, &cv, "%s = %d", 
      TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  if (num_affected_row != 1) {
    ZSLOG_ERROR("Call SetDeviceName() before Initialize.");;
    return ZISYNC_ERROR_CONFIG;
  }

  Config::set_device_name(device_name);
  return ZISYNC_SUCCESS;
}

#ifdef ZS_TEST
err_t RemoteTreeAdd(const string& tree_uuid, const string& device_uuid, 
                    const string& sync_uuid, TreeInfo *tree_info) {
  IContentResolver* resolver = GetContentResolver();
  /* check whether tree exists */
  const char* tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
          "%s = '%s'", TableTree::COLUMN_UUID, tree_uuid.c_str()));
  if (tree_cursor->MoveToNext()) {
    return ZISYNC_ERROR_TREE_EXIST;
  }

  const char* device_projs[] = {
    TableDevice::COLUMN_ID, TableDevice::COLUMN_NAME, 
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
          "%s = '%s'", TableDevice::COLUMN_UUID, device_uuid.c_str()));
  if (!device_cursor->MoveToNext()) {
    return ZISYNC_ERROR_DEVICE_NOENT;
  }
  // int32_t device_id = device_cursor->GetInt32(0);
  // const char *device_name = device_cursor->GetString(1);

  const char* sync_projs[] = {
    TableDevice::COLUMN_ID,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
          "%s = '%s' AND %s = %d", TableSync::COLUMN_UUID, sync_uuid.c_str(),
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  if (!sync_cursor->MoveToNext()) {
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  int32_t sync_id = sync_cursor->GetInt32(0);
  ContentValues tree_cv(5);
  tree_cv.Put(TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  tree_cv.Put(TableTree::COLUMN_SYNC_ID, sync_id);
  tree_cv.Put(TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL);
  tree_cv.Put(TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
  tree_cv.Put(TableTree::COLUMN_UUID, tree_info->tree_uuid.c_str());
  int32_t tree_id = resolver->Insert(TableTree::URI, &tree_cv, AOC_IGNORE);
  if (tree_id < 0) {
    ZSLOG_ERROR("Insert new tree fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  IContentProvider *provider = new TreeProvider(tree_uuid.c_str());
  err_t zisync_ret = provider->OnCreate(Config::database_dir().c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("TreeProvider(%s)->OnCreate(%s) fail", tree_uuid.c_str(),
                Config::database_dir().c_str());
    delete provider;
    return zisync_ret;
  }
  if (!resolver->AddProvider(provider)) {
    ZSLOG_ERROR("AddTreeProvider(%s) fail", tree_uuid.c_str());
    delete provider;
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}
#endif

void SetTreesInMsgSync(int32_t sync_id, MsgSync *sync) {
  IContentResolver *resolver = GetContentResolver();
  const char* tree_projs[] = {
    TableTree::COLUMN_ROOT, TableTree::COLUMN_UUID,
    TableTree::COLUMN_STATUS,
  };

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %" PRId32 " AND %s = %" PRId32, 
          TableTree::COLUMN_SYNC_ID, sync_id, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
  while (tree_cursor->MoveToNext()) {
    MsgTree *tree = sync->add_trees();
    assert(tree_cursor->GetInt32(2) != TableTree::STATUS_VCLOCK);
    tree->set_root(tree_cursor->GetString(0));
    tree->set_uuid(tree_cursor->GetString(1));
    tree->set_is_normal(tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL);
  }
}

static inline void AddBackupSyncModeInMsgSync(
    int32_t local_tree_id, int32_t remote_tree_id, 
    const char *remote_tree_uuid, MsgSync *sync) {
  IContentResolver *resolver = GetContentResolver();
  const char* sync_mode_projs[] = {
    TableSyncMode::COLUMN_SYNC_MODE, TableSyncMode::COLUMN_SYNC_TIME,
  };

  unique_ptr<ICursor2> sync_mode_cursor(resolver->Query(
          TableSyncMode::URI, sync_mode_projs, ARRAY_SIZE(sync_mode_projs),
          "%s = %d AND %s = %d", 
          TableSyncMode::COLUMN_LOCAL_TREE_ID, local_tree_id,
          TableSyncMode::COLUMN_REMOTE_TREE_ID, remote_tree_id));
  if (sync_mode_cursor->MoveToNext()) {
    MsgBackupSyncMode *sync_mode = sync->add_sync_mode();
    sync_mode->set_uuid(remote_tree_uuid);
    sync_mode->set_mode(
        SyncModeToMsgSyncMode(sync_mode_cursor->GetInt32(0)));
    if (sync_mode->mode() == SM_TIMER) {
      sync_mode->set_sync_time(sync_mode_cursor->GetInt32(1));
    }
  }
}

err_t SetTreesAndSyncModeInMsgSyncBackupSrc(
    int32_t sync_id, int32_t remote_device_id, MsgSync *sync) {
  IContentResolver *resolver = GetContentResolver();
  const char* tree_projs[] = {
    TableTree::COLUMN_ROOT, TableTree::COLUMN_UUID,
    TableTree::COLUMN_STATUS, TableTree::COLUMN_ID,
  };

  int32_t local_tree_id = - 1;
  unique_ptr<ICursor2> tree_cursor;
  tree_cursor.reset(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
  if (tree_cursor->MoveToNext()) {
    bool is_normal = tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL;
    if (is_normal) {
      local_tree_id = tree_cursor->GetInt32(3);
    } else {
      MsgTree *tree = sync->add_trees();
      tree->set_root(tree_cursor->GetString(0));
      tree->set_uuid(tree_cursor->GetString(1));
      tree->set_is_normal(is_normal);
    }
  } else {
    return ZISYNC_ERROR_TREE_NOENT;
  }
  
  tree_cursor.reset(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id, 
          TableTree::COLUMN_DEVICE_ID, remote_device_id));
  while (tree_cursor->MoveToNext()) {
    bool is_normal = tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL;
    if (is_normal) {
      assert (local_tree_id != -1);
      int32_t remote_tree_id = tree_cursor->GetInt32(3);
      const char *remote_tree_uuid = tree_cursor->GetString(1);
      AddBackupSyncModeInMsgSync(
          local_tree_id, remote_tree_id, remote_tree_uuid, sync);
    } else {
      MsgTree *tree = sync->add_trees();
      tree->set_root(tree_cursor->GetString(0));
      tree->set_uuid(tree_cursor->GetString(1));
      tree->set_is_normal(tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL);
    }

  }

  return ZISYNC_SUCCESS;
}

void SetTreesInMsgSyncBackupDst(
    int32_t sync_id, MsgSync *sync) {
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
          TableTree::COLUMN_STATUS, TableTree::STATUS_REMOVE));//todo: why send only removeed tree
  while (tree_cursor->MoveToNext()) {
    MsgTree *tree = sync->add_trees();
    tree->set_root(tree_cursor->GetString(0));
    tree->set_uuid(tree_cursor->GetString(1));
    tree->set_is_normal(tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL);
  }
}

void AppendCreatorInsertOperation(
    OperationList *creator_op_list, const MsgDevice &remote_creator,
	bool is_mine) {
  ContentOperation *insert = creator_op_list->NewInsert(
      TableDevice::URI, AOC_IGNORE);
  ContentValues *cv = insert->GetContentValues();
  cv->Put(TableDevice::COLUMN_UUID, remote_creator.uuid());
  cv->Put(TableDevice::COLUMN_NAME, remote_creator.name());
  cv->Put(TableDevice::COLUMN_DATA_PORT, remote_creator.data_port());
  cv->Put(TableDevice::COLUMN_ROUTE_PORT, remote_creator.route_port());
  cv->Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_OFFLINE);
  cv->Put(TableDevice::COLUMN_TYPE, 
          MsgDeviceTypeToPlatform(remote_creator.type()));
  cv->Put(TableDevice::COLUMN_IS_MINE, is_mine);
  cv->Put(TableDevice::COLUMN_VERSION, remote_creator.version());
}

void AppendSyncInsertOperation(
    OperationList* sync_op_list, const Device &local_device, 
    const MsgSync& remote_sync, bool is_share) {
  if (!remote_sync.is_normal() || 
      remote_sync.perm() == SP_DISCONNECT ||
      remote_sync.perm() == SP_CREATOR_DELETE) {
    return;
  }
  if (remote_sync.type() == ST_BACKUP && IsMobileDevice()) {
    assert(false);
    return;
  }

  // is called in ShareSyncHandler or from a not mine device;
  if (!is_share && !local_device.is_mine()) {
    return;
  }

  if (is_share && !remote_sync.has_perm()) {
    return;
  }
  
  int32_t creator_id;
  if (remote_sync.has_creator()) {
    const MsgDevice &remote_creator = remote_sync.creator();
    // TODO only get id
    unique_ptr<Device> local_creator(Device::GetBy(
            "%s = '%s'", 
            TableDevice::COLUMN_UUID, remote_creator.uuid().c_str()));
    if (!local_creator) {
      ZSLOG_ERROR("Should find Creator in database but not");
      return;
    }
    creator_id = local_creator->id();
  } else {
    creator_id = TableDevice::NULL_DEVICE_ID;
  }

  if (remote_sync.type() == ST_BACKUP) {
    ZSLOG_INFO("Insert Backup(%s)", remote_sync.uuid().c_str());
    if (creator_id == TableDevice::NULL_DEVICE_ID) { // old version
      creator_id = local_device.id();
    }
  } else {
    ZSLOG_INFO("Insert Sync(%s)", remote_sync.uuid().c_str());
  }


  ContentOperation* insert = sync_op_list->NewInsert(
      TableSync::URI, AOC_IGNORE);
  ContentValues* cv = insert->GetContentValues();
  cv->Put(TableSync::COLUMN_NAME, remote_sync.name(), true);
  cv->Put(TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL);
  cv->Put(TableSync::COLUMN_UUID, remote_sync.uuid(), true);
  cv->Put(TableSync::COLUMN_TYPE, MsgSyncTypeToTable(remote_sync.type()));
  cv->Put(TableSync::COLUMN_LAST_SYNC, TableSync::LAST_SYNC_NONE);
  cv->Put(TableSync::COLUMN_DEVICE_ID, creator_id);
  if (is_share) {
    assert(remote_sync.has_perm());
    cv->Put(TableSync::COLUMN_PERM, 
            MsgSyncPermToSyncPerm(remote_sync.perm()));
  } else {
    assert(local_device.is_mine());
    cv->Put(TableSync::COLUMN_PERM, TableSync::PERM_RDWR);
    cv->Put(TableSync::COLUMN_RESTORE_SHARE_PERM, TableSync::PERM_TOKEN_DIFF);
  }
}

// void AppendSyncUpdateOperation(
//     OperationList* sync_op_list, const Sync& local_sync, 
//     const MsgSync& remote_sync, int32_t device_id) {
//   assert(remote_sync.type() != ST_BACKUP);
//   if (device_id != local_sync.device_id()) {
//     return;
//   }
//   // is Creator
//   if (local_sync.device_id() == TableDevice::LOCAL_DEVICE_ID) {
//     return;
//   }
// 
//   ZSLOG_INFO("Update Sync(%s)", remote_sync.uuid().c_str());
// 
//   string tree_root;
//   ContentOperation* update = sync_op_list->NewUpdate(
//       TableSync::URI, " %s = '%s'", 
//       TableSync::COLUMN_UUID, remote_sync.uuid().c_str());
//   ContentValues* cv = update->GetContentValues();
// 
//   cv->Put(TableSync::COLUMN_NAME, remote_sync.name(), true);
// 
//   cv->Put(TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL);
// 
//   if (!remote_sync.is_normal()) {
//       cv->Put(TableSync::COLUMN_PERM, TableSync::PERM_DISCONNECT_UNRECOVERABLE);
//       Tree::AppendDeleteBy(
//           sync_op_list, "%s = %d", 
//           TableTree::COLUMN_SYNC_ID, local_sync.id());
//   }
// }
// 

static inline bool HasShareSyncPermChanged(
    int32_t local_sync_perm, MsgSyncPerm remote_sync_perm_) {
  int32_t remote_sync_perm = MsgSyncPermToSyncPerm(remote_sync_perm_);
  if (local_sync_perm == TableSync::PERM_TOKEN_DIFF) {
    return remote_sync_perm != TableSync::PERM_DISCONNECT;
  } else {
    return local_sync_perm != remote_sync_perm;
  }
}
void AppendNormalSyncUpdateOperation(
    OperationList* sync_op_list, const Device& local_device, 
    const Sync& local_sync, const MsgSync& remote_sync, bool is_share,
    bool device_was_mine) {

  class DisconnectShareSyncInCreatorCondition : public OperationCondition {
   public:
    DisconnectShareSyncInCreatorCondition(
        int32_t sync_id, int32_t remote_device_id) : 
        sync_id_(sync_id), remote_device_id_(remote_device_id) {}
    virtual ~DisconnectShareSyncInCreatorCondition() { /* */ }

    virtual bool Evaluate(ContentOperation *op) {
      assert(remote_device_id_ != TableDevice::LOCAL_DEVICE_ID);
      Tree::DeleteBy(
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id_, 
          TableTree::COLUMN_DEVICE_ID, remote_device_id_);
      return true;
    }

   private:
    int32_t sync_id_, remote_device_id_;
  };
  
  class DisconnectShareSyncInNotCreatorCondition : public OperationCondition {
   public:
    DisconnectShareSyncInNotCreatorCondition(int32_t sync_id) : 
        sync_id_(sync_id) {}
    virtual ~DisconnectShareSyncInNotCreatorCondition() { /* */ }

    virtual bool Evaluate(ContentOperation *op) {
      Tree::DeleteBy("%s = %d", TableTree::COLUMN_SYNC_ID, sync_id_);
      return true;
    }

   private:
    int32_t sync_id_;
  };
  
  class ShareSyncChangeFromPermRdonly : public OperationPostProcess {
   public:
    ShareSyncChangeFromPermRdonly(int32_t sync_id):sync_id_(sync_id) {}
    virtual ~ShareSyncChangeFromPermRdonly() { /* */ }

    virtual void Evaluate() {
      int32_t sync_perm = Sync::GetSyncPermByIdWhereStatusNormal(sync_id_);
      if (sync_perm == TableSync::PERM_WRONLY || sync_perm == TableSync::PERM_RDWR) {
        IssueRefreshWithSyncId(sync_id_);
      }
    }

   private:
    int32_t sync_id_;
  };

  // is Creator
  if (local_sync.device_id() == TableDevice::LOCAL_DEVICE_ID) {
    if (local_device.is_mine()) { // is my device, no need to modify ShareSync
      return;//todo: means creator never update meta(except disconnect) info from other Devices?
    }
    // only when disconnect or remove in shared need update
    if (!remote_sync.is_normal()) {
      ZSLOG_INFO("Update Sync(%s)", remote_sync.uuid().c_str());
      ContentOperation *co = sync_op_list->NewDelete(
          TableShareSync::URI, " %s = %d AND %s = %d ", 
          TableShareSync::COLUMN_DEVICE_ID, local_device.id(),
          TableShareSync::COLUMN_SYNC_ID, local_sync.id());
      co->SetCondition(new DisconnectShareSyncInCreatorCondition(
              local_sync.id(), local_device.id()), true);
    } 
    // else if (remote_sync.perm() == SP_DISCONNECT_UNRECOVERABLE) {
    //   ZSLOG_INFO("Update Sync(%s)", remote_sync.uuid().c_str());
    //   ContentOperation* update = sync_op_list->NewUpdate(
    //       TableShareSync::URI, " %s = %d AND %s = %d ", 
    //       TableShareSync::COLUMN_DEVICE_ID, local_device.id(),
    //       TableShareSync::COLUMN_SYNC_ID, local_sync.id());
    //   ContentValues* cv = update->GetContentValues();
    //   cv->Put(TableShareSync::COLUMN_SYNC_PERM, 
    //           TableSync::PERM_DISCONNECT_UNRECOVERABLE);
    //   update->SetCondition(new DisconnectShareSyncInCreatorCondition(
    //           local_sync.id(), local_device.id()), true);
    // } 
    return;
  } 

  bool sync_need_update = false;
  bool become_disconnect_unrecoverable = false;
  assert(local_sync.device_id() != TableDevice::LOCAL_DEVICE_ID);
  unique_ptr<ContentOperation> update(new UpdateOperation(
      TableSync::URI, " %s = '%s' ", 
      TableSync::COLUMN_UUID, remote_sync.uuid().c_str()));
  ContentValues* cv = update->GetContentValues();

  if (local_sync.device_id() == -1 && remote_sync.has_creator()) {
    const MsgDevice &remote_creator = remote_sync.creator();
    // TODO only get id
    unique_ptr<Device> local_creator(Device::GetBy(
            "%s = '%s'", 
            TableDevice::COLUMN_UUID, remote_creator.uuid().c_str()));
    if (!local_creator) {
      ZSLOG_ERROR("Should find Creator in database but not");
      return;
    }
    cv->Put(TableSync::COLUMN_DEVICE_ID, local_creator->id());
    sync_need_update = true;
  }

  if (remote_sync.type() == ST_SHARED && 
      local_sync.type() == TableSync::TYPE_NORMAL) {
    cv->Put(TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED);
    sync_need_update = true;
  }
  if (local_device.id() == local_sync.device_id()) { // from creator
    assert(remote_sync.has_perm());
    if (!remote_sync.is_normal()) {
      if (local_sync.perm() != TableSync::PERM_CREATOR_DELETE) {
        cv->Put(TableSync::COLUMN_PERM, TableSync::PERM_CREATOR_DELETE);
        become_disconnect_unrecoverable = true;
        sync_need_update = true;
      }
    } else { // remote is normal
      assert(local_sync.perm() != TableSync::PERM_CREATOR_DELETE);
      if (remote_sync.name() != local_sync.name()) {
        cv->Put(TableSync::COLUMN_NAME, remote_sync.name().c_str());
        sync_need_update = true;
      }

      if ((is_share || local_device.is_mine()) && !local_sync.is_normal()) {
        cv->Put(TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL);
        sync_need_update = true;
      }
      if (!local_device.is_mine()) {
        if (HasShareSyncPermChanged(local_sync.perm(), remote_sync.perm())) {
          sync_need_update = true;
          cv->Put(TableSync::COLUMN_PERM, 
                  MsgSyncPermToSyncPerm(remote_sync.perm()));
          if (remote_sync.perm() == SP_DISCONNECT) {
            become_disconnect_unrecoverable = true;
          } else if (local_sync.perm() == TableSync::PERM_RDONLY &&
                     remote_sync.perm() != SP_RDONLY) {
            update->SetPostProcess(new ShareSyncChangeFromPermRdonly(
                    local_sync.id()), true);
          } else if (local_sync.perm() == TableSync::PERM_TOKEN_DIFF) {
            // local tree has been disabled sync
            ContentOperation *co = sync_op_list->NewUpdate(
                TableTree::URI, "%s = %d AND %s = %d",
                TableTree::COLUMN_SYNC_ID, local_sync.id(), 
                TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
            co->GetContentValues()->Put(TableTree::COLUMN_IS_ENABLED, true);
          }
        }
      } else { // is mine
        if (local_sync.perm() != TableSync::PERM_RDWR
            || local_sync.restore_share_perm() != TableSync::PERM_RDWR){
          if(local_sync.restore_share_perm() == TableSync::PERM_TOKEN_DIFF) {
            assert(local_sync.perm() == TableSync::PERM_RDWR
                || local_sync.perm() == TableSync::PERM_RDONLY);
          }
          int32_t perm_update = local_sync.perm();
          int32_t perm_restore_update = local_sync.restore_share_perm();
          if (local_sync.perm() == TableSync::PERM_TOKEN_DIFF) {
            assert(local_sync.restore_share_perm() == TableSync::PERM_RDWR
                || local_sync.restore_share_perm() == TableSync::PERM_RDONLY
                || local_sync.restore_share_perm() == TableSync::PERM_WRONLY);
            perm_update = local_sync.restore_share_perm();
            perm_restore_update = TableSync::PERM_TOKEN_DIFF;
            sync_need_update = true;
          }else if(local_sync.perm() == TableSync::PERM_DISCONNECT){
            perm_update = TableSync::PERM_RDWR;
            perm_update = TableSync::PERM_TOKEN_DIFF;
            sync_need_update = true;
          }else {
            assert(local_sync.perm() == TableSync::PERM_RDONLY
                || local_sync.perm() == TableSync::PERM_RDWR
                || local_sync.perm() == TableSync::PERM_WRONLY);
            if (local_sync.restore_share_perm()
                == TableSync::RESTORE_SHARE_PERM_NULL){
              perm_update = TableSync::PERM_RDWR;
              perm_restore_update =
                local_sync.is_normal() ?
                local_sync.perm() : TableSync::PERM_TOKEN_DIFF;
              sync_need_update = true;
            }else if(!device_was_mine){
              assert(local_sync.restore_share_perm() == TableSync::PERM_RDWR
                  || local_sync.restore_share_perm() == TableSync::PERM_RDONLY
                  || local_sync.restore_share_perm() == TableSync::PERM_WRONLY);
              perm_update = local_sync.restore_share_perm();
              perm_restore_update = local_sync.perm();
              sync_need_update = true;
            }
          }

          if (sync_need_update) {
            cv->Put(TableSync::COLUMN_PERM, perm_update);
            cv->Put(TableSync::COLUMN_RESTORE_SHARE_PERM, perm_restore_update);
          }

          if (local_sync.perm() == TableSync::PERM_RDONLY) {
            IssueRefreshWithSyncId(local_sync.id());
          } else if (local_sync.perm() == TableSync::PERM_TOKEN_DIFF ||
              !device_was_mine) {
            ContentOperation *co = sync_op_list->NewUpdate(
                TableTree::URI, "%s = %d AND %s = %d",
                TableTree::COLUMN_SYNC_ID, local_sync.id(), 
                TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
            co->GetContentValues()->Put(TableTree::COLUMN_IS_ENABLED, true);
          }
        }
      }
    }
  } else { // not from creator
    // has_perm is used to imcompatiable old version
    if (remote_sync.has_perm()) {
      int32_t remote_sync_perm = MsgSyncPermToSyncPerm(remote_sync.perm());
      // only spread SYNC_DEL
      if (remote_sync_perm == TableSync::PERM_CREATOR_DELETE &&
          local_sync.perm() != TableSync::PERM_CREATOR_DELETE) {
        cv->Put(TableSync::COLUMN_PERM, TableSync::PERM_CREATOR_DELETE);
        become_disconnect_unrecoverable = true;
        sync_need_update = true;
      }
    }
  }
  // Not Creator, and the msg comes from Creator
  ZSLOG_INFO("Update Sync(%s)", remote_sync.uuid().c_str());

  if (become_disconnect_unrecoverable) {
    assert(local_sync.perm() != TableSync::PERM_CREATOR_DELETE);
    assert(local_sync.device_id() != TableDevice::LOCAL_DEVICE_ID);
    update->SetCondition(
        new DisconnectShareSyncInNotCreatorCondition(local_sync.id()), true);
  }
  if (sync_need_update) {
    sync_op_list->AddOperation(update.release());
  }
} 

void AppendBackupUpdateOpertion(
    OperationList* sync_op_list, const Sync &local_sync, 
    const MsgSync& remote_sync) {
  assert(remote_sync.type() == ST_BACKUP);
  if (local_sync.device_id() != TableDevice::LOCAL_DEVICE_ID) {
    return;
  }

  if ((remote_sync.name() == remote_sync.name()) &&
      (remote_sync.is_normal() == local_sync.is_normal()) &&
      (MsgSyncTypeToTable(remote_sync.type()) == local_sync.type())) {
    return;
  }

  ZSLOG_INFO("Update Backup(%s)", remote_sync.uuid().c_str());

  int32_t status = remote_sync.is_normal() ? 
      TableSync::STATUS_NORMAL : TableSync::STATUS_REMOVE;
  ContentOperation* update = sync_op_list->NewUpdate(
      TableSync::URI,
      " %s = '%s' ", TableSync::COLUMN_UUID, remote_sync.uuid().c_str());
  ContentValues* cv = update->GetContentValues();

  cv->Put(TableSync::COLUMN_NAME, remote_sync.name(), true);
  cv->Put(TableSync::COLUMN_STATUS, status);
  cv->Put(TableSync::COLUMN_TYPE, MsgSyncTypeToTable(remote_sync.type()));

  class RemoveBackupCondition : public OperationCondition {
   public:
    RemoveBackupCondition(int32_t sync_id) : sync_id_(sync_id) {}
    virtual ~RemoveBackupCondition() { /* */ }

    virtual bool Evaluate(ContentOperation *op) {
      Tree::DeleteBy(
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id_, 
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);

      return true;
    }

    int32_t sync_id_;
  };

  if (!remote_sync.is_normal()) {
    update->SetCondition(new  RemoveBackupCondition(local_sync.id()), true);
  }
}

static inline void AppendSyncModeInsertOperations(//todo: what is this
    OperationList *op_list, const Sync &local_sync, 
    const MsgSync &remote_sync) {
  if (remote_sync.sync_mode_size() == 0) {
    return;
  }
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
    };
    unique_ptr<ICursor2> tree_cursor;
    tree_cursor.reset(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d AND %s = %d AND %s != %d",
            TableTree::COLUMN_SYNC_ID, local_sync.id(), 
            TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_SRC,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
    if (!tree_cursor->MoveToNext()) {
      return;
    }
    int32_t remote_tree_id = tree_cursor->GetInt32(0);
    for (int j = 0; j < remote_sync.sync_mode_size(); j ++) {
      const MsgBackupSyncMode &sync_mode = remote_sync.sync_mode(j);
      tree_cursor.reset(resolver->Query(
              TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
              "%s = '%s' AND %s = %d AND %s = %d AND %s = %d",
              TableTree::COLUMN_UUID, sync_mode.uuid().c_str(),
              TableTree::COLUMN_SYNC_ID, local_sync.id(), 
              TableTree::COLUMN_BACKUP_TYPE, TableTree::BACKUP_DST,
              TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
      if (!tree_cursor->MoveToNext()) {
        continue;
      }
      int32_t local_tree_id = tree_cursor->GetInt32(0);
      int local_sync_mode;
      int32_t local_sync_time;
      int32_t remote_sync_mode = MsgSyncModeToSyncMode(sync_mode.mode());
      int32_t remote_sync_time = sync_mode.has_sync_time() ? 
          sync_mode.sync_time() : -1;
      GetSyncMode(local_tree_id, TableTree::BACKUP_SRC, remote_tree_id, 
                  &local_sync_mode, &local_sync_time);
      if (local_sync_mode != remote_sync_mode || (
              local_sync_mode == SYNC_MODE_TIMER && 
              local_sync_time != remote_sync_time)) {
        ContentOperation *cp = op_list->NewInsert(
            TableSyncMode::URI, AOC_REPLACE);
        ContentValues *cv = cp->GetContentValues();
        cv->Put(TableSyncMode::COLUMN_LOCAL_TREE_ID, local_tree_id);
        cv->Put(TableSyncMode::COLUMN_REMOTE_TREE_ID, remote_tree_id);
        cv->Put(TableSyncMode::COLUMN_SYNC_MODE, remote_sync_mode);
        cv->Put(TableSyncMode::COLUMN_SYNC_TIME, remote_sync_time);
      }
    }
}

/*  if fail return -1, if success return device_id 
 *  if has no host , means that this by push*/
int32_t StoreDeviceIntoDatabase(
    const MsgDevice &device, const char *host, bool is_mine, bool is_ipv6, 
    bool is_share) {
  IContentResolver *resolver = GetContentResolver();
  int32_t device_id;
  bool device_was_mine = true;

  ZSLOG_INFO("Store Device(%s)", device.uuid().c_str());

  unique_ptr<Device> local_device(Device::GetBy(
          "%s = '%s'", 
          TableDevice::COLUMN_UUID, device.uuid().c_str()));
  ContentValues cv(6);
  cv.Put(TableDevice::COLUMN_NAME, device.name());
  cv.Put(TableDevice::COLUMN_TYPE, MsgDeviceTypeToPlatform(device.type()));
  cv.Put(TableDevice::COLUMN_DATA_PORT, device.data_port());
  cv.Put(TableDevice::COLUMN_ROUTE_PORT, device.route_port());
  cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE);
  if (device.has_backup_root()) {
    cv.Put(TableDevice::COLUMN_BACKUP_DST_ROOT, device.backup_root());
  }
  if (device.has_version()) {
    cv.Put(TableDevice::COLUMN_VERSION, device.version());
  } else {
    cv.Put(TableDevice::COLUMN_VERSION, 0);
  }
  if (local_device) {
    device_id = local_device->id();
    device_was_mine = local_device->is_mine();
    if (device_id == TableDevice::LOCAL_DEVICE_ID) {
      ZSLOG_INFO("Update local_uuid is not allowed");
      return device_id;
    }
    OperationList device_op_list;
    bool need_update = (!local_device->is_online()) || 
        local_device->HasChanged(device);
    if (!local_device->is_mine() && is_mine) {
      need_update = true;
      cv.Put(TableDevice::COLUMN_IS_MINE, true);
    }

    if (need_update) {
      resolver->Update(
          TableDevice::URI, &cv, "%s = %d", TableDevice::COLUMN_ID, device_id);
    } 
  } else {
    cv.Put(TableDevice::COLUMN_IS_MINE, is_mine);
    cv.Put(TableDevice::COLUMN_UUID, device.uuid().c_str());
    device_id = resolver->Insert(TableDevice::URI, &cv, AOC_IGNORE); 
    if (device_id == -1) {
      ZSLOG_ERROR("Insert new device(%s) fail", device.uuid().c_str());
      return -1;
    }
  }

  if (host != NULL) {
    ContentValues device_ip_cv(3);
    device_ip_cv.Put(TableDeviceIP::COLUMN_DEVICE_ID, device_id);
    device_ip_cv.Put(TableDeviceIP::COLUMN_IS_IPV6,is_ipv6);
    device_ip_cv.Put(TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME, 
                     TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE);
    device_ip_cv.Put(TableDeviceIP::COLUMN_IP, host);
    resolver->Insert(TableDeviceIP::URI, &device_ip_cv, AOC_REPLACE);
  }

  local_device.reset(Device::GetBy(
          "%s = '%s'", 
          TableDevice::COLUMN_UUID, device.uuid().c_str()));
  if (!local_device) {
    ZSLOG_ERROR("this should not happen.");
    return -1;
  }

  OperationList creator_op_list;
  for (int i = 0; i < device.syncs_size(); i ++) {
    const MsgSync& remote_sync = device.syncs(i);
    if (remote_sync.has_creator()) {
      const MsgDevice &remote_creator = remote_sync.creator();
      unique_ptr<Device> local_creator(Device::GetBy(
              "%s = '%s'", 
              TableDevice::COLUMN_UUID, remote_creator.uuid().c_str()));
      if (!local_creator) {
        assert(!is_share);
        if (local_device->is_mine()) {
          AppendCreatorInsertOperation(
			  &creator_op_list, remote_creator, is_mine);
        }
      } // no need to update
    }
  }
  if (creator_op_list.GetCount() > 0) {
    resolver->ApplyBatch(ContentProvider::AUTHORITY, &creator_op_list);
  }

  OperationList sync_op_list;
  for (int i = 0; i < device.syncs_size(); i ++) {
    const MsgSync& remote_sync = device.syncs(i);
    unique_ptr<Sync> local_sync(Sync::GetByUuid(remote_sync.uuid()));
    if (!local_sync) {
      AppendSyncInsertOperation(
          &sync_op_list, *local_device, remote_sync, is_share);
    } else {
      if (remote_sync.type() == ST_SHARED || 
          remote_sync.type() == ST_NORMAL) {
          AppendNormalSyncUpdateOperation(
              &sync_op_list, *local_device, *local_sync, remote_sync, 
              is_share, device_was_mine);
      } else {
        // ST_BACKUP
        assert(remote_sync.type() == ST_BACKUP);
        // Only update the backup target
        AppendBackupUpdateOpertion(
            &sync_op_list, *local_sync, remote_sync);
      }
    }
    // StoreSyncIntoDatabase(
    //     device_id, device.syncs(i), ignore_deleted_sync);
  }
  if (sync_op_list.GetCount() > 0) {
    resolver->ApplyBatch(ContentProvider::AUTHORITY, &sync_op_list);
  }

  OperationList tree_op_list;
  OperationList sync_mode_op_list;
  for (int i = 0; i < device.syncs_size(); i ++) {
    const MsgSync& remote_sync = device.syncs(i);
    unique_ptr<Sync> local_sync(Sync::GetByUuid(remote_sync.uuid()));
    // assert(local_sync || !remote_sync.is_normal());
    if (!local_sync) { continue; }
    const int32_t& sync_id = local_sync->id();
        
    int32_t tree_backup_type = TableTree::BACKUP_NONE;
    if (local_sync->type() == TableSync::TYPE_BACKUP) {
      if (IsBackupSrc(sync_id, device_id)) {
        tree_backup_type = TableTree::BACKUP_SRC;
      } else {
        tree_backup_type = TableTree::BACKUP_DST;
      }
    }
    for (int j = 0; j < remote_sync.trees_size(); j++) {
      const MsgTree &remote_tree = remote_sync.trees(j);
      unique_ptr<Tree> local_tree(Tree::GetByUuid(remote_tree.uuid()));
      if (local_tree == NULL) {
        if (remote_tree.is_normal()) {
          Tree::AppendTreeInsertOpertion(
              &tree_op_list, remote_tree, sync_id, device_id, 
              tree_backup_type);
        }
      } else {
        if (local_tree->HasChanged(remote_tree, device_id)) {
          // for ShareSync from not mine, in Creator, if has set disconnect the device, 
          // not store update an tree from the device
          if (local_sync->device_id() == TableDevice::LOCAL_DEVICE_ID &&
              local_sync->type() == TableSync::TYPE_SHARED && !is_mine) {
            int32_t sync_perm;
            err_t zisync_ret = GetShareSyncPerm(
                device_id, local_sync->id(), &sync_perm);
            if (zisync_ret != ZISYNC_SUCCESS || 
                sync_perm == TableSync::PERM_DISCONNECT) {
              continue;
            }
          }

          // for local tree , only can remove backup_target 
          // can be change
          if (local_tree->IsLocalTree() && (
                  local_tree->type() != TableTree::BACKUP_DST ||
                  remote_tree.is_normal())) {
            ZSLOG_WARNING("For Local tree, the only handle is remove "
                          "backup target");
            continue;
          }

          // not store any tree
          if (local_sync->perm() == TableSync::PERM_DISCONNECT ||
              local_sync->perm() == TableSync::PERM_TOKEN_DIFF ||
              local_sync->perm() == TableSync::PERM_CREATOR_DELETE) {
            continue;
          }

          local_tree->AppendTreeUpdateOperation(
              &tree_op_list, remote_tree, device_id, tree_backup_type);
        }
      }
    }
    AppendSyncModeInsertOperations(
        &sync_mode_op_list, *local_sync, remote_sync);
  }
  if (tree_op_list.GetCount() > 0) {
    resolver->ApplyBatch(ContentProvider::AUTHORITY, &tree_op_list);
  }
  if (sync_mode_op_list.GetCount() > 0) {
    resolver->ApplyBatch(ContentProvider::AUTHORITY, &sync_mode_op_list);
  }

  return device_id;
}



/*  if fail return -1, if success return sync_id 
 *  not change the status of the sync*/
// int32_t StoreSyncIntoDatabase(
//     int32_t device_id, const MsgSync &sync, bool ignore_deleted) {
//   assert(device_id != -1);
//   IContentResolver *resolver = GetContentResolver();
//   int32_t sync_id = -1;
// 
//   ZSLOG_INFO("Store Sync(%s)", sync.uuid().c_str());
//   const char *sync_projs[] = {
//     TableSync::COLUMN_ID, TableSync::COLUMN_STATUS,
//     TableSync::COLUMN_DEVICE_ID,
//   };
//   unique_ptr<ICursor2> sync_cursor(resolver->Query(
//           TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
//           "%s = '%s'", TableSync::COLUMN_UUID, sync.uuid().c_str()));
//   if (sync_cursor->MoveToNext()) { // exist
//     sync_id = sync_cursor->GetInt32(0);
//     int32_t sync_status = sync_cursor->GetInt32(1);
//     if (sync.type() == ST_BACKUP) {
//       int32_t src_device_id = sync_cursor->GetInt32(2);
//       if (src_device_id != TableDevice::LOCAL_DEVICE_ID) { // the backup is target
//         sync_id = UpdateBackupInDatabase(
//             sync, device_id, sync_id, sync_status);
//       }
//     } else {
//       sync_id = UpdateSyncInDatabase(
//           sync, sync_id, sync_status, ignore_deleted);
//     }
//   } else {
//     sync_id = InsertSyncIntoDatabase(device_id, sync);
//   }
//   sync_cursor.reset(NULL);
// 
//   if (sync_id != -1) {
//     int32_t tree_backup_type = -1;
//     if (sync.type() != ST_BACKUP) {
//       tree_backup_type = TableTree::BACKUP_NONE;
//     } else {
//       if (IsBackupDst(sync_id, device_id)) {
//         tree_backup_type = TableTree::BACKUP_DST;
//       } else if (IsBackupSrc(sync_id, device_id)) {
//         tree_backup_type = TableTree::BACKUP_SRC;
//       }
//     }
//     if (tree_backup_type != -1) {
//       for (int i = 0; i < sync.trees_size(); i ++) {
//         StoreTreeIntoDatabase(
//             device_id, sync_id, sync.trees(i), tree_backup_type);
//       }
//     }
//   }
//   return sync_id;
// }
// 
// int32_t StoreTreeIntoDatabase(
//     int32_t device_id, int32_t sync_id, const MsgTree &tree, 
//     int32_t tree_backup_type) {
//   IContentResolver *resolver = GetContentResolver();
//   ZSLOG_INFO("Store Tree(%s)", tree.uuid().c_str());
// 
//   assert(device_id != TableDevice::LOCAL_DEVICE_ID);
//   bool need_add_provider = false;
//   int32_t tree_id = -1;
//   const char *tree_projs[] = {
//     TableTree::COLUMN_ID, TableTree::COLUMN_STATUS,
//   };
//   unique_ptr<ICursor2> tree_cursor(resolver->Query(
//           TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
//           "%s = '%s'", TableTree::COLUMN_UUID, tree.uuid().c_str()));
//   ContentValues cv(4);
//   cv.Put(TableTree::COLUMN_STATUS, tree.is_normal() ? 
//          TableTree::STATUS_NORMAL : TableTree::STATUS_REMOVE);
//   cv.Put(TableTree::COLUMN_SYNC_ID, sync_id);
//   cv.Put(TableTree::COLUMN_DEVICE_ID, device_id);
//   cv.Put(TableTree::COLUMN_ROOT, tree.root());
//   cv.Put(TableTree::COLUMN_BACKUP_TYPE, tree_backup_type);
//   if (tree_cursor->MoveToNext()) {
//     int32_t tree_status = tree_cursor->GetInt32(1);
//     tree_id = tree_cursor->GetInt32(0);
//     if (tree.is_normal() && tree_status != TableTree::STATUS_NORMAL) {
//       need_add_provider = true;
//     } else if (!tree.is_normal() && tree_status == TableTree::STATUS_NORMAL) {
//       if (!resolver->DelProvider(
//               TableFile::GenAuthority(tree.uuid().c_str()).c_str(), true)) {
//         ZSLOG_ERROR("DelTreeProvider(%s) fail", tree.uuid().c_str());
//         return ZISYNC_ERROR_CONTENT;;
//       }
//     }
//     resolver->Update(
//         TableTree::URI, &cv, "%s = %d", TableTree::COLUMN_ID, tree_id);
//   } else {
//     if (tree.is_normal()) {
//       need_add_provider = true;
//       cv.Put(TableTree::COLUMN_UUID, tree.uuid());
//       cv.Put(TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
//       int32_t row_id = resolver->Insert(TableTree::URI, &cv, AOC_IGNORE);
//       tree_id = row_id <= 0 ? -1 : row_id;
//     }
//   }
//   if (tree_id != -1 && need_add_provider) {
//     IContentProvider *provider = new TreeProvider(tree.uuid().c_str());
//     err_t zisync_ret = provider->OnCreate(Config::database_dir().c_str());
//     if (zisync_ret != ZISYNC_SUCCESS) {
//       ZSLOG_ERROR("OnCreate Provider(%s/%s) fail : %s", 
//                   Config::database_dir().c_str(), tree.uuid().c_str(),
//                   zisync_strerror(zisync_ret));
//       delete provider;
//       return -1;
//     }
//     if (!resolver->AddProvider(provider)) {
//       ZSLOG_ERROR("AddTreeProvider(%s) fail", tree.uuid().c_str());
//       delete provider;
//       return -1;
//     }
//     // need add provider means a new tree
//     // zs::AbortAddSyncRemoteTree(tree_id);
//     // IssueSyncWithRemoteTree(sync_id, tree_id);
//   }
//   return tree_id;
// }

void AddFileStatIntoOpList(const Uri &file_uri, const FileStat &file_stat,
                           OperationList *op_list) {
  IContentResolver *resolver = GetContentResolver();
  const char *file_projs[] = {
    TableFile::COLUMN_ID, TableFile::COLUMN_USN,
  };
  unique_ptr<ICursor2> file_cursor(resolver->Query(
          file_uri, file_projs, ARRAY_SIZE(file_projs), 
          "%s = '%s'", TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(file_stat.path()).c_str()));
  ContentOperation *cp;
  if (file_cursor->MoveToNext()) {
    cp = op_list->NewUpdate(
        file_uri, "%s = %" PRId32 " AND %s = %" PRId64,
        TableFile::COLUMN_ID, file_cursor->GetInt32(0), 
        TableFile::COLUMN_USN, file_cursor->GetInt64(1));
  } else {
    cp = op_list->NewInsert(file_uri, AOC_IGNORE);
    ContentValues *cv = cp->GetContentValues();
    cv->Put(TableFile::COLUMN_PATH, file_stat.path(), true);
  }

  ContentValues *cv = cp->GetContentValues();
  cv->Put(TableFile::COLUMN_TYPE, file_stat.type);
  cv->Put(TableFile::COLUMN_STATUS, file_stat.status);
  cv->Put(TableFile::COLUMN_MTIME, file_stat.mtime);
  cv->Put(TableFile::COLUMN_LENGTH, file_stat.length);
  cv->Put(TableFile::COLUMN_USN, file_stat.usn); 
  // copy sha1
  cv->Put(TableFile::COLUMN_SHA1, file_stat.sha1.c_str(), true);
  cv->Put(TableFile::COLUMN_UNIX_ATTR, file_stat.unix_attr);
  cv->Put(TableFile::COLUMN_WIN_ATTR, file_stat.win_attr);
  cv->Put(TableFile::COLUMN_ANDROID_ATTR, file_stat.android_attr);
  cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, file_stat.local_vclock);
  if (file_stat.vclock.length() > 1) {
    cv->Put(TableFile::COLUMN_REMOTE_VCLOCK, file_stat.vclock.remote_vclock(), 
            file_stat.vclock.remote_vclock_size(), true);
  }

  cv->Put(TableFile::COLUMN_MODIFIER, file_stat.modifier, true);
  cv->Put(TableFile::COLUMN_TIME_STAMP, file_stat.time_stamp);
}

err_t StoreRemoteMeta(
    int32_t sync_id, const char *remote_tree_uuid, 
    const MsgRemoteMeta &remote_meta) {
  IContentResolver *resolver = GetContentResolver();
  /*  get local tree uuids */
  const char* tree_projs[] = {
    TableTree::COLUMN_UUID, 
  };
  vector<string> local_tree_uuids;
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
          "%s = %" PRId32 " AND %s != '%s'", 
          TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_UUID, remote_tree_uuid));
  local_tree_uuids.push_back(remote_tree_uuid);
  while (cursor->MoveToNext()) {
    const char *tree_uuid = cursor->GetString(0);
    local_tree_uuids.push_back(tree_uuid);
  }

  vector<string> new_tree_uuids;
  vector<int> vclock_remote_map_to_local(
      remote_meta.uuids_size(), -1);
  for (int i = 0; i < remote_meta.uuids_size(); i ++) {
    auto find = std::find(local_tree_uuids.begin(), local_tree_uuids.end(), 
                          remote_meta.uuids(i));
    if (find != local_tree_uuids.end()) {
      vclock_remote_map_to_local.at(i) = std::distance(
          local_tree_uuids.begin(), find);
    } else {
      vclock_remote_map_to_local.at(i) = 
          local_tree_uuids.size() + new_tree_uuids.size();
      new_tree_uuids.push_back(remote_meta.uuids(i));
    }
  }

  OperationList op_list;
  for (auto iter = new_tree_uuids.begin(); iter != new_tree_uuids.end();
       iter ++) {
    ContentOperation *cp = op_list.NewInsert(TableTree::URI, AOC_ABORT);
    ContentValues *cv = cp->GetContentValues();
    cv->Put(TableTree::COLUMN_UUID, iter->c_str());
    cv->Put(TableTree::COLUMN_STATUS, TableTree::STATUS_VCLOCK);
    cv->Put(TableTree::COLUMN_SYNC_ID, sync_id);
    cv->Put(TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
  }
  int affected_row_num = resolver->ApplyBatch(ContentProvider::AUTHORITY, 
                                              &op_list);
  if (affected_row_num != static_cast<int>(new_tree_uuids.size())) {
    ZSLOG_ERROR("Insert new uuids fail.");
    return ZISYNC_ERROR_CONTENT;
  }

  op_list.Clear();
  const int limit = 5000;
  const Uri &file_uri = TableFile::GenUri(remote_tree_uuid);
  const string &authority = TableFile::GenAuthority(remote_tree_uuid);
  size_t vclock_len = local_tree_uuids.size() + new_tree_uuids.size();
  for (int i = 0; i < remote_meta.stats_size(); i ++) {
    unique_ptr<FileStat> file_stat(new FileStat(
            remote_meta.stats(i), vclock_remote_map_to_local, vclock_len));
    AddFileStatIntoOpList(file_uri, *file_stat, &op_list);
    if (op_list.GetCount() > limit || i == remote_meta.stats_size() - 1) {
      int affected_row_num = resolver->ApplyBatch(authority.c_str(), &op_list);
      if (affected_row_num != op_list.GetCount()) {
        ZSLOG_ERROR("Insert some rmeote file stat fail.");
      }
      op_list.Clear();
    }

  }
  return ZISYNC_SUCCESS;
}

void SetDeviceMetaInMsgDevice(MsgDevice *device) {
  device->set_name(Config::device_name());
  device->set_uuid(Config::device_uuid());
  device->set_data_port(Config::data_port());
  device->set_route_port(Config::route_port());
  device->set_type(PlatformToMsgDeviceType(GetPlatform()));
  device->set_version(Config::version());
}

void SetBackupRootInMsgDevice(int32_t remote_device_id, MsgDevice *device) {
  if (!IsMobileDevice()) {
    device->set_backup_root(Config::backup_root());
  }
}

void SetMsgDeviceForMyDevice(MsgDevice *device) {
  IContentResolver *resolver = GetContentResolver();

  SetDeviceMetaInMsgDevice(device);
  /* @TODO maybe we can use view or join */
  const char* sync_projs[] = {
    TableSync::COLUMN_ID, TableSync::COLUMN_NAME, 
    TableSync::COLUMN_UUID, TableSync::COLUMN_TYPE,
    TableSync::COLUMN_STATUS, TableSync::COLUMN_DEVICE_ID,
    TableSync::COLUMN_PERM,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
          "%s != %d AND %s != %d AND %s != %d",
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP,
          TableSync::COLUMN_PERM, TableSync::PERM_TOKEN_DIFF,
          TableSync::COLUMN_PERM, TableSync::PERM_DISCONNECT));
  while (sync_cursor->MoveToNext()) {
    int32_t creator_id = sync_cursor->GetInt32(5);
    unique_ptr<Device> creator(Device::GetById(creator_id));
    // shared device not spread
    if (!creator) {
      continue;
    }

    MsgSync *sync = device->add_syncs();
    if (!creator->is_mine()) {
      // the sync is shared or the device change from token same to 
      // diff , so we set the sync as !is_normal(), so that the sync will not
      // spead to my other device.
      // We can not ignore the Sync, because the device may need to spead the
      // remove the local tree fo the sync
      sync->set_is_normal(false);
    } else {
      sync->set_is_normal(sync_cursor->GetInt32(4) == TableSync::STATUS_NORMAL);
    }
    sync->set_type(TableSyncTypeToMsg(sync_cursor->GetInt32(3)));
    sync->set_name(sync_cursor->GetString(1));
    sync->set_uuid(sync_cursor->GetString(2));
    if (creator_id != TableDevice::NULL_DEVICE_ID) {
      MsgDevice *msg_creator = sync->mutable_creator();
      creator->ToMsgDevice(msg_creator);
    }
    sync->set_perm(SyncPermToMsgSyncPerm(sync_cursor->GetInt32(6)));
    int32_t sync_id = sync_cursor->GetInt32(0);
    SetTreesInMsgSync(sync_id, sync);
  }
}

void AddBackupInMsgDevice(
    MsgDevice *device, int32_t remote_device_id) {
  IContentResolver *resolver = GetContentResolver();
  const char* sync_projs[] = {
    TableSync::COLUMN_ID, TableSync::COLUMN_NAME, 
    TableSync::COLUMN_UUID, TableSync::COLUMN_TYPE,
    TableSync::COLUMN_STATUS, TableSync::COLUMN_DEVICE_ID,
  };

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %d", 
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP));
  while(sync_cursor->MoveToNext()) {
    MsgSync *sync = device->add_syncs();
    int32_t backup_id = sync_cursor->GetInt32(0);
    int32_t device_id = sync_cursor->GetInt32(5);
    sync->set_type(TableSyncTypeToMsg(sync_cursor->GetInt32(3)));
    sync->set_name(sync_cursor->GetString(1));
    sync->set_uuid(sync_cursor->GetString(2));
    sync->set_is_normal(
        sync_cursor->GetInt32(4) == TableSync::STATUS_NORMAL);
    
    if (device_id == TableDevice::LOCAL_DEVICE_ID) {
      SetTreesAndSyncModeInMsgSyncBackupSrc(backup_id, remote_device_id, sync);
    } else {
      SetTreesInMsgSyncBackupDst(backup_id, sync);
    }
  }
}

static inline void AddShareSyncInMsgDeviceForCreater(
    MsgDevice *device, int32_t remote_device_id) {
  IContentResolver *resolver = GetContentResolver();
  /*  for in Sync Creater */
  const char *share_sync_projs[] = {
    TableShareSync::COLUMN_SYNC_ID, TableShareSync::COLUMN_SYNC_PERM,
  };
  unique_ptr<ICursor2> share_sync_cursor(resolver->Query(
          TableShareSync::URI, share_sync_projs, ARRAY_SIZE(share_sync_projs),
          "%s = %d", TableShareSync::COLUMN_DEVICE_ID, remote_device_id));
  while (share_sync_cursor->MoveToNext()) {
    int32_t sync_id = share_sync_cursor->GetInt32(0);
    int32_t sync_perm = share_sync_cursor->GetInt32(1);
    assert(sync_perm != TableSync::PERM_TOKEN_DIFF);

    unique_ptr<Sync> local_sync(Sync::GetBy(
            "%s = %d AND %s = %d", TableSync::COLUMN_ID, sync_id, 
            TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
    if (!local_sync) {
      continue;
    } 
    assert(local_sync->type() == TableSync::TYPE_SHARED);
    MsgSync *sync = device->add_syncs();
    local_sync->ToMsgSync(sync);
    sync->set_perm(SyncPermToMsgSyncPerm(sync_perm));
    SetTreesInMsgSync(sync_id, sync);
  }
}

static inline void AddShareSyncInMsgDeviceForNotCreater(
    MsgDevice *device, int32_t remote_device_id) {
  IContentResolver *resolver = GetContentResolver();
  /*  for in Not Sync Creater */
  const char* sync_projs[] = {
    TableSync::COLUMN_ID, TableSync::COLUMN_NAME, 
    TableSync::COLUMN_UUID, TableSync::COLUMN_TYPE,
    TableSync::COLUMN_STATUS, TableSync::COLUMN_PERM,
  };

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %d AND %s = %d ",
          TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED,
          TableSync::COLUMN_DEVICE_ID, remote_device_id));
  while(sync_cursor->MoveToNext()) {
    int32_t sync_perm = sync_cursor->GetInt32(5);
    MsgSync *sync = device->add_syncs();
    int32_t backup_id = sync_cursor->GetInt32(0);
    sync->set_type(TableSyncTypeToMsg(sync_cursor->GetInt32(3)));
    sync->set_name(sync_cursor->GetString(1));
    sync->set_uuid(sync_cursor->GetString(2));
    sync->set_is_normal(
        sync_cursor->GetInt32(4) == TableSync::STATUS_NORMAL);
    // only set sync_perm when send to Creator
    sync->set_perm(SyncPermToMsgSyncPerm(sync_perm));
    SetTreesInMsgSync(backup_id, sync);
  }
}

void AddShareSyncInMsgDevice(
    MsgDevice *device, int32_t remote_device_id) {
  AddShareSyncInMsgDeviceForCreater(device, remote_device_id);
  AddShareSyncInMsgDeviceForNotCreater(device, remote_device_id);
}

void UpdateLastSync(int32_t sync_id) {
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableSync::COLUMN_LAST_SYNC, OsTimeInS());
  resolver->Update(
      TableSync::URI, &cv, "%s = %d AND %s = %d",
      TableSync::COLUMN_ID, sync_id, 
      TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL);
}

void InitDeviceStatus() {
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_OFFLINE);
  resolver->Update(TableDevice::URI, &cv, "%s != %d", 
                   TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID);
  ContentValues ip_cv(1);
  ip_cv.Put(TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME, 
            TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE);
  resolver->Update(TableDeviceIP::URI, &ip_cv, NULL);
}

void UpdateEarliestNoRespTimeInDatabase(
    int32_t device_id, const char *device_ip, int64_t time) {
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME, time);
  ZSLOG_INFO("Update device_ip(%s) to NO_RESP_TIME(%" PRId64 ")",
             device_ip, time);
  if (time == TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE) {
    resolver->Update(TableDeviceIP::URI, &cv, "%s = '%s'",
                     TableDeviceIP::COLUMN_IP, device_ip);
  } else {
    resolver->Update(TableDeviceIP::URI, &cv, 
                     "%s = %d AND %s = '%s' AND %s = %" PRId64,
                     TableDeviceIP::COLUMN_DEVICE_ID, device_id, 
                     TableDeviceIP::COLUMN_IP, device_ip,
                     TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME, 
                     TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE);
  }
}

static inline bool IsBackupSrc(int32_t backup_id, int32_t device_id) {
  IContentResolver *resolver = GetContentResolver();
  const char* sync_projs[] = {
    TableSync::COLUMN_STATUS,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %d AND %s = %d", 
          TableSync::COLUMN_ID, backup_id, 
          TableSync::COLUMN_DEVICE_ID, device_id));
  if (sync_cursor->MoveToNext()) {
    return sync_cursor->GetInt32(0) == TableSync::STATUS_NORMAL;
  }
  return false;
}

// bool IsBackupTargetNormal(int32_t backup_id, int32_t device_id) {
//   return IsBackupDst(backup_id, device_id) || IsBackupSrc(backup_id, device_id);
// }

err_t SetSyncMode(
    int32_t local_tree_id, int32_t remote_tree_id, int sync_mode, 
    int32_t sync_time_in_s) {
  assert(local_tree_id != remote_tree_id);
  assert(local_tree_id >= 0);
  assert(remote_tree_id >= 0);
  IContentResolver *resolver = GetContentResolver();

  ContentValues cv(4);
  cv.Put(TableSyncMode::COLUMN_LOCAL_TREE_ID, local_tree_id);
  cv.Put(TableSyncMode::COLUMN_REMOTE_TREE_ID, remote_tree_id);
  cv.Put(TableSyncMode::COLUMN_SYNC_MODE, sync_mode);
  cv.Put(TableSyncMode::COLUMN_SYNC_TIME, sync_time_in_s);
  int32_t row_id = resolver->Insert(TableSyncMode::URI, &cv, AOC_REPLACE);
  if (row_id < 0) {
    ZSLOG_ERROR("Insert SyncMode(%d, %d) fail",
                local_tree_id, remote_tree_id);
    return ZISYNC_ERROR_CONTENT;
  }
  return ZISYNC_SUCCESS;
}

void GetSyncMode(
    int32_t local_tree_id, int32_t local_tree_type, 
    int32_t remote_tree_id, int *sync_mode, int32_t *sync_time_in_s) {
  IContentResolver *resolver = GetContentResolver();
  const char *projs[] = {
    TableSyncMode::COLUMN_SYNC_MODE, TableSyncMode::COLUMN_SYNC_TIME,
  };
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableSyncMode::URI, projs, ARRAY_SIZE(projs),
          "%s = %d AND %s = %d", 
          TableSyncMode::COLUMN_LOCAL_TREE_ID, local_tree_id,
          TableSyncMode::COLUMN_REMOTE_TREE_ID, remote_tree_id));
  if (!cursor->MoveToNext()) {
    *sync_mode = local_tree_type == TableTree::BACKUP_NONE ? 
        SYNC_MODE_AUTO : SYNC_MODE_MANUAL;
  } else {
    *sync_mode = cursor->GetInt32(0);
    if (*sync_mode == SYNC_MODE_TIMER) {
      if (sync_time_in_s != NULL) {
        *sync_time_in_s = cursor->GetInt32(1);
      }
    }
  }
}

err_t SetShareSyncPerm(
    int32_t device_id, int32_t sync_id, int32_t sync_perm) {
  assert(sync_perm != TableSync::PERM_TOKEN_DIFF);
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(3);
  cv.Put(TableShareSync::COLUMN_DEVICE_ID, device_id);
  cv.Put(TableShareSync::COLUMN_SYNC_ID, sync_id);
  cv.Put(TableShareSync::COLUMN_SYNC_PERM, sync_perm);
  int32_t row_id = resolver->Insert(TableShareSync::URI, &cv, AOC_REPLACE);
  if (row_id < 0) {
    ZSLOG_ERROR("Insert ShareSync(%d, %d) fail",
                device_id, sync_id);
    return ZISYNC_ERROR_CONTENT;
  }
  return ZISYNC_SUCCESS;
}

err_t GetShareSyncPerm(
    int32_t device_id, int32_t sync_id, int32_t *sync_perm /* = NULL */) {
  IContentResolver *resolver = GetContentResolver();
  if (device_id == TableDevice::LOCAL_DEVICE_ID) {
    const char *projs[] = {
      TableSync::COLUMN_PERM,
    };
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableSync::URI, projs, ARRAY_SIZE(projs),
            "%s = %d AND %s = %d",
            TableSync::COLUMN_ID, sync_id,
            TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
    if (!cursor->MoveToNext()) {
      ZSLOG_ERROR("Noent ShareSync(%d, %d)", device_id, sync_id);
      return ZISYNC_ERROR_SYNC_NOENT;
    }
    if (sync_perm != NULL) {
      *sync_perm = cursor->GetInt32(0);
    }
  } else {
    const char *projs[] = {
      TableShareSync::COLUMN_SYNC_PERM,
    };
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableShareSync::URI, projs, ARRAY_SIZE(projs),
            "%s = %d AND %s = %d",
            TableShareSync::COLUMN_DEVICE_ID, device_id,
            TableShareSync::COLUMN_SYNC_ID, sync_id));
    if (!cursor->MoveToNext()) {
      ZSLOG_ERROR("Noent ShareSync(%d, %d)", device_id, sync_id);
      return ZISYNC_ERROR_SHARE_SYNC_NOENT;
    }
    if (sync_perm != NULL) {
      *sync_perm = cursor->GetInt32(0);
      assert(*sync_perm != TableSync::PERM_TOKEN_DIFF);
    }
  }
  return ZISYNC_SUCCESS;
}


/* update sync, tree and device */
void TokenChangeToDiff(const string &sync_where, const string &device_where) {
  IContentResolver *resolver = GetContentResolver();
  OperationList op_list;
  ContentOperation *co;


  string share_sync_where;
  StringFormat(
      &share_sync_where, 
      "( %s ) AND %s = %d AND %s != %d AND %s != %d", sync_where.c_str(), 
      TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
      TableSync::COLUMN_RESTORE_SHARE_PERM, 
      TableSync::RESTORE_SHARE_PERM_NULL,
      TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP);
  {
    // TODO JOIN
    const char *sync_projs[] = { 
      TableSync::COLUMN_ID
        , TableSync::COLUMN_RESTORE_SHARE_PERM
        , TableSync::COLUMN_PERM
        , TableSync::COLUMN_DEVICE_ID};
    unique_ptr<ICursor2> sync_cursor(resolver->Query(
            TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
            "%s", share_sync_where.c_str()));
    while (sync_cursor->MoveToNext()) {
      int32_t sync_id = sync_cursor->GetInt32(0);
      int32_t restore_share_perm = sync_cursor->GetInt32(1);
      int32_t current_perm = sync_cursor->GetInt32(2);
      int32_t creator_id = sync_cursor->GetInt32(3);
      unique_ptr<Device> creator(Device::GetById(creator_id));
      bool creator_is_mine = creator->is_mine();

      assert(restore_share_perm != TableSync::PERM_DISCONNECT);
      assert(restore_share_perm != TableSync::PERM_CREATOR_DELETE);

      if (creator_is_mine) {
        int32_t perm_update, perm_restore_update;
        if (restore_share_perm == TableSync::PERM_TOKEN_DIFF) {
          Tree::AppendDeleteBy(
              &op_list, "%s = %d AND %s != %d", 
              TableTree::COLUMN_SYNC_ID, sync_id,
              TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
          co = op_list.NewUpdate(
              TableTree::URI, "%s = %d AND %s = %d",
              TableTree::COLUMN_SYNC_ID, sync_id, 
              TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
          co->GetContentValues()->Put(TableTree::COLUMN_IS_ENABLED, false);
        }
        perm_restore_update = current_perm;
        perm_update = restore_share_perm;
        co = op_list.NewUpdate(TableSync::URI, "%s = %d", 
            TableSync::COLUMN_ID, sync_id);
        co->GetContentValues()->Put(TableSync::COLUMN_PERM, perm_update);
        co->GetContentValues()->Put(TableSync::COLUMN_RESTORE_SHARE_PERM, 
            perm_restore_update);
      }
    }
  }

  string device_where_;
  StringFormat(
      &device_where_, 
      "( %s ) AND %s = %d", device_where.c_str(), 
      TableDevice::COLUMN_IS_MINE, true);
  /*  remove RemoteTrees that related to change token device */
  {
    const char *device_projs[] = { TableDevice::COLUMN_ID };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
            "%s", device_where_.c_str())); 
    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      Tree::AppendDeleteBy(
          &op_list, "%s = %d", TableTree::COLUMN_DEVICE_ID, device_id);
    }
  }
  
  // update device as not mine
  co = op_list.NewUpdate(TableDevice::URI, "%s", device_where_.c_str());
  ContentValues *device_cv = co->GetContentValues();
  //may need to check ip immediately
  //device_cv->Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_OFFLINE);
  device_cv->Put(TableDevice::COLUMN_IS_MINE, false);

  resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
}

void RemoteDeviceTokenChangeToDiff(int32_t device_id, int32_t route_port) {
  if (device_id == TableDevice::LOCAL_DEVICE_ID || device_id == -1) {
    return;
  }

  string sync_where;
  StringFormat(
      &sync_where, "%s = %d", TableSync::COLUMN_DEVICE_ID, device_id);
  string device_where;
  StringFormat(
      &device_where, "%s = %d", TableDevice::COLUMN_ID, device_id);
  TokenChangeToDiff(sync_where, device_where);
  IssueErasePeer(device_id, route_port);
}

void CheckAndDeleteNoIpLeftDevice(int32_t device_id) {
  IContentResolver *resolver = GetContentResolver();
  bool offline = false;
  { 
    const char *device_ip_projs[] = {
      TableDeviceIP::COLUMN_ID,
    };
    unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
            TableDeviceIP::URI, device_ip_projs, ARRAY_SIZE(device_ip_projs),
            "%s = %d", TableDeviceIP::COLUMN_DEVICE_ID, device_id));
    if (!device_ip_cursor->MoveToNext()) {
      ZSLOG_INFO("Set device(%d) as offline due to no IP left", device_id);
      offline = true;
    }
  }
  
  if (offline) {
    ContentValues cv(1);
    cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_OFFLINE);
    resolver->Update(TableDevice::URI, &cv, "%s = %d",
                     TableDevice::COLUMN_ID, device_id);
  }
}

bool GetListableDevice(int32_t device_id, int32_t *route_port) {
  if(device_id == TableDevice::LOCAL_DEVICE_ID) {
    return false;
  }
  IContentResolver *resolver = GetContentResolver();
  const char *device_projs[] = {
    TableDevice::COLUMN_TYPE, TableDevice::COLUMN_ROUTE_PORT,
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
          "%s = %" PRId32 " AND %s = %d", 
          TableDevice::COLUMN_ID, device_id,
          TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
  if (device_cursor->MoveToNext()) {
    Platform platform = static_cast<Platform>(device_cursor->GetInt32(0));
    if (!IsMobileDevice(platform)) {
      if (route_port != NULL) {
        *route_port = device_cursor->GetInt32(1);
      }
      return true;
    }
  }
  return false;
}

}  // namespace zs


