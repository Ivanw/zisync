// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_DISCOVER_WORKER_H_
#define ZISYNC_KERNEL_WORKER_DISCOVER_WORKER_H_

#include <vector>
#include <memory>
#include <unordered_map>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/libevent/discover.h"
#include "zisync/kernel/libevent/libevent++.h"

namespace zs {

class ZmqSocket;
class TestDiscoverServer;
class MsgSetDiscoverPortRequest;
class SuperNode;
class DiscoverServer;

using std::unique_ptr;

class SearchNode {
 public:
  SearchNode(const char *host, int32_t port):
      host_(host), port_(port), store_time_(OsTimeInS()) {}
 
  std::string host_;
  int32_t port_;
  int64_t store_time_;
  bool is_lan_;
};
/*
class IpPort {
 public:
  IpPort() {}
  IpPort(const char *ip, int32_t port): ip_(ip), port_(port) {}
  std::string ip_;
  int32_t port_;
};
*/
class SearchResult {
 public:
  SearchResult(const char *info_hash_hex_):info_hash_hex(info_hash_hex_) {}

  string info_hash_hex;
  std::unordered_map<string, unique_ptr<SearchNode>> nodes;   // key is addr
  std::unordered_map<string, unique_ptr<SearchNode>> nodes6;  // key is addr
};

class IDiscoverDataSource {
 public:
  virtual ~IDiscoverDataSource() {}
  
  virtual int32_t GetDiscoverPort() = 0;
  virtual const std::string GetMyNodeId() = 0;

  virtual err_t GetTrackers(std::vector<std::string>* super_nodes) = 0;
  virtual err_t GetInfoHashes(std::vector<std::string>* infohashes) = 0;
  
  virtual err_t StorePort(int32_t new_port) = 0;
  virtual void  StorePeers(const std::string infohash, const bool is_ipv6,
                           const std::vector<SearchNode> &peers) = 0;
  virtual const std::string GetAccount() = 0;
  virtual err_t GetShareSyncs(std::vector<std::string>* sync_uuids) = 0;

  virtual err_t GetStaticPeers(std::vector<IpPort> *staticaddrs) = 0;
  virtual err_t StoreStaticPeers(const std::vector<IpPort> &staticaddrs) = 0;
  virtual err_t DeleteStaticPeers(const std::vector<IpPort> &staticaddrs) = 0;
  virtual err_t SaveStaticPeers(const std::vector<IpPort> &staticaddrs) = 0;
};

class DiscoverServer : public IDiscoverServer {
#ifdef ZS_TEST
  friend class DiscoverCacheClearHandler;
#endif

 public:
  DiscoverServer(IDiscoverDataSource* data_source);
  virtual ~DiscoverServer();

  static DiscoverServer* GetInstance();
  //
  // Implement IDiscoverServer
  //
  virtual err_t Initialize(int32_t route_port);
  virtual err_t CleanUp();
  
  virtual void SetForeground();
  virtual void SetBackground();
  virtual void IssueBroadcast();
  virtual void IssueAnnounce();
  virtual void PeerErase(const MsgDiscoverPeerEraseRequest &request);
  virtual err_t SetDiscoverPort(int32_t discover_port);

  virtual err_t GetStaticPeers(std::vector<IpPort> *staticaddrs);
  virtual err_t AddStaticPeers(const std::vector<IpPort> &staticaddrs);
  virtual err_t DeleteStaticPeers(const std::vector<IpPort> &staticaddrs);
  virtual err_t SaveStaticPeers(const std::vector<IpPort> &staticaddrs);

#ifdef ZS_TEST
  virtual err_t SearchCacheClear();
  void SearchCacheClearCb();
#endif

  //
  // Implement ILibEventVirtualServer
  //
  virtual err_t Startup(ILibEventBase* base);
  virtual err_t Shutdown(ILibEventBase* base);

  //
  // The following DoSomething function must be exec in virtual server
  // thread
  // 
  void OnDHTEvent(
      int event, unsigned char *info_hash,
      void *data, size_t data_len);
  void DoDHTPingSuperNode();
  void DoDHTAnnounce();
  void DoDHTBroadcast();
  void DoCheckPeerExpired();
  void DoStaticAddrs();


  err_t DoSetBackground(); 
  err_t DoSetForeground(); 
  void  DoPeerErase(MsgDiscoverPeerEraseRequest* request);
  err_t DoSetDiscoverPort(int32_t new_port);
  void DhtEventCb(short event);
  //
  // End of OnFooBar
  //

  ILibEventBase* evbase() {
    return evbase_;
  }
  
 private:
  DiscoverServer(DiscoverServer&);
  void operator=(DiscoverServer&);

  err_t ScheduleUdpEvent();
  err_t ReInitUdpSocket(int32_t port);

  SearchResult* FindSerachResult(const std::string& info_hash);
  bool ShouldStoreInfohash(const std::string &info_hash_bin);
  void StorePeers(const std::vector<std::string> &addrs, const std::string& info_hash,
                 bool is_lan);
  void StorePeer6s(const std::vector<std::string> &addrs, const std::string& info_hash,
                  bool is_lan);
  err_t SetDiscoverPortInter(int32_t new_port);
  void IssueInsertNode(const std::string info_hash, const bool is_ipv6,
                       const std::vector<SearchNode> &peers);
  void IssueDeviceInfo(const std::string& host, int port,
                       const char *sync_uuid, bool is_ipv6);
  void IssueStaticPeers(const std::vector<IpPort> &staticaddrs);
  void DoStaticPeersStep(const IpPort &addr);

 private:
  ZmqSocket *inner_push;
  ILibEventBase* evbase_;
  OsUdpSocket *udp_socket_;
  time_t tosleep_time_;

  int route_port_;
  bool delete_data_source_;
  IDiscoverDataSource* data_source_;

  std::unordered_map<std::string, unique_ptr<SearchResult>> search_result_cache_;
  std::unordered_map<std::string, int> announce_items_;

  struct event* dht_event_;
  SuperNode* super_node_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_DISCOVER_WORKER_H_
