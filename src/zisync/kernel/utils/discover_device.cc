// Copyright 2014, zisync.com
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <tuple>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/discover_device.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/libevent/discover_server.h"
#include "zisync/kernel/libevent/libevent_base.h"

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#include "zisync/kernel/notification/notification.h"
#include "zisync/kernel/kernel_stats.h"
#endif
namespace zs {

using std::unique_ptr;
using std::string;
using std::map;
using std::vector;

class IssueDeviceMetaAbort;
class DiscoverDeviceThread;

DiscoveredDevice::DiscoveredDevice(
    int32_t id_, const std::string& name_, const std::string &uuid_,
    const std::string& ip_, 
    int32_t route_port_, int32_t data_port_, int32_t discover_port_,
    MsgDeviceType type_, bool is_mine_, bool is_ipv6_,
    bool is_shared_, bool is_static_peer_):
    id(id_), name(name_), uuid(uuid_), ip(ip_), route_port(route_port_), 
    data_port(data_port_), discover_port(discover_port_),
    type(MsgDeviceTypeToDeviceType(type_)), is_mine(is_mine_),
    is_ipv6(is_ipv6_), is_shared(is_shared_), is_static_peer(is_static_peer_) {}

class IssueDeviceMetaReq : public IssueRequest {
 public:
  IssueDeviceMetaReq(
      const char *remote_ip, int32_t remote_route_port, bool is_ipv6_):
      IssueRequest(-1, remote_ip, remote_route_port, false), is_ipv6(is_ipv6_) {}
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }

  DeviceMetaRequest request;
  DeviceMetaResponse response;
  bool is_ipv6;
 private:
  IssueDeviceMetaReq(IssueDeviceMetaReq&);
  void operator=(IssueDeviceMetaReq&);
};

class IssueDeviceMetaAbort : public IssueRequestsAbort {
 public:
  IssueDeviceMetaAbort(DiscoverDeviceThread *thread_):thread(thread_) {}
  inline virtual bool IsAborted();

 private:
  DiscoverDeviceThread *thread;
};

class DiscoverDeviceThread : public OsThread, IAbortable {
 public :
  DiscoverDeviceThread(int32_t sync_id, int32_t discover_id):OsThread("DiscoverDeivce"),
    abort(this), 
    issue_requests(DISCOVER_DEVICE_TIMEOUT_IN_S * 1000, &abort), 
    aborted(false),search_is_done(false), sync_id_(sync_id), discover_id_(discover_id) {
#ifdef __APPLE__
      deviceInfos = [NSMutableArray array];
#endif
    }
  virtual ~DiscoverDeviceThread() {
    this->Abort();
    this->Shutdown();
  }

  virtual int Run();
  virtual bool Abort() {
    aborted = true;
    return true;
  }
  virtual bool IsAborted() { return aborted; }

  err_t GetDiscoveredDevice(DiscoverDeviceResult *result);
  err_t GetDiscoveredDevice(int32_t device_id, DiscoveredDevice *device);
  int32_t sync_id() { return sync_id_; }

 protected:
  IssueRequests<IssueDeviceMetaReq>& GetIssueRequests() {
    return issue_requests;
  }

  vector<unique_ptr<DiscoveredDevice>>& GetDiscoveredDevices() {
    return discovered_devices;
  }

  Mutex* GetMutex() {
    return &mutex;
  }

  void SetSearch(bool method) {
    search_is_done = method;
  }

 private:
  IssueDeviceMetaAbort abort;
  IssueRequests<IssueDeviceMetaReq> issue_requests;
  Mutex mutex;
  vector<unique_ptr<DiscoveredDevice>> discovered_devices;
#ifdef __APPLE__
  NSMutableArray *deviceInfos;
#endif
  bool aborted;
  bool search_is_done;
  int32_t sync_id_;
  int32_t discover_id_;
};

