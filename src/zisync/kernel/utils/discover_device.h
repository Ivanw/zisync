// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_DISCOVER_DEVICE_H_
#define ZISYNC_KERNEL_UTILS_DISCOVER_DEVICE_H_

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/proto/kernel.pb.h"

namespace zs {

class DiscoveredDevice {
 public:
  DiscoveredDevice() {}
  DiscoveredDevice(
      int32_t id_, const std::string& name_, const std::string &uuid_,
      const std::string& ip_, 
      int32_t route_port_, int32_t data_port_, int32_t discover_port,
      MsgDeviceType type_, bool is_mine_, bool is_ipv6_,
      bool is_shared, bool is_static_peer_);
  int32_t id;
  std::string name;
  std::string uuid;
  std::string ip;
  int32_t route_port;
  int32_t data_port;
  int32_t discover_port;
  int type;
  bool is_mine;
  bool is_ipv6;
  bool is_shared;
  bool is_static_peer;
};

class IDiscoverDeviceHandler {
 public:
  IDiscoverDeviceHandler() {}
  virtual ~IDiscoverDeviceHandler() {};
  virtual void CleanUp() = 0;
  virtual err_t StartupDiscover(
      int32_t sync_id, int32_t *discover_id) = 0;
  virtual err_t ShutDownDiscover(int32_t discover_id) = 0;
  virtual err_t GetDiscoveredDevice(
      int32_t discover_id, DiscoverDeviceResult *result) = 0;
  virtual err_t GetDiscoveredDevice(
      int32_t discover_id, int32_t device_id, DiscoveredDevice *device) = 0;
  virtual err_t GetSyncId(int32_t discover_id, int32_t *sync_id) = 0;

  static IDiscoverDeviceHandler* GetInstance();

 private:
  IDiscoverDeviceHandler(IDiscoverDeviceHandler&);
  void operator=(IDiscoverDeviceHandler&);
};

}  // namespace zs

#endif // ZISYNC_KERNEL_UTILS_DISCOVER_DEVICE_H_
