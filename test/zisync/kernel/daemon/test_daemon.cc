// Copyright 2014, zisync.com
#include <cassert>
#include <unistd.h>
#include <signal.h>
#include <string>
#include <map>
#include <memory>
#include <vector>

#include "zisync_kernel.h"

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/response.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/vector_clock.h"
#include "zisync/kernel/discover/discover.h"
#include "zisync/kernel/history/history.h"
#include "zisync/kernel/proto/verify.pb.h"
#include "zisync/kernel/permission.h"
#include "zisync/kernel/monitor/monitor.h"

using namespace zs;

using std::string;
using std::map;
using std::unique_ptr;

const int32_t PORT = 9523;
const int32_t DISCOVER_PORT = 8848;

static map<MsgCode, MessageHandler*> msg_handlers_;
static IZiSyncKernel *kernel = GetZiSyncKernel("actual");
static string app_data, log_path, backup, token;

#ifdef ZS_TEST
class DeviceAddHandler : public MessageHandler {
 public:
  virtual ~DeviceAddHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgDeviceAddRequest request_msg_;
};

err_t DeviceAddHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start DeviceAdd");
  IContentResolver *resolver = GetContentResolver();

  const char *device_projs[] = {
    TableDevice::COLUMN_ID,
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
          "%s = '%s'", TableDevice::COLUMN_UUID, 
          request_msg_.uuid().c_str()));
  if (device_cursor->MoveToNext()) {
    return ZISYNC_ERROR_DEVICE_EXIST;
  }
  size_t pos1 = request_msg_.route_uri().find_first_of(':');
  size_t pos2 = request_msg_.route_uri().find_last_of(':');
  assert(pos1 != string::npos);
  assert(pos2 != string::npos);
  string ip = request_msg_.route_uri().substr(pos1 + 3, pos2 - pos1 - 3);
  int32_t route_port = atoi(request_msg_.route_uri().substr(pos2 + 1).c_str());
  pos2 = request_msg_.data_uri().find_last_of(':');
  int32_t data_port = atoi(request_msg_.data_uri().substr(pos2 + 1).c_str());

  MsgDevice device;
  device.set_name(request_msg_.name());
  device.set_uuid(request_msg_.uuid());
  device.set_data_port(data_port);
  device.set_route_port(route_port);
  device.set_type(request_msg_.type());

  int32_t device_id = StoreDeviceIntoDatabase(
      device, ip.c_str(), true, false, false);
  if (device_id == -1) {
    ZSLOG_ERROR("Insert Device IP fail.");
    return ZISYNC_ERROR_CONTENT;
  } else if (device_id != TableDevice::LOCAL_DEVICE_ID) {
    ZSLOG_INFO("DeviceAdd(%s), IP(%s)", request_msg_.uuid().c_str(),
               ip.c_str());
  }

  DeviceAddResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End DeviceAdd");
  return ZISYNC_SUCCESS;
}

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
  DeviceInfoResponse response;
  MsgDevice *device = response.mutable_response()->mutable_device();
  zs::SetDeviceMetaInMsgDevice(device);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End DeviceInfo");
  return ZISYNC_SUCCESS;
}

class RemoteDeviceShowHandler : public MessageHandler {
 public:
  virtual ~RemoteDeviceShowHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

};

err_t RemoteDeviceShowHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start RemoteDeviceShow");
  const char *device_projs[] = {
    TableDevice::COLUMN_NAME, TableDevice::COLUMN_TYPE, 
    TableDevice::COLUMN_UUID, TableDevice::COLUMN_DATA_PORT, 
    TableDevice::COLUMN_ROUTE_PORT, TableDevice::COLUMN_ID,
    TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_STATUS,
    TableDevice::COLUMN_ID,
  };
  const char *device_ip_projs[] = {
    TableDeviceIP::COLUMN_IP,
  };
  
  IContentResolver *resolver = GetContentResolver();
  RemoteDeviceShowResponse response;
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
          "%s != %d", 
          TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID));
  while (device_cursor->MoveToNext()) {
    unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
            TableDeviceIP::URI, device_ip_projs, 
            ARRAY_SIZE(device_ip_projs), "%s = %d",
            TableDeviceIP::COLUMN_DEVICE_ID, device_cursor->GetInt32(5)));
    string ip;
    if (device_ip_cursor->MoveToNext()) {
      ip = device_ip_cursor->GetString(0);
    }

    MsgRemoteDeviceShowResult *device = 
        response.mutable_response()->add_devices();
    device->set_name(device_cursor->GetString(0));
    device->set_type(PlatformToMsgDeviceType(
            static_cast<Platform>(device_cursor->GetInt32(1))));
    device->set_uuid(device_cursor->GetString(2));
    int32_t data_port = device_cursor->GetInt32(3);
    int32_t route_port = device_cursor->GetInt32(4);
    string data_uri, route_uri;
    StringFormat(&data_uri, "tcp://%s:%d", ip.c_str(), data_port);
    StringFormat(&route_uri, "tcp://%s:%d", ip.c_str(), route_port);
    device->set_data_uri(data_uri);
    device->set_route_uri(route_uri);
    device->set_is_mine(device_cursor->GetInt32(6));
    device->set_is_online(
        device_cursor->GetInt32(7) == TableDevice::STATUS_ONLINE);
    device->set_id(device_cursor->GetInt32(8));
  }

  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End RemoteDeviceShow");
  return ZISYNC_SUCCESS;
}

class DatabaseInitHandler : public MessageHandler {
 public:
  virtual ~DatabaseInitHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t DatabaseInitHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start DatabaseInit");

  kernel->Shutdown();
  err_t zisync_ret = kernel->Initialize(
      app_data.c_str(), token.c_str(), token.c_str(), backup.c_str());
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = kernel->Startup(app_data.c_str(), DISCOVER_PORT, NULL);
  assert(zisync_ret == ZISYNC_SUCCESS);
  
  DatabaseInitResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End DatabaseInit");
  return ZISYNC_SUCCESS;
}

class MonitorEnableHandler : public MessageHandler {
 public:
  virtual ~MonitorEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t MonitorEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start MonitorEnable");
  Config::enable_monitor();

  MonitorEnableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End MonitorEnable");
  return ZISYNC_SUCCESS;
}

class MonitorDisableHandler : public MessageHandler {
 public:
  virtual ~MonitorDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t MonitorDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start MonitorDisable");
  Config::disable_monitor();

  MonitorDisableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End MonitorDisable");
  return ZISYNC_SUCCESS;
}

class DhtAnnounceEnableHandler : public MessageHandler {
 public:
  virtual ~DhtAnnounceEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t DhtAnnounceEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::enable_dht_announce();

  DhtAnnounceEnableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class DhtAnnounceDisableHandler : public MessageHandler {
 public:
  virtual ~DhtAnnounceDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t DhtAnnounceDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::disable_dht_announce();

  DhtAnnounceDisableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}
class BroadcastEnableHandler : public MessageHandler {
 public:
  virtual ~BroadcastEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t BroadcastEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::enable_broadcast();

  BroadcastEnableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class BroadcastDisableHandler : public MessageHandler {
 public:
  virtual ~BroadcastDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t BroadcastDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::disable_broadcast();
  BroadcastDisableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class AutoSyncEnableHandler : public MessageHandler {
 public:
  virtual ~AutoSyncEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t AutoSyncEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::enable_auto_sync();

  AutoSyncEnableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class AutoSyncDisableHandler : public MessageHandler {
 public:
  virtual ~AutoSyncDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t AutoSyncDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::disable_auto_sync();

  AutoSyncDisableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class DeviceInfoEnableHandler : public MessageHandler {
 public:
  virtual ~DeviceInfoEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t DeviceInfoEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::enable_device_info();

  DeviceInfoEnableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class DeviceInfoDisableHandler : public MessageHandler {
 public:
  virtual ~DeviceInfoDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t DeviceInfoDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::disable_device_info();

  DeviceInfoDisableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class PushDeviceInfoEnableHandler : public MessageHandler {
 public:
  virtual ~PushDeviceInfoEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t PushDeviceInfoEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::enable_push_device_info();

  PushDeviceInfoEnableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class PushDeviceInfoDisableHandler : public MessageHandler {
 public:
  virtual ~PushDeviceInfoDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t PushDeviceInfoDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::disable_push_device_info();

  PushDeviceInfoDisableResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class AnnounceTokenChangedEnableHandler : public MessageHandler {
 public:
  virtual ~AnnounceTokenChangedEnableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    Config::enable_announce_token_changed();

    AnnounceTokenChangedEnableResponse response;
    err_t not_ret = response.SendTo(socket);
    assert(not_ret == ZISYNC_SUCCESS);
    return ZISYNC_SUCCESS;
  }
};

class AnnounceTokenChangedDisableHandler : public MessageHandler {
 public:
  virtual ~AnnounceTokenChangedDisableHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    Config::disable_announce_token_changed();

    AnnounceTokenChangedDisableResponse response;
    err_t not_ret = response.SendTo(socket);
    assert(not_ret == ZISYNC_SUCCESS);
    return ZISYNC_SUCCESS;
  }
};

class SyncAddHandler : public MessageHandler {
 public:
  virtual ~SyncAddHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSyncAddRequest request_msg_;
};

err_t SyncAddHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start Syncadd");

  SyncInfo sync_info;
  if (request_msg_.has_sync_uuid()) {
    NormalSync sync;
    sync.set_uuid(request_msg_.sync_uuid());
    err_t zisync_ret = sync.Create(request_msg_.sync_name()); 
    if (zisync_ret != ZISYNC_SUCCESS) {
      return zisync_ret;
    }
    IssuePushSyncInfo(sync.id());
  } else {
    err_t zisync_ret = kernel->CreateSync(
        request_msg_.sync_name().c_str(), &sync_info);
    if (zisync_ret != ZISYNC_SUCCESS) {
      return zisync_ret;
    }
  }
  SyncAddResponse response;
  MsgSyncInfo *msg_sync_info = response.mutable_response()->mutable_sync();
  msg_sync_info->set_id(sync_info.sync_id);
  msg_sync_info->set_name(request_msg_.sync_name());
  msg_sync_info->set_uuid(sync_info.sync_uuid);
  msg_sync_info->set_last_sync(sync_info.last_sync);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End Syncadd");
  return ZISYNC_SUCCESS;
}

class SyncDelHandler : public MessageHandler {
 public:
  virtual ~SyncDelHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSyncDelRequest request_msg_;
};

err_t SyncDelHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start SyncDel");
  err_t zisync_ret = kernel->DestroySync(
      request_msg_.sync_id()); 
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  SyncDelResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End SyncDel");
  return ZISYNC_SUCCESS;
}

class TreeAddHandler : public MessageHandler {
 public:
  virtual ~TreeAddHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgTreeAddRequest request_msg_;
};

err_t TreeAddHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start TreeAdd");
  SyncTree tree;
  if (request_msg_.has_tree_uuid()) {
    tree.set_uuid(request_msg_.tree_uuid());
  }
  err_t zisync_ret = tree.Create(
      request_msg_.sync_id(), request_msg_.tree_root().c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  IssuePushTreeInfo(tree.id());
  IssueRefresh(tree.id());
  IssueSyncWithLocalTree(request_msg_.sync_id(), tree.id());

  TreeAddResponse response;
  MsgTreeInfo *msg_tree_info = response.mutable_response()->mutable_tree();
  msg_tree_info->set_id(tree.id());
  msg_tree_info->set_root(tree.root());
  msg_tree_info->set_uuid(tree.uuid());
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End TreeAdd");
  return ZISYNC_SUCCESS;
}

class TreeDelHandler : public MessageHandler {
 public:
  virtual ~TreeDelHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgTreeDelRequest request_msg_;
};

err_t TreeDelHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start TreeDel");
  TreeInfo tree_info;
  err_t zisync_ret = kernel->DestroyTree(
      request_msg_.tree_id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  TreeDelResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End TreeDel");
  return ZISYNC_SUCCESS;
}

class RefreshOuterHandler : public MessageHandler {
 public:
  virtual ~RefreshOuterHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgRefreshRequest request_msg_;
};

err_t RefreshOuterHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start RefreshOuter");
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_refresh_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  RefreshRequest request;
  request.mutable_request()->set_tree_id(request_msg_.tree_id());
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
  RefreshResponse response;
  response.mutable_response()->set_tree_id(request_msg_.tree_id());
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End RefreshOuter");
  return ZISYNC_SUCCESS;
}

class SyncOuterHandler : public MessageHandler {
 public:
  virtual ~SyncOuterHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSyncRequest request_msg_;
};

err_t SyncOuterHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start SyncOuter");
  zs::AbortAddSyncTree(request_msg_.local_tree_id(), 
                       request_msg_.remote_tree_id());
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_sync_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  SyncRequest request;
  request.mutable_request()->set_local_tree_id(request_msg_.local_tree_id());
  request.mutable_request()->set_remote_tree_id(request_msg_.remote_tree_id());
  request.mutable_request()->set_is_manual(request_msg_.is_manual());
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
  SyncResponse response;

  response.mutable_response()->set_local_tree_id(request_msg_.local_tree_id());
  response.mutable_response()->set_remote_tree_id(request_msg_.remote_tree_id());
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End SyncOuter");
  return ZISYNC_SUCCESS;
}

class RemoteTreeAddHandler : public MessageHandler {
 public:
  virtual ~RemoteTreeAddHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgRemoteTreeAddRequest request_msg_;
};

err_t RemoteTreeAddHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start RemoteTreeAdd");
  TreeInfo tree_info;
  err_t zisync_ret = RemoteTreeAdd(
      request_msg_.tree_uuid(), request_msg_.device_uuid(), 
      request_msg_.sync_uuid(), &tree_info);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_WARNING("RemoteTreeAdd fail : %s", zisync_strerror(zisync_ret));
  }
  RemoteTreeAddResponse response;
  MsgTreeInfo *msg_tree_info = response.mutable_response()->mutable_tree();
  msg_tree_info->set_id(tree_info.tree_id);
  msg_tree_info->set_root(tree_info.tree_root);
  msg_tree_info->set_uuid(tree_info.tree_uuid);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End RemoteTreeAdd");
  return ZISYNC_SUCCESS;
}

class TreeShowHandler : public MessageHandler {
 public:
  virtual ~TreeShowHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t TreeShowHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start TreeShow");
  IContentResolver *resolver = GetContentResolver();
  const char* tree_projs[] = {
    TableTree::COLUMN_ROOT,
    TableTree::COLUMN_UUID,
    TableTree::COLUMN_DEVICE_ID,
    TableTree::COLUMN_ID,
  };
  const char* device_projs[] = {
    TableDevice::COLUMN_UUID,
  };
  TreeShowResponse response;

  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d", TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  while (tree_cursor->MoveToNext()) {
    MsgTreeInfo *tree = response.mutable_response()->add_trees();
    tree->set_root(tree_cursor->GetString(0));
    tree->set_uuid(tree_cursor->GetString(1));
    tree->set_id(tree_cursor->GetInt32(3));
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
            "%s = %" PRId32, TableDevice::COLUMN_ID, tree_cursor->GetInt32(2)));
    if (device_cursor->MoveToNext()) {
      tree->set_device_uuid(device_cursor->GetString(0));
    } else {
      assert(0);
    }
  }
  
  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End TreeShow");
  return ZISYNC_SUCCESS;
}

class SyncShowHandler : public MessageHandler {
 public:
  virtual ~SyncShowHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t SyncShowHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start SyncShow");

  QuerySyncInfoResult result;
  kernel->QuerySyncInfo(&result);
  SyncShowResponse response;

