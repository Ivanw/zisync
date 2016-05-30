// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_DISCOVER_H_
#define ZISYNC_KERNEL_DISCOVER_H_

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/libevent/libevent++.h"

namespace zs {

class MsgDiscoverPeerEraseRequest;
class IpPort;

class IDiscoverServer : public ILibEventVirtualServer {
 public:
  virtual ~IDiscoverServer() { /* virtual destructor */ }

  virtual err_t Initialize(int32_t route_port) = 0;
  virtual err_t CleanUp() = 0;

  virtual void SetForeground()  = 0;
  virtual void SetBackground()  = 0;
  virtual void IssueBroadcast() = 0;
  virtual void IssueAnnounce()  = 0;
  virtual void PeerErase(const MsgDiscoverPeerEraseRequest& request) = 0;
  virtual err_t SetDiscoverPort(int32_t discover_port) = 0;

  virtual err_t GetStaticPeers(std::vector<IpPort> *staticaddrs) = 0;
  virtual err_t AddStaticPeers(const std::vector<IpPort> &staticaddrs) = 0;
  virtual err_t DeleteStaticPeers(const std::vector<IpPort> &staticaddrs) = 0;
  virtual err_t SaveStaticPeers(const std::vector<IpPort> &staticaddrs) = 0;

// #ifdef ZS_TEST
//   virtual err_r SearchCacheClear() = 0;
// #endif
  static IDiscoverServer* GetInstance();

 protected:
  IDiscoverServer()  {}

 private:
  DISALLOW_COPY_AND_ASSIGN(IDiscoverServer);
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_DISCOVER_H_