err_t DiscoverDeviceThread::GetDiscoveredDevice(
    DiscoverDeviceResult *result) {
  result->devices.clear();
  MutexAuto auto_mutex(&mutex);
  result->is_done = search_is_done;
  for (auto iter = discovered_devices.begin(); 
       iter != discovered_devices.end(); iter ++) {
    DeviceInfo device;
    device.device_id = (*iter)->id;
    assert((*iter)->id == std::distance(discovered_devices.begin(), iter));
    device.device_name = (*iter)->name;
    device.device_type = (*iter)->type;
    device.is_mine = (*iter)->is_mine;
    device.is_shared = (*iter)->is_shared;
    device.ip = (*iter)->ip;
    device.discover_port = (*iter)->discover_port;
    device.is_static_peer = (*iter)->is_static_peer;
    result->devices.push_back(device);
  }
  return ZISYNC_SUCCESS;
}

err_t DiscoverDeviceThread::GetDiscoveredDevice(
    int32_t device_id, DiscoveredDevice *device) {
  if (device_id < 0 || 
      device_id >= static_cast<int32_t>(discovered_devices.size())) {
    return ZISYNC_ERROR_DEVICE_NOENT;
  }
  MutexAuto auto_mutex(&mutex);
  *device = *discovered_devices[device_id];
  return ZISYNC_SUCCESS;
}

bool IssueDeviceMetaAbort::IsAborted() {
  return thread->IsAborted();
};

int DiscoverDeviceThread::Run() {
  IContentResolver *resolver = GetContentResolver();

  ListStaticPeers static_peers;
  err_t rv = 
    IDiscoverServer::GetInstance()->GetStaticPeers(&(static_peers.peers));
  assert(rv == ZISYNC_SUCCESS);

  const char* dht_peer_projs[] = {
    TableDHTPeer::COLUMN_PEER_HOST,
    TableDHTPeer::COLUMN_PEER_PORT, TableDHTPeer::COLUMN_PEER_IS_IPV6,
  };

  string account_name_sha;
  Sha1Hex(Config::account_name(), &account_name_sha);
  unique_ptr<ICursor2> dht_peer_cursor(resolver->Query(
          TableDHTPeer::URI, dht_peer_projs, ARRAY_SIZE(dht_peer_projs), 
          NULL));
  while (dht_peer_cursor->MoveToNext()) {
    const char *host = dht_peer_cursor->GetString(0);
    const int32_t port = dht_peer_cursor->GetInt32(1);
    const bool is_ipv6 = dht_peer_cursor->GetBool(2);
    IssueDeviceMetaReq *req= new IssueDeviceMetaReq(host, port, is_ipv6);
    issue_requests.IssueOneRequest(req);
  }

  IssueDeviceMetaReq *req;
  string my_token_sha1 = Config::token_sha1();
  while ((req = issue_requests.RecvNextResponsedRequest()) != NULL) {
    if (IsAborted()) {
      break;
    }
    if (req->error() != ZISYNC_SUCCESS) {
      ZSLOG_WARNING("DeviceMetaRequest fail : %s", 
                    zisync_strerror(req->error()));
      continue;
    }
    const MsgDevice &device = req->response.response().device();
    if (device.uuid() == Config::device_uuid()) {
      continue;
    }
    auto iter = std::find_if(
        discovered_devices.begin(), discovered_devices.end(),
        [ device ] (const unique_ptr<DiscoveredDevice> &discovered_device)
        { return device.uuid() == discovered_device->uuid; });
    if (iter != discovered_devices.end()) {
      continue;
    }
    const string &token_sha1 = req->response.response().token();
    bool is_mine = token_sha1 == my_token_sha1;
    int32_t device_id;
    {
      const char *device_projs[] = { TableDevice::COLUMN_ID };
      IContentResolver *resolver = GetContentResolver();
      unique_ptr<ICursor2> device_cursor(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
              "%s = '%s'", TableDevice::COLUMN_UUID, device.uuid().c_str()));
      device_id = device_cursor->MoveToNext() ? 
          device_cursor->GetInt32(0) : -1;
    }
    bool is_shared = false;
    if (device_id != -1) {
      int32_t sync_perm;
      err_t zisync_ret = zs::GetShareSyncPerm(
          device_id, sync_id_, &sync_perm);
      is_shared = (zisync_ret == ZISYNC_SUCCESS) && 
          (sync_perm != TableSync::PERM_DISCONNECT);
    }
    {      
      const string &ip = req->remote_ip();
      auto iter = std::find_if(
          static_peers.peers.begin(), static_peers.peers.end(),
          [ ip ] (const IpPort &ip_port)
          { return ip  == ip_port.ip_; });
      bool is_static = iter != static_peers.peers.end();
      int32_t discover_port = is_static ? iter->port_ : Config::discover_port();

      MutexAuto auto_mutex(&mutex);

      discovered_devices.emplace_back(new DiscoveredDevice(
              discovered_devices.size(), device.name(), device.uuid(), 
              req->remote_ip(), device.route_port(), device.data_port(),
              discover_port, device.type(), is_mine, req->is_ipv6,
              is_shared, is_static));
#ifdef __APPLE__
      @autoreleasepool {
        [deviceInfos addObject:[[DeviceInfoObjc alloc] initWithDiscoveredDevice:*discovered_devices.back().get()]];
      }
#endif
    }
   
#ifdef __APPLE__
    @autoreleasepool {
      NSDictionary *userInfo = @{kZSNotificationUserInfoEventId:[NSNumber numberWithInt:discover_id_], kZSNotificationUserInfoData:deviceInfos};
      [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameUpdateDiscoveredDevices
                                                          object:nil
                                                        userInfo:userInfo];
    }
#endif
    
    if (is_mine) {
      zs::StoreDeviceIntoDatabase(
          device, req->remote_ip(), is_mine, req->is_ipv6, false);
    }
  }
  issue_requests.UpdateDeviceInDatabase();
  search_is_done = true;

  return 0;
}