  for (auto iter = result.sync_infos.begin(); iter != result.sync_infos.end();
       iter ++) {
      MsgSyncInfo *sync_info = response.mutable_response()->add_syncs();
      sync_info->set_id(iter->sync_id);
      sync_info->set_name(iter->sync_name);
      sync_info->set_uuid(iter->sync_uuid);
      sync_info->set_last_sync(iter->last_sync);
      sync_info->set_type(iter->is_share ? ST_SHARED : ST_NORMAL);
      if (iter->sync_perm == SYNC_PERM_DISCONNECT_UNRECOVERABLE) {
        iter->sync_perm = TableSync::PERM_DISCONNECT;
      } else if (iter->sync_perm == SYNC_PERM_DISCONNECT_RECOVERABLE) {
        iter->sync_perm = TableSync::PERM_TOKEN_DIFF;
      }
      sync_info->set_perm(SyncPermToMsgSyncPerm(iter->sync_perm));
      for (auto tree_iter = iter->trees.begin();
           tree_iter != iter->trees.end(); tree_iter ++) {
        MsgTreeInfo *tree_info = sync_info->add_trees();
        tree_info->set_id(tree_iter->tree_id);
        tree_info->set_uuid(tree_iter->tree_uuid);
        tree_info->set_root(tree_iter->tree_root);
        tree_info->set_is_local(tree_iter->is_local);
      }
    }

  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End SyncShow");
  return ZISYNC_SUCCESS;
}

class AddFavoriteHandler : public MessageHandler {
 public:
  virtual ~AddFavoriteHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
  
 private:
  MsgAddFavoriteRequest request_msg_;
};

err_t AddFavoriteHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start AddFavorite");
  err_t zisync_ret = kernel->AddFavorite(
      request_msg_.tree_id(), request_msg_.path().c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  AddFavoriteResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("End AddFavorite");
  return ZISYNC_SUCCESS;
}

class TestFindHandler : public MessageHandler {
 public:
  virtual ~TestFindHandler() {
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
  MsgTestFindRequest request_msg_;
};

err_t TestFindHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start TestFind");
  IContentResolver* resolver = GetContentResolver();

  const char *tree_projs[] = { TableTree::COLUMN_SYNC_ID, 
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s' AND %s = %d", TableTree::COLUMN_UUID, 
          request_msg_.tree_uuid().c_str(),
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!tree_cursor->MoveToNext()) {
    return  ZISYNC_ERROR_TREE_NOENT;
  }

  int32_t sync_id = tree_cursor->GetInt32(0);
  const char *sync_projs[] = {
    TableSync::COLUMN_UUID,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
          "%s = %" PRId32 " AND %s = %d", TableSync::COLUMN_ID, sync_id,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  if (!sync_cursor->MoveToNext()) {
    // if sync_id not found, it must be moved by another thread, then the tree
    // should have been moved 
    return ZISYNC_ERROR_TREE_NOENT;
  }
  const char *sync_uuid = sync_cursor->GetString(0);
  if (request_msg_.sync_uuid() != sync_uuid) {
    return ZISYNC_ERROR_SYNCDIR_MISMATCH;
  }

  TestFindResponse response;

  const char *vclock_tree_projs[] = {
    TableTree::COLUMN_UUID, 
  };
  unique_ptr<ICursor2> vclock_tree_cursor(resolver->Query(
          TableTree::URI, vclock_tree_projs, ARRAY_SIZE(vclock_tree_projs),
          "%s = %" PRId32 " AND %s != '%s'", TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_UUID, request_msg_.tree_uuid().c_str()));
  response.mutable_response()->add_uuids(request_msg_.tree_uuid());
  while(vclock_tree_cursor->MoveToNext()) {
    response.mutable_response()->add_uuids(vclock_tree_cursor->GetString(0));
  }

  const char *file_projs[] = {
    TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
    TableFile::COLUMN_STATUS, TableFile::COLUMN_MTIME, 
    TableFile::COLUMN_LENGTH, TableFile::COLUMN_USN, 
    TableFile::COLUMN_SHA1, TableFile::COLUMN_UNIX_ATTR, 
    TableFile::COLUMN_ANDROID_ATTR, 
    TableFile::COLUMN_WIN_ATTR, TableFile::COLUMN_LOCAL_VCLOCK,
    TableFile::COLUMN_REMOTE_VCLOCK,
  };

  Selection selection("%s > %" PRId64, TableFile::COLUMN_USN, 
                      request_msg_.since());
  string sort;
  StringFormat(&sort, "%s LIMIT %" PRId32, TableFile::COLUMN_USN, 
               request_msg_.limit());
  unique_ptr<ICursor2> file_cursor(resolver->sQuery(
          TableFile::GenUri(request_msg_.tree_uuid().c_str()), 
          file_projs, ARRAY_SIZE(file_projs), &selection, sort.c_str())); 
  while (file_cursor->MoveToNext()) {
    MsgStat *file_stat = response.mutable_response()->add_stats();
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
         i < response.response().uuids_size(); i ++) {
      file_stat->add_vclock(vclock.at(i));
    }
  }

  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End TestFind");
  return ZISYNC_SUCCESS;
}

class ListSyncHandler : public MessageHandler {
 public:
  virtual ~ListSyncHandler() {
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
  MsgListSyncRequest request_msg_;
};

err_t ListSyncHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start ListSync");
  ListSyncResult result;
  err_t zisync_ret = kernel->ListSync(
      request_msg_.sync_id(), request_msg_.path(), &result);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  ListSyncResponse response;
  MsgListSyncResponse *msg_response = response.mutable_response();
  for (auto iter = result.files.begin(); iter != result.files.end();
       iter ++) {
    MsgFileMeta *file_meta = msg_response->add_files();
    file_meta->set_name(iter->name);
    file_meta->set_type(
        iter->type == FILE_META_TYPE_REG ? FT_REG : FT_DIR);
    file_meta->set_mtime(iter->mtime);
    file_meta->set_length(iter->length);
    file_meta->set_has_download_cache(iter->has_download_cache);
    file_meta->set_cache_path(iter->cache_path);
  }
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End ListSync");
  return ZISYNC_SUCCESS;
}

