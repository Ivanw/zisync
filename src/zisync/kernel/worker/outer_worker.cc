// Copyright 2014 zisync.com
#include <memory>
#include <vector>
#include <string>

#include "zisync/kernel/worker/outer_worker.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/monitor/monitor.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/vector_clock.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/libevent/discover.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/read_fs_task.h"

namespace zs {

class FindHandler : public MessageHandler {
 public:
  virtual ~FindHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgFindRequest request_msg_;
};

static inline err_t CheckFindable(
    int32_t sync_id, const char *remote_device_uuid, 
    const char *remote_tree_uuid, const string &local_tree_uuid, 
    const string &sync_uuid, bool is_rwonly) {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = { 
    TableTree::COLUMN_SYNC_ID, 
  };

  /*  check the remote tree or device*/
  /*  the remote tree has high priority */
  bool remote_tree_ok = false;
  if (remote_tree_uuid != NULL) {
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = '%s' AND %s = %d", TableTree::COLUMN_UUID, 
            remote_tree_uuid,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
    if (tree_cursor->MoveToNext()) {
      int32_t remote_sync_id = tree_cursor->GetInt32(0);
      if (remote_sync_id != sync_id) {
        ZSLOG_ERROR("tree(%s) and (%s) should have same sync_id "
                    "but not", local_tree_uuid.c_str(), 
                    remote_tree_uuid);
        return ZISYNC_ERROR_TREE_NOENT;
      }
      //todo: if root_moved, return error
      remote_tree_ok = true;
    }
  } 
  if (!remote_tree_ok) {
    if (remote_device_uuid != NULL) {
      bool is_mine = false;
      int32_t device_id = -1;
      {
        const char *device_projs[] = { 
          TableDevice::COLUMN_ID, TableDevice::COLUMN_IS_MINE };
        unique_ptr<ICursor2> device_cursor(resolver->Query(
                TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
                "%s = '%s'", 
                TableDevice::COLUMN_UUID, remote_device_uuid));
        if (!device_cursor->MoveToNext()) {
          return ZISYNC_ERROR_DEVICE_NOENT;
        }
        is_mine = device_cursor->GetBool(1);
        device_id = device_cursor->GetInt32(0);
      }

      // not my device
      if (!is_mine) {
        int32_t sync_perm;
        err_t zisync_ret = zs::GetShareSyncPerm(device_id, sync_id, &sync_perm);
        if (zisync_ret != ZISYNC_SUCCESS || 
            sync_perm == TableSync::PERM_DISCONNECT) {
          return ZISYNC_ERROR_DEVICE_NOENT;
        }
      }
    } else {
      return ZISYNC_ERROR_TREE_NOENT;
    }
  }

  {
    const char *sync_projs[] = {
      TableSync::COLUMN_UUID, TableSync::COLUMN_PERM,
    };
    unique_ptr<ICursor2> sync_cursor(resolver->Query(
            TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
            "%s = %" PRId32 " AND %s = %d", TableSync::COLUMN_ID, sync_id,
            TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
    if (!sync_cursor->MoveToNext()) {
      // if sync_id not found, it must be moved by another thread, then the tree
      // should have been moved 
      return ZISYNC_ERROR_TREE_NOENT;
    } else {
      int32_t sync_perm = sync_cursor->GetInt32(1);
      if (sync_perm == TableSync::PERM_DISCONNECT ||
          sync_perm == TableSync::PERM_CREATOR_DELETE ||
          sync_perm == TableSync::PERM_TOKEN_DIFF || 
          // if list sync, only find PERMISSION OF RW
          (is_rwonly &&
           sync_perm != TableSync::PERM_RDWR)) {
        return ZISYNC_ERROR_SYNC_NOENT;
      }
    }
    const char *sync_uuid_ = sync_cursor->GetString(0);
    if (sync_uuid != sync_uuid_) {
      return ZISYNC_ERROR_SYNCDIR_MISMATCH;
    }
  }
  return ZISYNC_SUCCESS;
}

err_t FindHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  IContentResolver* resolver = GetContentResolver();
  ZSLOG_INFO("Find Start.");
  
  int32_t sync_id;
  int32_t tree_id;
  {
    const char *tree_projs[] = { 
      TableTree::COLUMN_SYNC_ID, TableTree::COLUMN_ID, 
    };
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = '%s' AND %s = %d", TableTree::COLUMN_UUID, 
            request_msg_.remote_tree_uuid().c_str(),
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
    if (!tree_cursor->MoveToNext()) {
      return  ZISYNC_ERROR_TREE_NOENT;
    }
    sync_id = tree_cursor->GetInt32(0);
    tree_id = tree_cursor->GetInt32(1);
  }

  const char *remote_device_uuid = request_msg_.has_device_uuid() ? 
      request_msg_.device_uuid().c_str() : NULL;
  const char *remote_tree_uuid = request_msg_.has_local_tree_uuid() ? 
      request_msg_.local_tree_uuid().c_str() : NULL;
  err_t zisync_ret = CheckFindable(
      sync_id, remote_device_uuid, remote_tree_uuid, 
      request_msg_.remote_tree_uuid(), request_msg_.sync_uuid(), 
      request_msg_.has_is_list_sync() && request_msg_.is_list_sync());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  FindResponse response;

  MsgRemoteMeta *remote_meta = 
      response.mutable_response()->mutable_remote_meta();
  {
    const char *vclock_tree_projs[] = {
      TableTree::COLUMN_UUID, 
    };
    unique_ptr<ICursor2> vclock_tree_cursor(resolver->Query(
            TableTree::URI, vclock_tree_projs, ARRAY_SIZE(vclock_tree_projs),
            "%s = %" PRId32 " AND %s != '%s'", 
            TableTree::COLUMN_SYNC_ID, sync_id,
            TableTree::COLUMN_UUID, request_msg_.remote_tree_uuid().c_str()));
    remote_meta->add_uuids(request_msg_.remote_tree_uuid());
    while(vclock_tree_cursor->MoveToNext()) {
      remote_meta->add_uuids(vclock_tree_cursor->GetString(0));
    }
  }

  const char *file_projs[] = {
    TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
    TableFile::COLUMN_STATUS, TableFile::COLUMN_MTIME, 
    TableFile::COLUMN_LENGTH, TableFile::COLUMN_USN, 
    TableFile::COLUMN_SHA1, TableFile::COLUMN_UNIX_ATTR, 
    TableFile::COLUMN_ANDROID_ATTR, 
    TableFile::COLUMN_WIN_ATTR, TableFile::COLUMN_LOCAL_VCLOCK,
    TableFile::COLUMN_REMOTE_VCLOCK, TableFile::COLUMN_MODIFIER,
    TableFile::COLUMN_TIME_STAMP,
  };

  Selection selection("%s > %" PRId64, TableFile::COLUMN_USN, 
                      request_msg_.since());
  string sort;
  StringFormat(&sort, "%s LIMIT %" PRId32, TableFile::COLUMN_USN, 
               request_msg_.limit());
  unique_ptr<ICursor2> file_cursor(resolver->sQuery(
          TableFile::GenUri(request_msg_.remote_tree_uuid().c_str()), 
          file_projs, ARRAY_SIZE(file_projs), &selection, sort.c_str())); 
  while (file_cursor->MoveToNext()) {
    if (!SyncList::NeedSync(tree_id, file_cursor->GetString(0))) {
      continue;
    }
    MsgStat *file_stat = remote_meta->add_stats();
    file_stat->set_path(file_cursor->GetString(0));
    file_stat->set_type(file_cursor->GetInt32(1) == OS_FILE_TYPE_DIR ?
                        FT_DIR : FT_REG);
    file_stat->set_status(file_cursor->GetInt32(2) == TableFile::STATUS_NORMAL ?
                          FS_NORMAL : FS_REMOVE);
    file_stat->set_mtime(file_cursor->GetInt64(3));
    file_stat->set_length(file_cursor->GetInt64(4));
    file_stat->set_usn(file_cursor->GetInt64(5));
    file_stat->set_sha1(file_cursor->GetString(6));
    file_stat->set_unix_attr(file_cursor->GetInt32(7));
    file_stat->set_android_attr(file_cursor->GetInt32(8));
    file_stat->set_win_attr(file_cursor->GetInt32(9));
    file_stat->add_vclock(file_cursor->GetInt32(10));
    VectorClock vclock(file_cursor->GetBlobBase(11), 
                       file_cursor->GetBlobSize(11));
    for (int i = 0; i < vclock.length() && 
         i < remote_meta->uuids_size(); i ++) {
      file_stat->add_vclock(vclock.at(i));
    }
	const char *modifier = file_cursor->GetString(12);
	if (!modifier) {
		modifier = "";
	}
    file_stat->set_modifier(modifier);
    file_stat->set_time_stamp(file_cursor->GetInt64(13));

  }

  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("Find End.");

  return ZISYNC_SUCCESS;
}

class FindFileHandler : public MessageHandler {
 public:
  virtual ~FindFileHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgFindFileRequest request_msg_;
};

err_t FindFileHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  IContentResolver* resolver = GetContentResolver();
  ZSLOG_INFO("FindiFile Start.");

  int32_t sync_id;
  {
    const char *tree_projs[] = { 
      TableTree::COLUMN_SYNC_ID
    };
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = '%s' AND %s = %d", TableTree::COLUMN_UUID, 
            request_msg_.remote_tree_uuid().c_str(),
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
    if (!tree_cursor->MoveToNext()) {
      ZSLOG_ERROR("Noent local tree(%s)", 
                  request_msg_.remote_tree_uuid().c_str());
      return  ZISYNC_ERROR_TREE_NOENT;
    }
    sync_id = tree_cursor->GetInt32(0);
  }
  const char *remote_device_uuid = request_msg_.has_device_uuid() ? 
      request_msg_.device_uuid().c_str() : NULL;
  const char *remote_tree_uuid = request_msg_.has_local_tree_uuid() ? 
      request_msg_.local_tree_uuid().c_str() : NULL;
  err_t zisync_ret = CheckFindable(
      sync_id, remote_device_uuid, remote_tree_uuid, 
      request_msg_.remote_tree_uuid(), request_msg_.sync_uuid(), true);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  const char *file_projs[] = {
    TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
    TableFile::COLUMN_STATUS, TableFile::COLUMN_MTIME, 
    TableFile::COLUMN_LENGTH, TableFile::COLUMN_USN, 
    TableFile::COLUMN_SHA1, TableFile::COLUMN_UNIX_ATTR, 
    TableFile::COLUMN_ANDROID_ATTR, 
    TableFile::COLUMN_WIN_ATTR,
    TableFile::COLUMN_MODIFIER,
    TableFile::COLUMN_TIME_STAMP,
  };

  unique_ptr<ICursor2> file_cursor(resolver->Query(
          TableFile::GenUri(request_msg_.remote_tree_uuid().c_str()), 
          file_projs, ARRAY_SIZE(file_projs), "%s = '%s'", 
          TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(request_msg_.path()).c_str())); 
  if (!file_cursor->MoveToNext()) {
    ZSLOG_ERROR("Noent file(%s)", 
                request_msg_.path().c_str());
    return ZISYNC_ERROR_FILE_NOENT;
  }

  FindFileResponse response;
  MsgStat *file_stat = response.mutable_response()->mutable_stat();
  file_stat->set_path(file_cursor->GetString(0));
  file_stat->set_type(file_cursor->GetInt32(1) == OS_FILE_TYPE_DIR ?
                      FT_DIR : FT_REG);
  file_stat->set_status(file_cursor->GetInt32(2) == TableFile::STATUS_NORMAL ?
                        FS_NORMAL : FS_REMOVE);
  file_stat->set_mtime(file_cursor->GetInt64(3));
  file_stat->set_length(file_cursor->GetInt64(4));
  file_stat->set_usn(file_cursor->GetInt64(5));
  file_stat->set_sha1(file_cursor->GetString(6));
  file_stat->set_unix_attr(file_cursor->GetInt32(7));
  file_stat->set_android_attr(file_cursor->GetInt32(8));
  file_stat->set_win_attr(file_cursor->GetInt32(9));
  const char *modifier = file_cursor->GetString(10);
  if (!modifier) {
	  modifier = "";
  }
  file_stat->set_modifier(modifier);
  file_stat->set_time_stamp(file_cursor->GetInt64(11));
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("FindFile End.");

  return ZISYNC_SUCCESS;
}

/*  show all local normal tree */
class DeviceInfoHandler : public MessageHandler {
 public:
  virtual ~DeviceInfoHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgDeviceInfoRequest request_msg_;
};

err_t DeviceInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start DeviceInfo");
#ifndef ZS_TEST
  if (head.level() != EL_ENCRYPT_WITH_ACCOUNT && 
      !request_msg_.has_sync_uuid()) {
    ZSLOG_ERROR("Expected EncryptLevel(%d) but is (%d)",
                EL_ENCRYPT_WITH_ACCOUNT, head.level());
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
#endif
  DeviceInfoResponse response_;
  MsgDeviceInfoResponse *response = response_.mutable_response();
  SetDeviceMetaInMsgDevice(response->mutable_device());
  // int set_msg_device_type;
  int32_t device_id = -1;
  bool is_mine = false;
  if (request_msg_.has_device_uuid()) {
    IContentResolver *resolver = GetContentResolver();
    const char *device_projs[] = {
      TableDevice::COLUMN_ID, TableDevice::COLUMN_IS_MINE,
    };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
            "%s = '%s'", TableDevice::COLUMN_UUID, 
            request_msg_.device_uuid().c_str()));
    if (device_cursor->MoveToNext()) {
      device_id = device_cursor->GetInt32(0);
      is_mine = device_cursor->GetBool(1);
    }
  }
  
  if (request_msg_.has_sync_uuid()) {
    unique_ptr<Sync> local_sync(Sync::GetBy(
            "%s = '%s' AND %s = %d", 
            TableSync::COLUMN_UUID, request_msg_.sync_uuid().c_str(),
            TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED));
    if (!local_sync) {
      return ZISYNC_ERROR_SYNC_NOENT;
    }
    MsgSync *sync = response->mutable_device()->add_syncs();
    local_sync->ToMsgSync(sync);
    SetTreesInMsgSync(local_sync->id(), sync);
    if (device_id != -1 && !is_mine) { // new version msg
      if (local_sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
        // if is Creater only response the shared device
        int32_t sync_perm;
        err_t zisync_ret = GetShareSyncPerm(
            device_id, local_sync->id(), &sync_perm);
        if (zisync_ret != ZISYNC_SUCCESS) {
          ZSLOG_WARNING("GetShareSyncPerm(%d, %d) fail : %s",
                        device_id, local_sync->id(), 
                        zisync_strerror(zisync_ret));
          return zisync_ret;
        }
        sync->set_perm(SyncPermToMsgSyncPerm(sync_perm));
      } else if (local_sync->device_id() == device_id) {
        sync->set_perm(SyncPermToMsgSyncPerm(
                local_sync->perm()));
      }
    }
    // otherwise, Not the Creater, it does not kown who should own the sync,
    // so always response
  } else {
    if (device_id != -1) {
      AddBackupInMsgDevice(response->mutable_device(), device_id);
      SetBackupRootInMsgDevice(device_id, response->mutable_device());
    }
    SetMsgDeviceForMyDevice(response->mutable_device());
  }

  //SetMsgDevice(response->mutable_device(), set_msg_device_type, 
  //             request_msg_.has_sync_uuid() ? 
  //             request_msg_.sync_uuid().c_str() : NULL);

  err_t zisync_ret;
  if (head.level() != EL_ENCRYPT_WITH_ACCOUNT) {
    zisync_ret = response_.SendTo(socket);
  } else {
    zisync_ret = response_.SendToWithAccountEncrypt(socket);
  }
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End DeviceInfo");
  return ZISYNC_SUCCESS;
}