static void LambdaShutdownDiscoverDevice(void *);

class DiscoverDeviceHandler : public IDiscoverDeviceHandler {
  friend void zs::LambdaShutdownDiscoverDevice(void *);
 public:
  virtual err_t StartupDiscover(
      int32_t sync_id, int32_t *discover_id);
  virtual err_t ShutDownDiscover(int32_t discover_id);
  virtual err_t GetDiscoveredDevice(
      int32_t discover_id, DiscoverDeviceResult *result);
  virtual err_t GetDiscoveredDevice(
      int32_t discover_id, int32_t device_id, DiscoveredDevice *device);
  virtual err_t GetSyncId(int32_t discover_id, int32_t *sync_id) ;
  virtual void CleanUp();

  static IDiscoverDeviceHandler* GetInstance() {
    return &handler;
  }
 private:
  DiscoverDeviceHandler(DiscoverDeviceHandler&);
  void operator=(IDiscoverDeviceHandler&);
  DiscoverDeviceHandler():tasks_mask(0), tasks_max_num(64), 
    tasks(tasks_max_num) {}

  static DiscoverDeviceHandler handler;
  int32_t AllocDiscoverId() {
    int64_t mask = tasks_mask;
    for (int32_t i = 0; i < tasks_max_num; i ++, mask >>= 1) {
      if (mask & 0x1) {
        continue;
      } else {
        tasks_mask |= static_cast<int64_t>(1) << i;
        return i;
      }
    }
    return -1;
  }
  
  void ReleaseDiscoverId(int32_t discover_id) {
    assert(discover_id >= 0);
    assert(discover_id < tasks_max_num);
    tasks_mask &= ~(static_cast<int64_t>(1) << discover_id);
  }

  err_t ShutDownDiscoverIntern(int32_t discover_id);
 
  Mutex mutex;
  int64_t tasks_mask;
  int tasks_max_num;
  vector<unique_ptr<DiscoverDeviceThread>> tasks;
};

DiscoverDeviceHandler DiscoverDeviceHandler::handler;

IDiscoverDeviceHandler* IDiscoverDeviceHandler::GetInstance() {
  return DiscoverDeviceHandler::GetInstance();
}