class SetAccountHandler : public MessageHandler {
 public:
  virtual ~SetAccountHandler() {
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
  MsgSetAccountRequest request_msg_;
};

err_t SetAccountHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start SetAccount");
  err_t zisync_ret = kernel->SetAccount(
      request_msg_.username().c_str(), request_msg_.password().c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  SetAccountResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End SetAccount");
  return ZISYNC_SUCCESS;
}

class IssueDeviceInfoOuterHandler : public MessageHandler {
 public:
  virtual ~IssueDeviceInfoOuterHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgIssueDeviceInfoRequest request_msg_;
};

err_t IssueDeviceInfoOuterHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssueDeviceInfoRequest request;
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssueDeviceInfoResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class IssuePushDeviceInfoOuterHandler : public MessageHandler {
 public:
  virtual ~IssuePushDeviceInfoOuterHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgIssuePushDeviceInfoRequest request_msg_;
};

err_t IssuePushDeviceInfoOuterHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssuePushDeviceInfoRequest request;
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssuePushDeviceInfoResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class StartupDiscoverDeviceHandler : public MessageHandler {
 public:
  virtual ~StartupDiscoverDeviceHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgStartupDiscoverDeviceRequest request_msg_;
};

err_t StartupDiscoverDeviceHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  int32_t discover_id;
  err_t zisync_ret = kernel->StartupDiscoverDevice(
      request_msg_.sync_id(), &discover_id);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  StartupDiscoverDeviceResponse response;
  response.mutable_response()->set_id(discover_id);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class ShutdownDiscoverDeviceHandler : public MessageHandler {
 public:
  virtual ~ShutdownDiscoverDeviceHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgShutdownDiscoverDeviceRequest request_msg_;
};

err_t ShutdownDiscoverDeviceHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = kernel->ShutdownDiscoverDevice(
      request_msg_.id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  ShutdownDiscoverDeviceResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class GetDiscoveredDeviceHandler : public MessageHandler {
 public:
  virtual ~GetDiscoveredDeviceHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgGetDiscoveredDeviceRequest request_msg_;
};

err_t GetDiscoveredDeviceHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  DiscoverDeviceResult result;
  err_t zisync_ret = kernel->GetDiscoveredDevice(
      request_msg_.id(), &result);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  GetDiscoveredDeviceResponse response;
  response.mutable_response()->set_is_done(result.is_done);
  for (auto iter = result.devices.begin(); iter != result.devices.end(); 
       iter ++) {
    MsgDiscoverDeviceInfo *device = response.mutable_response()->add_devices();
    device->set_id(iter->device_id);
    device->set_name(iter->device_name);
  }
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class TestShareSyncHandler : public MessageHandler {
 public:
  virtual ~TestShareSyncHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgTestShareSyncRequest request_msg_;
};

err_t TestShareSyncHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = kernel->ShareSync(
      request_msg_.discover_id(), request_msg_.device_id(), 
      MsgSyncPermToSyncPerm(request_msg_.sync_perm()));
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  TestShareSyncResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class StartupDownloadHandler : public MessageHandler {
 public:
  virtual ~StartupDownloadHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgStartupDownloadRequest request_msg_;
};

err_t StartupDownloadHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  int32_t task_id;
  string target_path = request_msg_.target_path();
  err_t zisync_ret = kernel->StartupDownload(
      request_msg_.sync_id(), request_msg_.relative_path(), 
      &target_path, &task_id);

  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  StartupDownloadResponse response;
  response.mutable_response()->set_task_id(task_id);
  response.mutable_response()->set_target_path(target_path);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class ShutdownDownloadHandler : public MessageHandler {
 public:
  virtual ~ShutdownDownloadHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgShutdownDownloadRequest request_msg_;
};

err_t ShutdownDownloadHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = kernel->ShutdownDownload(request_msg_.task_id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  ShutdownDownloadResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class BackupAddHandler : public MessageHandler {
 public:
  virtual ~BackupAddHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgBackupAddRequest request_msg_;
};

err_t BackupAddHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  BackupInfo backup_info;
  err_t zisync_ret = kernel->CreateBackup(
      request_msg_.name().c_str(), request_msg_.root().c_str(), 
      &backup_info);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  BackupAddResponse response;
  response.mutable_response()->set_id(backup_info.backup_id);

  IContentResolver *resolver = GetContentResolver();
  const char *sync_projs[] = {
    TableSync::COLUMN_UUID,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
          "%s = %d", TableSync::COLUMN_ID, backup_info.backup_id));
  assert(sync_cursor->MoveToNext());
  response.mutable_response()->set_uuid(sync_cursor->GetString(0));
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class BackupDelHandler : public MessageHandler {
 public:
  virtual ~BackupDelHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgBackupDelRequest request_msg_;
};

err_t BackupDelHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  BackupInfo backup_info;
  err_t zisync_ret = kernel->DestroyBackup(request_msg_.id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  BackupDelResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class BackupShowHandler : public MessageHandler {
 public:
  virtual ~BackupShowHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t BackupShowHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  IContentResolver* resolver = GetContentResolver();

  /* @TODO maybe we can use view or join */
  const char* sync_projs[] = {
    TableSync::COLUMN_ID, TableSync::COLUMN_NAME, TableSync::COLUMN_UUID,
    TableSync::COLUMN_LAST_SYNC,
  };

  BackupShowResponse response;
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %d AND %s = %" PRId32,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP));
  while (sync_cursor->MoveToNext()) {
    MsgBackupInfo *backup = response.mutable_response()->add_backups();
    backup->set_id(sync_cursor->GetInt32(0));
    backup->set_name(sync_cursor->GetString(1));
    backup->set_uuid(sync_cursor->GetString(2));
    backup->set_last_sync(sync_cursor->GetInt64(3));

    std::vector<unique_ptr<Tree>> trees;
    Tree::QueryBySyncIdWhereStatusNormal(backup->id(), &trees);
    for (auto iter = trees.begin(); iter != trees.end(); iter ++) {
      MsgTreeInfo *tree_info = backup->add_trees();
      tree_info->set_id((*iter)->id());
      tree_info->set_uuid((*iter)->uuid());
      tree_info->set_root((*iter)->root());
      tree_info->set_is_local((*iter)->IsLocalTree());
    }
  }

  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class AddBackupTargetDeviceHandler : public MessageHandler {
 public:
  virtual ~AddBackupTargetDeviceHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgAddBackupTargetDeviceRequest request_msg_;
};

err_t AddBackupTargetDeviceHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  BackupInfo backup_info;
  const char *backup_root = request_msg_.has_backup_root() ?
      request_msg_.backup_root().c_str() : NULL;
  TreeInfo remote_backup_tree;
  err_t zisync_ret = kernel->AddBackupTargetDevice(
      request_msg_.backup_id(), request_msg_.device_id(), 
      &remote_backup_tree, backup_root);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("AddBackupTargetDevice fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  AddBackupTargetDeviceResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class DelBackupTargetDeviceHandler : public MessageHandler {
 public:
  virtual ~DelBackupTargetDeviceHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgDelBackupTargetDeviceRequest request_msg_;
};

err_t DelBackupTargetDeviceHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  BackupInfo backup_info;
  err_t zisync_ret = kernel->DelBackupTargetDevice(
      request_msg_.backup_id(), request_msg_.device_id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  DelBackupTargetDeviceResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class DelBackupTargetHandler : public MessageHandler {
 public:
  virtual ~DelBackupTargetHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgDelBackupTargetRequest request_msg_;
};

err_t DelBackupTargetHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  BackupInfo backup_info;
  err_t zisync_ret = kernel->DelBackupTarget(
      request_msg_.dst_tree_id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  DelBackupTargetResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SetBackupRootHandler : public MessageHandler {
 public:
  virtual ~SetBackupRootHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSetBackupRootRequest request_msg_;
};

err_t SetBackupRootHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  BackupInfo backup_info;
  err_t zisync_ret = kernel->SetBackupRoot(request_msg_.root().c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetBackupRootResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SetTestPlatformHandler : public MessageHandler {
 public:
  virtual ~SetTestPlatformHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSetTestPlatformRequest request_msg_;
};

err_t SetTestPlatformHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::set_test_platform(
      MsgDeviceTypeToPlatform(request_msg_.device_type()));

  SetTestPlatformResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class ClearTestPlatformHandler : public MessageHandler {
 public:
  virtual ~ClearTestPlatformHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t ClearTestPlatformHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Config::clear_test_platform();

  ClearTestPlatformResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SetSyncModeHandler : public MessageHandler {
 public:
  virtual ~SetSyncModeHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSetSyncModeRequest request_msg_;
};

err_t SetSyncModeHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = kernel->SetSyncMode(
      request_msg_.local_tree_id(), request_msg_.remote_tree_id(),
      MsgSyncModeToSyncMode(request_msg_.sync_mode()), 
      request_msg_.sync_time());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetSyncModeResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class GetSyncModeHandler : public MessageHandler {
 public:
  virtual ~GetSyncModeHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgGetSyncModeRequest request_msg_;
};

err_t GetSyncModeHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  int sync_mode;
  int32_t sync_time;
  err_t zisync_ret = kernel->GetSyncMode(
      request_msg_.local_tree_id(), request_msg_.remote_tree_id(),
      &sync_mode, &sync_time);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  GetSyncModeResponse response;
  response.mutable_response()->set_sync_mode(
      SyncModeToMsgSyncMode(sync_mode));
  response.mutable_response()->set_sync_time(sync_time);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class QueryTreePairStatusHandler : public MessageHandler {
 public:
  virtual ~QueryTreePairStatusHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgQueryTreePairStatusRequest request_msg_;
};

err_t QueryTreePairStatusHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  TreePairStatus status;
  err_t zisync_ret = kernel->QueryTreePairStatus(
      request_msg_.local_tree_id(), request_msg_.remote_tree_id(),
      &status);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  QueryTreePairStatusResponse response;
  response.mutable_response()->set_num_file_to_upload(
      status.static_num_file_to_upload);
  response.mutable_response()->set_num_file_to_download(
      status.static_num_file_to_download);
  response.mutable_response()->set_num_file_consistent(
      status.static_num_file_consistent);
  response.mutable_response()->set_num_byte_to_upload(
      status.static_num_byte_to_upload);
  response.mutable_response()->set_num_byte_to_download(
      status.static_num_byte_to_download);
  response.mutable_response()->set_num_byte_consistent(
      status.static_num_byte_consistent);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SetTreeRootHandler : public MessageHandler {
 public:
  virtual ~SetTreeRootHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSetTreeRootRequest request_msg_;
};

err_t SetTreeRootHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  TreePairStatus status;
  err_t zisync_ret = kernel->SetTreeRoot(
      request_msg_.tree_id(), request_msg_.tree_root().c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetTreeRootResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SetShareSyncPermHandler : public MessageHandler {
 public:
  virtual ~SetShareSyncPermHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSetShareSyncPermRequest request_msg_;
};

err_t SetShareSyncPermHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  TreePairStatus status;
  err_t zisync_ret;
  if (request_msg_.device_id() == TableDevice::LOCAL_DEVICE_ID) {
    assert(request_msg_.sync_perm() == SP_DISCONNECT);
    zisync_ret = kernel->
        DisconnectShareSync(request_msg_.sync_id());
  } else {
    zisync_ret = kernel->SetShareSyncPerm(
        request_msg_.device_id(), request_msg_.sync_id(), 
        MsgSyncPermToSyncPerm(request_msg_.sync_perm()));
  }
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetShareSyncPermResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class GetShareSyncPermHandler : public MessageHandler {
 public:
  virtual ~GetShareSyncPermHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgGetShareSyncPermRequest request_msg_;
};

err_t GetShareSyncPermHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  TreePairStatus status;
  int32_t sync_perm;
  err_t zisync_ret = kernel->GetShareSyncPerm(
      request_msg_.device_id(), request_msg_.sync_id(), &sync_perm);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  GetShareSyncPermResponse response;
  response.mutable_response()->set_sync_perm(
      SyncPermToMsgSyncPerm(sync_perm));
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SetDownloadCacheVolumeHandler : public MessageHandler {
 public:
  virtual ~SetDownloadCacheVolumeHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgSetDownloadCacheVolumeRequest request_msg_;
};

err_t SetDownloadCacheVolumeHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = kernel->SetDownloadCacheVolume(
      request_msg_.volume());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetDownloadCacheVolumeResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class GetDownloadCacheAmountHandler : public MessageHandler {
 public:
  virtual ~GetDownloadCacheAmountHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
};

err_t GetDownloadCacheAmountHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  int64_t amount = kernel->GetDownloadCacheAmount();

  GetDownloadCacheAmountResponse response;
  response.mutable_response()->set_amount(amount);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class GetDownloadCacheRootHandler : public MessageHandler {
 public:
  virtual ~GetDownloadCacheRootHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
};

err_t GetDownloadCacheRootHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  GetDownloadCacheRootResponse response;
  response.mutable_response()->set_root(Config::download_cache_dir());
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class CleanUpDownloadCacheHandler : public MessageHandler {
 public:
  virtual ~CleanUpDownloadCacheHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
};

err_t CleanUpDownloadCacheHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = kernel->CleanUpDownloadCache();
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  CleanUpDownloadCacheResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class QueryDownloadStatusHandler : public MessageHandler {
 public:
  virtual ~QueryDownloadStatusHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgQueryDownloadStatusRequest request_msg_;
};

err_t QueryDownloadStatusHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  DownloadStatus status;
  err_t zisync_ret = kernel->QueryDownloadStatus(
      request_msg_.task_id(), &status);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  QueryDownloadStatusResponse response;
  response.mutable_response()->set_status(
      static_cast<MsgUpDownloadStatus>(status.status));
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class StartupUploadHandler : public MessageHandler {
 public:
  virtual ~StartupUploadHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgStartupUploadRequest request_msg_;
};

err_t StartupUploadHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  int32_t task_id;
  string target_path;
  err_t zisync_ret = GetZiSyncKernel("actual")->StartupUpload(
      request_msg_.sync_id(), request_msg_.relative_path(),
      request_msg_.real_path(), &task_id);

  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  StartupUploadResponse response;
  response.mutable_response()->set_task_id(task_id);
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class ShutdownUploadHandler : public MessageHandler {
 public:
  virtual ~ShutdownUploadHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgShutdownUploadRequest request_msg_;
};

err_t ShutdownUploadHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = GetZiSyncKernel("actual")->ShutdownUpload(
      request_msg_.task_id());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  ShutdownUploadResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class QueryUploadStatusHandler : public MessageHandler {
 public:
  virtual ~QueryUploadStatusHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgQueryUploadStatusRequest request_msg_;
};

err_t QueryUploadStatusHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  UploadStatus status;
  err_t zisync_ret = zs::GetZiSyncKernel("actual")->QueryUploadStatus(
      request_msg_.task_id(), &status);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  QueryUploadStatusResponse response;
  response.mutable_response()->set_status(
      static_cast<MsgUpDownloadStatus>(status.status));
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class QueryHistoryInfoHandler : public MessageHandler {
  public:
   virtual ~QueryHistoryInfoHandler() {}
   virtual ::google::protobuf::Message* mutable_msg() {
     return &request_msg_;
   }

   virtual err_t HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
   MsgQueryHistoryInfoRequest request_msg_;
};

err_t QueryHistoryInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  QueryHistoryResult queryResult;
  std::vector<History> &histories = queryResult.histories;

  int32_t limit = request_msg_.limit();
  err_t zisync_ret = zs::GetZiSyncKernel("actual")->QueryHistoryInfo(
      &queryResult, 0, limit);
  assert(zisync_ret == ZISYNC_SUCCESS);

  QueryHistoryInfoResponse response;
  ZSLOG_INFO("---------------------query history start------------------");
  for (auto it = histories.begin(); it != histories.end(); it++) {
    MsgHistoryInfo *history = response.mutable_response()->add_historys();
    assert(history != NULL);

    history->set_dev_name(it->modifier);
    history->set_frompath(it->frompath);
    history->set_topath(it->topath);
    history->set_time_stamp(it->time_stamp);
    history->set_code(it->code);
    history->set_error(it->error);
    ZSLOG_INFO("%s, %s, %s, %" PRId64, it->modifier.c_str(), it->frompath.c_str(),
               it->topath.c_str(), it->time_stamp);
  }
  ZSLOG_INFO("---------------------query history end------------------");
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return zisync_ret;
}

class PermissionVerifyHandler : public MessageHandler {
 public:
  virtual ~PermissionVerifyHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);
 private:
  MsgPermissionVerifyRequest request_msg_;
};

err_t PermissionVerifyHandler::HandleMessage(
    const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  std::string key = request_msg_.perm_key();
  err_t zisync_ret = zs::GetZiSyncKernel("actual")->Verify(key);
  assert(zisync_ret == ZISYNC_SUCCESS);

  PermissionVerifyResponse response;
  ZSLOG_INFO("key(%s)==>verify status(%d)", key.c_str(), zisync_ret);
  response.mutable_response()->set_status(zisync_ret);

  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class QueryPermissionsHandler : public MessageHandler {
 public:
  virtual ~QueryPermissionsHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);

 private:
  MsgQueryPermissionsRequest request_msg_;
};

err_t QueryPermissionsHandler::HandleMessage(
    const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  std::map<zs::UserPermission_t, int32_t> perms;
  for (int i = 0; i < request_msg_.perms_size(); i++) {
    perms[static_cast<zs::UserPermission_t>(request_msg_.perms(i))] = 0;
  }

  err_t zisync_ret = zs::GetZiSyncKernel("actual")->QueryUserPermission(&perms);
  assert(zisync_ret == ZISYNC_SUCCESS);

  QueryPermissionsResponse response;
  for (auto it = perms.begin(); it != perms.end(); it++) {
    MsgPermission *perm= response.mutable_response()->add_perms();
    assert(perm != NULL);
    perm->set_perm(it->first);
    perm->set_constraint(it->second);
  }

  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class SetMacTokenInDbHandler : public MessageHandler {
 public:
  virtual ~SetMacTokenInDbHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);

 private:
  MsgSetMacAddressInDbRequest request_msg_;
};

err_t SetMacTokenInDbHandler::HandleMessage(
    const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  std::string mactoken = request_msg_.mac_token();

  err_t zisync_ret = zs::GetLicences()->SaveMacAddress(mactoken);
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetMacAddressInDbResponse response;
  response.mutable_response()->set_status(ZISYNC_SUCCESS);
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class SetExpiredTimeInDbHandler : public MessageHandler {
 public:
  virtual ~SetExpiredTimeInDbHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);

 private:
  MsgSetExpiredTimeInDbRequest request_msg_;
};

err_t SetExpiredTimeInDbHandler::HandleMessage(
    const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  zs::TimeCode code = request_msg_.code();
  int64_t expired_time = request_msg_.expired_time();

  err_t zisync_ret = ZISYNC_SUCCESS;
  switch (code) {
    case zs::EXPIRED_TIME:
      zisync_ret = zs::GetLicences()->SaveExpiredTime(expired_time);
      break;
    case zs::CREATED_TIME:
      zisync_ret = zs::GetLicences()->SaveCreatedTime(expired_time);
      break;
    case zs::LAST_CONTACT_TIME:
      zisync_ret = zs::GetLicences()->SaveLastContactTime(expired_time);
      break;
    case zs::EXPIRED_OFFLINE_TIME:
      zisync_ret = zs::GetLicences()->SaveExpiredOfflineTime(expired_time);
      break;
    default:
      assert(0);
      break;
  }
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetExpiredTimeInDbResponse response;
  response.mutable_response()->set_status(ZISYNC_SUCCESS);
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class ClearPermissionsHandler: public MessageHandler {
 public:
  virtual ~ClearPermissionsHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);

 private:
  MsgClearPermissionsRequest request_msg_;
};

err_t ClearPermissionsHandler::HandleMessage(
    const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    std::string temp = request_msg_.key();
    for (auto it = temp.begin(); it != temp.end(); ) {
      if (*it == '-') {
        it = temp.erase(it);
      } else {
        ++it;
      }
    }
    response.set_keycode(temp);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    err_t zisync_ret = zs::GetPermission()->Reset(content, temp);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  
  {
    ClearPermissionsResponse response;
    response.mutable_response()->set_status(ZISYNC_SUCCESS);
    err_t zisync_ret = response.SendTo(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  return ZISYNC_SUCCESS;
}

class QueryVerifyStatusHandler : public MessageHandler {
 public:
  virtual ~QueryVerifyStatusHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);

 private:
  MsgQueryVerifyStatusRequest request_msg_;
};

err_t QueryVerifyStatusHandler::HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  VerifyStatus_t status = GetZiSyncKernel("actual")->VerifyStatus();

  QueryVerifyStatusResponse response;
  VerifyStatusCode code;
  switch (status) {
    case VS_OK:
      code = VSC_OK;
      break;
    case VS_WAITING:
      code = VSC_WAITING;
      break;
    case VS_DEVICE_NOT_EXISTS:
      code = VSC_DEVICE_NOT_EXISTS;
      break;
    case VS_DEVICE_NOT_BIND:
      code = VSC_DEVICE_NOT_BIND;
      break;
    case VS_INVALID_KEY_CODE:
      code = VSC_INVALID_KEY_CODE;
      break;
    case VS_LIMITED_KEY_BIND:
      code = VSC_LIMITED_KEY_BIND;
      break;
    case VS_KEY_EXPIRED:
      code = VSC_EXPIRED_TIME;
      break;
    case VS_PERMISSION_DENY:
      code = VSC_PERMISSION_DENY;
      break;
    case VS_UNKNOW_ERROR:
      code = VSC_UNKNOW_ERROR;
      break;
    case VS_NETWORK_ERROR:
      code = VSC_NETWORK_ERROR;
      break;
    default:
      assert(0);
  }
  response.mutable_response()->set_status(code);
  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class QueryVerifyIdentifyInfoHandler : public MessageHandler {
 public:
  virtual ~QueryVerifyIdentifyInfoHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata);

 private:
  MsgQueryVerifyIdentifyInfoRequest request_msg_;
};
err_t QueryVerifyIdentifyInfoHandler::HandleMessage(
      const ZmqSocket &socket, const MsgHead &head, void *userdata) {
  struct LicencesInfo licences;
  err_t zisync_ret = GetZiSyncKernel(
      "actual")->QueryLicencesInfo(&licences);
  assert(zisync_ret == ZISYNC_SUCCESS);

  QueryVerifyIdentifyInfoResponse response;
  LicencesStatus status;
  switch (licences.status) {
    case LS_OK:
      status = LSP_OK;
      break;
    case LS_EXPIRED_TIME:
      status = LSP_EXPIRED_TIME;
      break;
    case LS_EXPIRED_OFFLINE_TIME:
      status = LSP_EXPIRED_OFFLINE_TIME;
      break;
    case LS_INVALID:
      status = LSP_INVALID;
      break;
    default:
      assert(0);
  }
  
  response.mutable_response()->set_status(status);
  response.mutable_response()->set_left_time(licences.left_time);
  response.mutable_response()->set_last_contact_time(
      licences.last_contact_time);
  response.mutable_response()->set_expired_time(licences.expired_time);
  response.mutable_response()->set_expired_offline_time(
      licences.expired_offline_time);
  response.mutable_response()->set_created_time(licences.created_time);
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret);

  return ZISYNC_SUCCESS;
}

class SyncControlerHandler : public MessageHandler {
  public:
   virtual ~SyncControlerHandler() {}
   virtual ::google::protobuf::Message* mutable_msg() {
     return &request_msg_;
   }

   virtual err_t HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
   MsgSyncControlerRequest request_msg_;
};

err_t SyncControlerHandler::HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = ZISYNC_SUCCESS;
  if (request_msg_.sync_behaviour() == ENABLE_SYNC) {
    zisync_ret = GetZiSyncKernel("actual")->EnableSync(request_msg_.tree_id());
    assert(zisync_ret == ZISYNC_SUCCESS);
  } else if (request_msg_.sync_behaviour() == DISABLE_SYNC) {
    zisync_ret = GetZiSyncKernel("actual")->DisableSync(request_msg_.tree_id());
    assert(zisync_ret == ZISYNC_SUCCESS);
  } else {
    ZSLOG_ERROR("Invalid sync behaviour(%d).", request_msg_.sync_behaviour());
    assert(0);
  }

  SyncControlerResponse response;
  response.mutable_response()->set_status(zisync_ret);
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}
class VerifyBindHandler: public MessageHandler {
  public:
   virtual ~VerifyBindHandler() {}
   virtual ::google::protobuf::Message* mutable_msg() {
     return &request_msg_;
   }

   virtual err_t HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
   MsgVerifyBindRequest request_msg_;
};
err_t VerifyBindHandler::HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = GetZiSyncKernel("actual")->Bind(request_msg_.key());

  VerifyBindResponse response;
  response.mutable_response()->set_status(zisync_ret);
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}
class VerifyUnbindHandler : public MessageHandler {
  public:
   virtual ~VerifyUnbindHandler() {}
   virtual ::google::protobuf::Message* mutable_msg() {
     return &request_msg_;
   }

   virtual err_t HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
   MsgVerifyUnbindRequest request_msg_;
};
err_t VerifyUnbindHandler::HandleMessage(
       const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = GetZiSyncKernel("actual")->Unbind();
  VerifyUnbindResponse response;
  response.mutable_response()->set_status(zisync_ret);
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class SetSyncPermHandler : public MessageHandler {
  public:
    ~SetSyncPermHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }

    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgSetSyncPermRequest request_msg_;
};

err_t SetSyncPermHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(request_msg_.sync_id()));
  assert(sync);

  SetSyncPermResponse response;
  response.mutable_response()->set_origin_perm(
      SyncPermToMsgSyncPerm(sync->perm()));

  zisync_ret = GetZiSyncKernel("actual")->SetSyncPerm(request_msg_.sync_id()
      , MsgSyncPermToSyncPerm(request_msg_.perm()));
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class SetDiscoverPortHandler : public MessageHandler {
  public:
    ~SetDiscoverPortHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }

    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgSetDiscoverPortRequest request_msg_;
};

err_t SetDiscoverPortHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = kernel->SetDiscoverPort(request_msg_.new_port());
  assert(zisync_ret == ZISYNC_SUCCESS);
  SetDiscoverPortResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}


class SetRoutePortHandler : public MessageHandler {
  public:
    ~SetRoutePortHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }

    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgSetRoutePortRequest request_msg_;
};