class PushDeviceInfoHandler : public MessageHandler {
 public:
  virtual ~PushDeviceInfoHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgPushDeviceInfoRequest request_msg_;
};

err_t PushDeviceInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start PushDeviceInfo");
  bool is_mine = (head.level() == EL_ENCRYPT_WITH_ACCOUNT);
  const MsgDevice &device = request_msg_.device();
  int32_t device_id = StoreDeviceIntoDatabase(
      device, NULL, is_mine, false, false);
  if (device_id == -1) {
    ZSLOG_ERROR("StoreDeviceIntoDatabase(%s) fail", device.uuid().c_str());
    return ZISYNC_ERROR_CONTENT;
  } else if (device_id == TableDevice::LOCAL_DEVICE_ID) {
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
  // GetEventNotifier()->NotifySyncModify();
  // GetEventNotifier()->NotifyBackupModify();

  PushDeviceInfoResponse response_;
  if (is_mine) {
    err_t zisync_ret = response_.SendToWithAccountEncrypt(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);
  } else {
    err_t zisync_ret = response_.SendTo(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  ZSLOG_INFO("End PushDeviceInfo");
  return ZISYNC_SUCCESS;
}

class PushBackupInfoHandler : public MessageHandler {
 public:
  virtual ~PushBackupInfoHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgPushBackupInfoRequest request_msg_;
};

err_t PushBackupInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start PushBackupInfo");
  if (head.level() != EL_ENCRYPT_WITH_ACCOUNT) {
    ZSLOG_ERROR("Expected EncryptLevel(%d) but is (%d)",
                EL_ENCRYPT_WITH_ACCOUNT, head.level());
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  if (IsMobileDevice()) {
    ZSLOG_ERROR("Mobile device can not be backup target");
    return ZISYNC_ERROR_GENERAL;
  }
  const MsgDevice &device = request_msg_.device();
  assert(request_msg_.device().syncs_size() == 1);
  if (request_msg_.device().syncs_size() != 1) {
    return ZISYNC_ERROR_INVALID_MSG;
  }
  const MsgSync &sync = device.syncs(0);
  assert(sync.type() == ST_BACKUP);
  if (sync.type() != ST_BACKUP) {
    return ZISYNC_ERROR_INVALID_MSG;
  }
  int32_t device_id = StoreDeviceIntoDatabase(
      device, NULL, true, false, false);
  if (device_id <= 0) {
    ZSLOG_ERROR("StoreDeviceIntoDatabase(%s) fail", device.uuid().c_str());
    return ZISYNC_ERROR_CONTENT;
  } 

  int32_t sync_id = Sync::GetIdByUuidWhereStatusNormal(sync.uuid());
  assert(sync_id != -1);
  if (sync_id == -1) {
    ZSLOG_ERROR("Sync(%s) does not exist, which should not happen", 
                sync.uuid().c_str());
    return ZISYNC_ERROR_GENERAL;
  }
  BackupDstTree tree;
  string tree_root;
  if (request_msg_.has_root()) {
    tree_root = request_msg_.root();
  }
  err_t zisync_ret = tree.Create(device_id, sync_id, tree_root);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("TreeAdd for backup(%s) fail : %s", 
                sync.uuid().c_str(), zisync_strerror(zisync_ret));
    // TODO if treeAdd fail , remove sync
    return zisync_ret;
  }
  IssueRefresh(tree.id());
  // GetEventNotifier()->NotifyBackupModify();

  PushBackupInfoResponse response_;
  MsgDevice *resp_device = response_.mutable_response()->mutable_device();
  SetDeviceMetaInMsgDevice(resp_device);
  MsgSync *resp_sync = resp_device->add_syncs();
  
  resp_sync->set_type(ST_BACKUP);
  resp_sync->set_name(device.syncs(0).name());
  resp_sync->set_uuid(device.syncs(0).uuid());
  resp_sync->set_is_normal(device.syncs(0).is_normal());
  {
    const char* tree_projs[] = {
      TableTree::COLUMN_ROOT, TableTree::COLUMN_UUID, 
      TableTree::COLUMN_STATUS,
    };
    IContentResolver *resolver = GetContentResolver();
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d", 
            TableTree::COLUMN_ID, tree.id(), 
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
    if (!tree_cursor->MoveToNext()) {
      return ZISYNC_ERROR_TREE_NOENT;
    }
    MsgTree *tree = resp_sync->add_trees();
    tree->set_root(tree_cursor->GetString(0));
    tree->set_uuid(tree_cursor->GetString(1));
    assert(tree_cursor->GetInt32(2) != TableTree::STATUS_VCLOCK);
    tree->set_is_normal(tree_cursor->GetInt32(2) == TableTree::STATUS_NORMAL);
  }

  zisync_ret = response_.SendToWithAccountEncrypt(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End PushBackupInfo");
  return ZISYNC_SUCCESS;
}

class AnnounceExitHandler : public MessageHandler {
 public:
  virtual ~AnnounceExitHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgAnnounceExitRequest request_msg_;
};

err_t AnnounceExitHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  IContentResolver *resolver = GetContentResolver();

  const char* device_uuid = request_msg_.device_uuid().c_str();

  int32_t device_id;
  int32_t route_port;
  {
    const char *device_projs[] = {
      TableDevice::COLUMN_ID,
      TableDevice::COLUMN_ROUTE_PORT,
    };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
            "%s = '%s'", TableDevice::COLUMN_UUID, device_uuid));
    if (!device_cursor->MoveToNext()) {
      ZSLOG_WARNING("Noent Device(%s)", device_uuid);
      return ZISYNC_ERROR_DEVICE_NOENT;
    }

    device_id = device_cursor->GetInt32(0);
    route_port = device_cursor->GetInt32(1);
  }

  ContentValues cv(1);
  cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_OFFLINE);
  int affected_row_num = resolver->Update(
      TableDevice::URI, &cv, "%s = %d", 
      TableDevice::COLUMN_ID, device_id);
  if (affected_row_num != 1) {
    ZSLOG_ERROR("Update device(%s) to offline fail", device_uuid);
    return ZISYNC_ERROR_CONTENT;
  }
  
  IssueErasePeer(device_id, route_port);

  AnnounceExitResponse response_;
  err_t zisync_ret = response_.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}
  