err_t DiscoverDeviceHandler::StartupDiscover(
    int32_t sync_id, int32_t *discover_id) {
  MutexAuto auto_mutex(&mutex);
  *discover_id = AllocDiscoverId(); 
  if (*discover_id == -1) {
    return ZISYNC_ERROR_DISCOVER_LIMIT;
  }
  assert(*discover_id < tasks_max_num);
  unique_ptr<DiscoverDeviceThread> &thread = tasks[*discover_id];
  assert(!thread);
  thread.reset(new DiscoverDeviceThread(sync_id, *discover_id));

  int ret = thread->Startup();
  if (ret != 0) {
    thread.reset(NULL);
    ReleaseDiscoverId(*discover_id);
    return ZISYNC_ERROR_OS_THREAD;
  }
  return ZISYNC_SUCCESS;
}

static void LambdaShutdownDiscoverDevice(void *ctx) {
  DiscoverDeviceHandler *handler;
  int32_t discover_id;

  std::tie(handler, discover_id) =
      *(std::tuple<DiscoverDeviceHandler*, int32_t>*)ctx;
  handler->ShutDownDiscoverIntern(discover_id);
  delete (std::tuple<DiscoverDeviceHandler*, int32_t>*)ctx;
}

err_t DiscoverDeviceHandler::ShutDownDiscoverIntern(int32_t discover_id) {
  if (discover_id < 0 || discover_id >= tasks_max_num) {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
  MutexAuto auto_mutex(&mutex);
  unique_ptr<DiscoverDeviceThread> &thread = tasks[discover_id];
  if (thread) {
    thread.reset(NULL);
    ReleaseDiscoverId(discover_id);
    return ZISYNC_SUCCESS;
  } else {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
}

err_t DiscoverDeviceHandler::ShutDownDiscover(int32_t discover_id) {
#ifndef ZS_TEST
  ILibEventBase *base_ = GetEventBaseDb();
  auto context = new std::tuple<DiscoverDeviceHandler*,
  int32_t>(this, discover_id);
  base_->DispatchAsync(LambdaShutdownDiscoverDevice, context, NULL);
  return ZISYNC_SUCCESS;
#else
  return ShutDownDiscoverIntern(discover_id);
#endif
}

err_t DiscoverDeviceHandler::GetDiscoveredDevice(
    int32_t discover_id, DiscoverDeviceResult *result) {
  if (discover_id < 0 || discover_id >= tasks_max_num) {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
  result->devices.clear();
  MutexAuto auto_mutex(&mutex);
  unique_ptr<DiscoverDeviceThread> &thread = tasks[discover_id];
  if (thread) {
    return thread->GetDiscoveredDevice(result);
  } else {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
}

err_t DiscoverDeviceHandler::GetDiscoveredDevice(
      int32_t discover_id, int32_t device_id, DiscoveredDevice *device) {
  if (discover_id < 0 || discover_id >= tasks_max_num) {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
  MutexAuto auto_mutex(&mutex);
  unique_ptr<DiscoverDeviceThread> &thread = tasks[discover_id];
  if (thread) {
    return thread->GetDiscoveredDevice(device_id, device);
  } else {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
}

err_t DiscoverDeviceHandler::GetSyncId(
      int32_t discover_id, int32_t *sync_id) {
  if (discover_id < 0 || discover_id >= tasks_max_num) {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
  MutexAuto auto_mutex(&mutex);
  unique_ptr<DiscoverDeviceThread> &thread = tasks[discover_id];
  if (thread) {
    *sync_id = thread->sync_id();
    return ZISYNC_SUCCESS;
  } else {
    return ZISYNC_ERROR_DISCOVER_NOENT;
  }
}

void DiscoverDeviceHandler::CleanUp() {
  MutexAuto auto_mutex(&mutex);
  for (auto iter = tasks.begin(); iter != tasks.end(); iter ++) {
    iter->reset(NULL);
  }
  tasks_mask = 0;
}

}  // namespace zs