err_t SetRoutePortHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = kernel->SetRoutePort(request_msg_.new_port());
  SetRoutePortResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class AddStaticPeersHandler : public MessageHandler {
  public:
    ~AddStaticPeersHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }

    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgAddStaticPeersRequest request_msg_;
};

err_t AddStaticPeersHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  ListStaticPeers peers_to_add;
  for(int i = 0; i < request_msg_.ip_port_size(); i++) {
    const MsgIpPort &item = request_msg_.ip_port(i);
    peers_to_add.peers.emplace_back(item.ip().c_str(), item.port());
  }
  zisync_ret = kernel->AddStaticPeers(peers_to_add);
  assert(zisync_ret == ZISYNC_SUCCESS);

  AddStaticPeersResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class GetActiveIpHandler : public MessageHandler {
  public:
    ~GetActiveIpHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }
    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgGetActiveIpRequest request_msg_;
};

err_t GetActiveIpHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  GetActiveIpResponse response;
  MsgGetActiveIpResponse *msg_response = response.mutable_response();

  IContentResolver *resolver = GetContentResolver();
  const char* projs[] = {
    TableDeviceIP::COLUMN_IP,
  };

  unique_ptr<ICursor2> ip_cursor(resolver->Query(
        TableDeviceIP::URI, projs, ARRAY_SIZE(projs)
        , "%s = %" PRId64
        , TableDeviceIP::COLUMN_EARLIEST_NO_RESP_TIME
        , TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE
        ));
  while(ip_cursor->MoveToNext()) {
    msg_response->add_ips()->set_ip(ip_cursor->GetString(0));
  }
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class DeleteWatchHandler : public MessageHandler {
  public:
    ~DeleteWatchHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }
    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgDeleteWatchRequest request_msg_;
};