static bool FileBeenIndexed(const string &rela_path
                            , const string &tree_uuid
                            , const string &tree_root) {
  IContentResolver *resolver = GetContentResolver();
  
  unique_ptr<ICursor2> cursor(resolver->Query(
                                              TableFile::GenUri(tree_uuid.c_str()), FileStat::file_projs_with_remote_vclock,
                                              FileStat::file_projs_with_remote_vclock_len,
                                              "%s = '%s'", TableFile::COLUMN_PATH, 
                                              GenFixedStringForDatabase(rela_path.c_str()).c_str()));
  unique_ptr<FileStat> file_in_db;
  if (cursor->MoveToNext()) {
    file_in_db.reset(new FileStat(cursor.get()));
  }
  
  OsFileStat file_stat;
  int rv = OsStat(tree_root + rela_path
                  , string()
                  , &file_stat);
  if (rv == 0 && !file_in_db) {
    return false;
  }
  return true;
}

class FilterPushSyncMetaHandler : public MessageHandler {
 public:
  virtual ~FilterPushSyncMetaHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgFilterPushSyncMetaRequest request_msg_;
};

err_t FilterPushSyncMetaHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  IContentResolver *resolver = GetContentResolver();

  int32_t sync_id, tree_id;
  string tree_root;
  vector<unique_ptr<Tree>> trees;
  {
    const char *tree_projs[] = {
      TableTree::COLUMN_ID, TableTree::COLUMN_SYNC_ID, TableTree::COLUMN_ROOT,
    };

    Tree::QueryBy(&trees,
                  "%s = '%s' AND %s = %d AND %s = %d", 
                  TableTree::COLUMN_UUID, request_msg_.local_tree_uuid().c_str(), 
                  TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
                  TableTree::COLUMN_IS_ENABLED, true);
    if (trees.size() == 0) {
      ZSLOG_ERROR("Noent tree(%s)", request_msg_.local_tree_uuid().c_str());
      return ZISYNC_ERROR_TREE_NOENT;
    }
    sync_id = trees[0]->sync_id();
    tree_id = trees[0]->id();

    tree_root = trees[0]->root();
//    unique_ptr<ICursor2> tree_cursor(resolver->Query(
//            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
//            "%s = '%s' AND %s = %d AND %s = %d", 
//            TableTree::COLUMN_UUID, request_msg_.local_tree_uuid().c_str(), 
//            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
//            TableTree::COLUMN_IS_ENABLED, true));
//    if (!tree_cursor->MoveToNext()) {
//      ZSLOG_ERROR("Noent tree(%s)", request_msg_.local_tree_uuid().c_str());
//      return ZISYNC_ERROR_TREE_NOENT;
//    }
//    sync_id = tree_cursor->GetInt32(1);
//    tree_id = tree_cursor->GetInt32(0);
//    if (tree_cursor->GetInt32(2)) {
//      tree_root = tree_cursor->GetInt32(2);
//    }else {
//      assert(false);
//    }
  }

  int32_t sync_perm = Sync::GetSyncPermByIdWhereStatusNormal(sync_id);
  if (sync_perm == -1 || sync_perm == TableSync::PERM_DISCONNECT ||
      sync_perm == TableSync::PERM_TOKEN_DIFF || 
      sync_perm == TableSync::PERM_CREATOR_DELETE) {
    // WR not allow push
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  } else if (sync_perm == TableSync::PERM_WRONLY) {
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
  
  FilterPushSyncMetaResponse response_;
  MsgFilterPushSyncMetaResponse *response = response_.mutable_response();
  for (int i = 0; i < request_msg_.stats_size(); i ++) {
    if (SyncList::NeedSync(tree_id, request_msg_.stats(i).path().c_str())
        && ( ReadFsTask::GetType(*trees[0]) != ReadFsTask::TYPE_RDWR
            ||FileBeenIndexed(request_msg_.stats(i).path()
                              , request_msg_.local_tree_uuid()
                              , tree_root) )
        ){
      MsgStat *stat = response->add_stats();
      *stat = request_msg_.stats(i);
    }
  }

  err_t zisync_ret = response_.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class ShareSyncHandler : public MessageHandler {
 public:
  virtual ~ShareSyncHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgShareSyncRequest request_msg_;
};

err_t ShareSyncHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start ShareSyncHandler");
  const MsgDevice &device = request_msg_.device();
  int32_t device_id = StoreDeviceIntoDatabase(
      device, NULL, false, false, true);
  if (device_id == -1) {
    ZSLOG_ERROR("StoreDeviceIntoDatabase(%s) fail", device.uuid().c_str());
    return ZISYNC_ERROR_CONTENT;
  } else if (device_id == TableDevice::LOCAL_DEVICE_ID) {
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
  
  /* StoreSync not change the status of sync, so we need to update sync to
   * normal now */
  assert(request_msg_.device().syncs_size() == 1);
  IContentResolver *resolver = GetContentResolver();
  ContentValues cv(1);
  cv.Put(TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL);
  resolver->Update(
      TableSync::URI, &cv, "%s = '%s'", 
      TableSync::COLUMN_UUID, request_msg_.device().syncs(0).uuid().c_str());
  // GetEventNotifier()->NotifySyncModify();

  ShareSyncResponse response;
  SetDeviceMetaInMsgDevice(response.mutable_response()->mutable_device());

  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End ShareSyncHandler");
  return ZISYNC_SUCCESS;
}

class DeviceMetaHandler : public MessageHandler {
 public:
  virtual ~DeviceMetaHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t DeviceMetaHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  DeviceMetaResponse response;
  SetDeviceMetaInMsgDevice(response.mutable_response()->mutable_device());
  response.mutable_response()->set_token(Config::token_sha1());
  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class AnnounceTokenChangedHandler : public MessageHandler {
 public:
  virtual ~AnnounceTokenChangedHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgAnnounceTokenChangedRequest request_msg_;
};

err_t AnnounceTokenChangedHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  int32_t device_id = -1, route_port = -1;

  if (request_msg_.new_token() != Config::token_sha1()) {
    IContentResolver *resolver = GetContentResolver();
    const string &device_uuid = request_msg_.device_uuid();
    {
      const char *device_projs[] = {
        TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT,
      };
      unique_ptr<ICursor2> device_cursor(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
              "%s = '%s' AND %s = %d", 
              TableDevice::COLUMN_UUID, device_uuid.c_str(),
              TableDevice::COLUMN_IS_MINE, true));
      if (device_cursor->MoveToNext())
        device_id = device_cursor->GetInt32(0);
        route_port = device_cursor->GetInt32(1);
    }

    if (device_id != TableDevice::LOCAL_DEVICE_ID && device_id != -1) {
      RemoteDeviceTokenChangeToDiff(device_id, route_port);
    }
  }
  
  AnnounceTokenChangedResponse response;
  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = req.Connect(zs::router_inner_pull_fronter_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connect to (%s) fail.", router_inner_pull_fronter_uri);
    return zisync_ret;
  }

  IssuePushDeviceInfoRequest push_request;
  zisync_ret = push_request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class RemoveRemoteFileHandler : public MessageHandler {
 public:
  virtual ~RemoveRemoteFileHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgRemoveRemoteFileRequest request_msg_;
};

static bool IsFsDbConsistent(
    const std::string &tree_uuid, const OsFileStat &stat, const std::string root) {
  const char *file_projs[] = {
    TableFile::COLUMN_TYPE, TableFile::COLUMN_STATUS,
    TableFile::COLUMN_LENGTH,
  };
  std::string relative_path = stat.path.c_str() + root.size();
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> cursor(resolver->Query(
        TableFile::GenUri(tree_uuid.c_str()), file_projs, ARRAY_SIZE(file_projs),
            "%s = '%s'", TableFile::COLUMN_PATH, 
            GenFixedStringForDatabase(relative_path).c_str()));
    if (cursor->MoveToNext()) {
      if (cursor->GetInt32(0) == stat.type
        && cursor->GetInt32(1) == TableFile::STATUS_NORMAL
        && cursor->GetInt64(2) == stat.length) {
        return true;
      }
    }
    return false;
}

err_t RemoveRemoteFileHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  
  const string &sync_uuid = request_msg_.sync_uuid();
  const string &relative_path = request_msg_.relative_path();

  int32_t sync_id = Sync::GetIdByUuidWhereStatusNormal(sync_uuid);
  if (sync_id == -1) {
    ZSLOG_ERROR("No such sync: %s", sync_uuid.c_str());
    return ZISYNC_SUCCESS;
  }
  std::vector<std::unique_ptr<Tree>> trees;
  Tree::QueryBySyncIdWhereStatusNormal(sync_id, &trees);

  RemoveRemoteFileResponse response;
  MsgRemoveRemoteFileResponse &response_msg = *response.mutable_response(); 
  response_msg.set_relative_path(relative_path);
  response_msg.set_sync_uuid(sync_uuid);
  response_msg.set_device_uuid(Config::device_uuid());
  response_msg.set_error(E_NONE);

  std::string full_path;
  OsFileStat tmp;
  int rm_ret;
  for (auto it = trees.begin(); it != trees.end(); ++it) {
    full_path = (*it)->root(); 
    if (relative_path.at(0) == '/') {
      zs::OsPathAppend(&full_path, relative_path.c_str() + 1);
    }else{
      zs::OsPathAppend(&full_path, relative_path);
    }
    zs::OsStat(full_path, string(), &tmp); 
    if (IsFsDbConsistent((*it)->uuid(), tmp, (*it)->root())) {
      rm_ret = zs::OsDeleteDirectories(full_path.c_str());
    }else {
      continue;
    }
    if (rm_ret ==  0) {
      response_msg.set_success_tree_uuid((*it)->uuid());
      response_msg.set_error(E_NONE);
      break;
    }else if (rm_ret == ENOENT){
      if (response_msg.error() != E_RM_FAIL) {
        response_msg.set_error(E_NOENT);
      }
    }else {
      response_msg.add_fail_tree_uuids((*it)->uuid());      
      response_msg.set_error(E_RM_FAIL);
    }
  }

  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("Remove Remote File End.");

  return ZISYNC_SUCCESS;
}