err_t DeleteWatchHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  zisync_ret = Monitor::GetMonitor()->DelWatchDir(request_msg_.path());
  assert(zisync_ret == ZISYNC_SUCCESS);

  DeleteWatchResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class HandleRootRemoveHandler : public MessageHandler {
  public:
    ~HandleRootRemoveHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }
    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgHandleRootRemoveRequest request_msg_;
};

err_t HandleRootRemoveHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  unique_ptr<Tree> tree(Tree::GetByIdWhereStatusNormal(request_msg_.tree_id()));
  assert(tree);
  zisync_ret = tree->TestInitLocalTreeModules();
  assert(zisync_ret == ZISYNC_ERROR_ROOT_MOVED);

  HandleRootRemoveResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class RefreshEnableHandler : public MessageHandler {
  public:
    ~RefreshEnableHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }
    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgRefreshEnableRequest request_msg_;
};

err_t RefreshEnableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  Config::enable_refresh();
  RefreshEnableResponse response;
  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class RefreshDisableHandler : public MessageHandler {
  public:
    ~RefreshDisableHandler(){}
    virtual ::google::protobuf::Message* mutable_msg() {
      return &request_msg_;
    }
    virtual err_t HandleMessage(
        const ZmqSocket& socket, const MsgHead& head, void* userdata);
  private:
    MsgRefreshDisableRequest request_msg_;
};