OuterWorker::OuterWorker():
    Worker("OuterWorker") {
      msg_handlers_[MC_FIND_REQUEST] = new FindHandler;
      msg_handlers_[MC_DEVICE_INFO_REQUEST] = new DeviceInfoHandler;
      msg_handlers_[MC_PUSH_DEVICE_INFO_REQUEST] = new PushDeviceInfoHandler;
      msg_handlers_[MC_PUSH_BACKUP_INFO_REQUEST] = new PushBackupInfoHandler;
      msg_handlers_[MC_ANNOUNCE_EXIT_REQUEST] = new AnnounceExitHandler;
      msg_handlers_[MC_FILTER_PUSH_SYNC_META_REQUEST] = new FilterPushSyncMetaHandler;
      msg_handlers_[MC_SHARE_SYNC_REQUEST] = new ShareSyncHandler;
      msg_handlers_[MC_DEVICE_META_REQUEST] = new DeviceMetaHandler;
      msg_handlers_[MC_FIND_FILE_REQUEST] = new FindFileHandler;
      msg_handlers_[MC_ANNOUNCE_TOKEN_CHANGED_REQUEST] = 
          new AnnounceTokenChangedHandler;
      msg_handlers_[MC_REMOVE_REMOTE_FILE_REQUEST] =
          new RemoveRemoteFileHandler;
    }

err_t OuterWorker::Initialize() {
  err_t zisync_ret;
  zisync_ret = req->Connect(zs::router_outer_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = exit->Connect(exit_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

}  // namespace zs