err_t RefreshDisableHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {

  err_t zisync_ret = ZISYNC_SUCCESS;
  Config::disable_refresh();
  RefreshDisableResponse response;

  zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}
#endif

static inline void InitMsgHandlers() {
#ifdef ZS_TEST
  msg_handlers_[MC_DEVICE_ADD_REQUEST] = new DeviceAddHandler;
  msg_handlers_[MC_DEVICE_INFO_REQUEST] = new DeviceInfoHandler;
  msg_handlers_[MC_DATABASE_INIT_REQUEST] = new DatabaseInitHandler;
  msg_handlers_[MC_MONITOR_ENABLE_REQUEST] = new MonitorEnableHandler;
  msg_handlers_[MC_MONITOR_DISABLE_REQUEST] = new MonitorDisableHandler;
  msg_handlers_[MC_SYNC_ADD_REQUEST] = new SyncAddHandler;
  msg_handlers_[MC_SYNC_DEL_REQUEST] = new SyncDelHandler;
  msg_handlers_[MC_TREE_ADD_REQUEST] = new TreeAddHandler;
  msg_handlers_[MC_TREE_DEL_REQUEST] = new TreeDelHandler;
  msg_handlers_[MC_REFRESH_REQUEST] = new RefreshOuterHandler;
  msg_handlers_[MC_SYNC_REQUEST] = new SyncOuterHandler;
  msg_handlers_[MC_REMOTE_TREE_ADD_REQUEST] = new RemoteTreeAddHandler;
  msg_handlers_[MC_REMOTE_DEVICE_SHOW_REQUEST] = new RemoteDeviceShowHandler;
  msg_handlers_[MC_TREE_SHOW_REQUEST] = new TreeShowHandler;
  msg_handlers_[MC_SYNC_SHOW_REQUEST] = new SyncShowHandler;
  msg_handlers_[MC_ISSUE_DEVICE_INFO_REQUEST] = new IssueDeviceInfoOuterHandler;
  msg_handlers_[MC_ISSUE_PUSH_DEVICE_INFO_REQUEST] = new IssuePushDeviceInfoOuterHandler;
  msg_handlers_[MC_ADD_FAVORITE_REQUEST] = new AddFavoriteHandler;
  msg_handlers_[MC_TEST_FIND_REQUEST] = new TestFindHandler;
  msg_handlers_[MC_LIST_SYNC_REQUEST] = new ListSyncHandler;
  msg_handlers_[MC_DHT_ANNOUNCE_ENABLE_REQUEST] = new DhtAnnounceEnableHandler;
  msg_handlers_[MC_DHT_ANNOUNCE_DISABLE_REQUEST] = new DhtAnnounceDisableHandler;
  msg_handlers_[MC_BROADCAST_ENABLE_REQUEST] = new BroadcastEnableHandler;
  msg_handlers_[MC_BROADCAST_DISABLE_REQUEST] = new BroadcastDisableHandler;
  msg_handlers_[MC_AUTO_SYNC_ENABLE_REQUEST] = new AutoSyncEnableHandler;
  msg_handlers_[MC_AUTO_SYNC_DISABLE_REQUEST] = new AutoSyncDisableHandler;
  msg_handlers_[MC_DEVICE_INFO_ENABLE_REQUEST] = new DeviceInfoEnableHandler;
  msg_handlers_[MC_DEVICE_INFO_DISABLE_REQUEST] = new DeviceInfoDisableHandler;
  msg_handlers_[MC_PUSH_DEVICE_INFO_ENABLE_REQUEST] = 
      new PushDeviceInfoEnableHandler;
  msg_handlers_[MC_PUSH_DEVICE_INFO_DISABLE_REQUEST] = 
      new PushDeviceInfoDisableHandler;
  msg_handlers_[MC_SET_ACCOUNT_REQUEST] = new SetAccountHandler;
  msg_handlers_[MC_STARTUP_DISCOVER_DEVICE_REQUEST] = 
      new StartupDiscoverDeviceHandler;
  msg_handlers_[MC_SHUTDOWN_DISCOVER_DEVICE_REQUEST] = 
      new ShutdownDiscoverDeviceHandler;
  msg_handlers_[MC_GET_DISCOVERED_DEVICE_REQUEST] = 
      new GetDiscoveredDeviceHandler;
  msg_handlers_[MC_TEST_SHARE_SYNC_REQUEST] = new TestShareSyncHandler;
  msg_handlers_[MC_STARTUP_DOWNLOAD_REQUEST] = new StartupDownloadHandler;
  msg_handlers_[MC_SHUTDOWN_DOWNLOAD_REQUEST] = new ShutdownDownloadHandler;
  msg_handlers_[MC_BACKUP_ADD_REQUEST] = new BackupAddHandler;
  msg_handlers_[MC_BACKUP_DEL_REQUEST] = new BackupDelHandler;
  msg_handlers_[MC_BACKUP_SHOW_REQUEST] = new BackupShowHandler;
  msg_handlers_[MC_ADD_BACKUP_TARGET_DEVICE_REQUEST] = 
      new AddBackupTargetDeviceHandler;
  msg_handlers_[MC_DEL_BACKUP_TARGET_DEVICE_REQUEST] = 
      new DelBackupTargetDeviceHandler;
  msg_handlers_[MC_SET_BACKUP_ROOT_REQUEST] = new SetBackupRootHandler;
  msg_handlers_[MC_SET_TEST_PLATFORM_REQUEST] = new SetTestPlatformHandler;
  msg_handlers_[MC_CLEAR_TEST_PLATFORM_REQUEST] = new ClearTestPlatformHandler;
  msg_handlers_[MC_SET_SYNC_MODE_REQUEST] = new SetSyncModeHandler;
  msg_handlers_[MC_GET_SYNC_MODE_REQUEST] = new GetSyncModeHandler;
  msg_handlers_[MC_QUERY_TREE_PAIR_STATUS_REQUEST] = new QueryTreePairStatusHandler;
  msg_handlers_[MC_SET_TREE_ROOT_REQUEST] = new SetTreeRootHandler;
  msg_handlers_[MC_DEL_BACKUP_TARGET_REQUEST] = new DelBackupTargetHandler;
  msg_handlers_[MC_SET_SHARE_SYNC_PERM_REQUEST] = new SetShareSyncPermHandler;
  msg_handlers_[MC_GET_SHARE_SYNC_PERM_REQUEST] = new GetShareSyncPermHandler;
  msg_handlers_[MC_ANNOUNCE_TOKEN_CHANGED_ENABLE_REQUEST] = 
      new AnnounceTokenChangedEnableHandler;
  msg_handlers_[MC_ANNOUNCE_TOKEN_CHANGED_DISABLE_REQUEST] = 
      new AnnounceTokenChangedDisableHandler;
  msg_handlers_[MC_SET_DOWNLOAD_CACHE_VOLUME_REQUEST] = 
      new SetDownloadCacheVolumeHandler;
  msg_handlers_[MC_QUERY_DOWNLOAD_STATUS_REQUEST] = 
      new QueryDownloadStatusHandler;
  msg_handlers_[MC_STARTUP_UPLOAD_REQUEST] = new StartupUploadHandler;
  msg_handlers_[MC_SHUTDOWN_UPLOAD_REQUEST] = new ShutdownUploadHandler;
  msg_handlers_[MC_QUERY_UPLOAD_STATUS_REQUEST] = new QueryUploadStatusHandler;
  msg_handlers_[MC_GET_DOWNLOAD_CACHE_AMOUNT_REQUEST] = 
      new GetDownloadCacheAmountHandler;
  msg_handlers_[MC_GET_DOWNLOAD_CACHE_ROOT_REQUEST] = 
      new GetDownloadCacheRootHandler;
  msg_handlers_[MC_CLEAN_UP_DOWNLOAD_CACHE_REQUEST] = 
      new CleanUpDownloadCacheHandler;
  msg_handlers_[MC_QUERY_HISTORY_INFO_REQUEST] =
      new QueryHistoryInfoHandler;
  msg_handlers_[MC_PERMISSION_VERIFY_REQUEST] =
      new PermissionVerifyHandler;
  msg_handlers_[MC_QUERY_PERMISSIONS_REQUEST] =
      new QueryPermissionsHandler;
  msg_handlers_[MC_SET_MAC_ADDRESS_IN_DB_REQUEST] =
      new SetMacTokenInDbHandler;
  msg_handlers_[MC_SET_EXPIRED_TIME_IN_DB_REQUEST] =
      new SetExpiredTimeInDbHandler;
  msg_handlers_[MC_CLEAR_PERMISSIONS_REQUEST] =
      new ClearPermissionsHandler;
  msg_handlers_[MC_QUERY_VERIFY_STATUS_REQUEST] =
      new QueryVerifyStatusHandler;
  msg_handlers_[MC_QUERY_VERIFY_IDENTIFY_INFO_REQUEST] =
      new QueryVerifyIdentifyInfoHandler;
  msg_handlers_[MC_SYNC_CONTROLER_REQUEST] =
      new SyncControlerHandler;
  msg_handlers_[MC_VERIFY_BIND_REQUEST] =
      new VerifyBindHandler;
  msg_handlers_[MC_VERIFY_UNBIND_REQUEST] =
      new VerifyUnbindHandler;
  msg_handlers_[MC_SET_SYNC_PERM_REQUEST] = new SetSyncPermHandler;
  msg_handlers_[MC_SET_DISCOVER_PORT_REQUEST] =
      new SetDiscoverPortHandler;
  msg_handlers_[MC_SET_ROUTE_PORT_REQUEST] =
      new SetRoutePortHandler;
  msg_handlers_[MC_ADD_STATIC_PEERS_REQUEST] =
      new AddStaticPeersHandler;
  msg_handlers_[MC_GET_ACTIVE_IP_REQUEST] =
      new GetActiveIpHandler;
  msg_handlers_[MC_DELETE_WATCH_REQUEST] =
      new DeleteWatchHandler;
  msg_handlers_[MC_HANDLE_ROOT_REMOVE_REQUEST] =
      new HandleRootRemoveHandler;
  msg_handlers_[MC_REFRESH_ENABLE_REQUEST] =
      new RefreshEnableHandler;
  msg_handlers_[MC_REFRESH_DISABLE_REQUEST] =
      new RefreshDisableHandler;
#endif
}

static inline void RecvAndProcessTestRequest() {
  InitMsgHandlers();
  ZmqContext context;
  string uri;
  zs::StringFormat(&uri, "tcp://*:%" PRId32, PORT);
  ZmqSocket socket(context, ZMQ_REP);
  err_t zisync_ret = socket.Bind(uri.c_str());
  assert(zisync_ret == ZISYNC_SUCCESS);
  while (true) {
    zmq_pollitem_t items[] = {
      { socket.socket(), -1, ZMQ_POLLIN, 0 },
    };

    int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t), -1);
    if (ret == -1) {
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      zs::MessageContainer container(msg_handlers_, false);
      container.RecvAndHandleSingleMessage(socket, NULL);
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Invalid argc : %d", argc);
    return 1;
  }
  token = argv[1];
  std::string pwd;
  zs::OsGetFullPath(".", &pwd);
  app_data = pwd + "/ZiSync";
  log_path = app_data + "/Log";
  backup = app_data + "/Backup";
  OsDeleteDirectories(backup);
  int ret = OsCreateDirectory(app_data, true);
  assert(ret == 0);
  ret = OsCreateDirectory(backup, true);
  assert(ret == 0);
  ret = OsCreateDirectory(log_path, true);
  assert(ret == 0);

  zs::DefaultLogger logger(log_path.c_str());
  logger.warning_to_stdout = true;
  logger.error_to_stderr = true;
  logger.info_to_stdout = true;
  logger.Initialize();
  zs::LogInitialize(&logger);
  err_t zisync_ret = kernel->Startup(app_data.c_str(), DISCOVER_PORT, NULL);
  if (zisync_ret == zs::ZISYNC_ERROR_CONFIG) {
    zisync_ret = kernel->Initialize(
        app_data.c_str(), token.c_str(), token.c_str(), backup.c_str());
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = kernel->Startup(app_data.c_str(), DISCOVER_PORT, NULL);
  } 
  assert(ret == ZISYNC_SUCCESS);
  RecvAndProcessTestRequest();
  kernel->Shutdown();

  return 0;
}
