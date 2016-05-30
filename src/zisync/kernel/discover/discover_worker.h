// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_DISCOVER_WORKER_H_
#define ZISYNC_KERNEL_WORKER_DISCOVER_WORKER_H_

#include <vector>
#include <memory>
#include <unordered_map>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/discover/discover.h"

namespace zs {

class ZmqSocket;
class TestDiscoverWorker;
class MsgSetDiscoverPortRequest;

using std::unique_ptr;

class DiscoverWorker;
class SearchNode {
 public:
  SearchNode(const char *host, int32_t port):
      host_(host), port_(port), store_time_(OsTimeInS()) {}
 
  std::string host_;
  int32_t port_;
  int64_t store_time_;
  bool is_lan_;
};

class SearchResult {
 public:
  SearchResult(const char *info_hash_hex_):info_hash_hex(info_hash_hex_) {}
  string info_hash_hex;
  std::unordered_map<string, unique_ptr<SearchNode>> nodes; // key is addr
  std::unordered_map<string, unique_ptr<SearchNode>> nodes6;        // key is addr
};

class IDiscoverDataStore {
 public:
  virtual ~IDiscoverDataStore() {}
  
  virtual err_t Initialize() = 0;
  virtual int32_t GetPort() = 0;
  virtual const std::string GetMyNodeId() = 0;

  virtual err_t GetSuperNodes(std::vector<std::string>* super_nodes) = 0;
  virtual err_t GetInfoHashes(std::vector<std::string>* infohashes) = 0;
  
  virtual err_t StorePort(int32_t new_port) = 0;
  virtual void  StorePeer(const std::string& info_hash,
                          const std::string& host,
                          int port, bool is_lan, bool is_ipv6) = 0;
};

class BroadcastTimerHandle : public IOnTimer {
 public:
  virtual void OnTimer();
};

class CheckPeerExpiredOnTimer : public IOnTimer {
 public:
  virtual void OnTimer();
};

class DiscoverWorker
    : public IRunnable
    , public IDiscoverWorker
    , public IOnTimer {
  friend class TestDiscoverWorker;
#ifdef ZS_TEST
  friend class DiscoverCacheClearHandler;
#endif

 public:
  DiscoverWorker(IDiscoverDataStore* data_store);
  virtual ~DiscoverWorker();

  virtual err_t Initialize(int32_t route_port);
  virtual err_t CleanUp();

  virtual err_t SetDiscoverPort(int32_t discover_port);
  err_t PeerErase(const MsgDiscoverPeerEraseRequest &request);
#ifdef ZS_TEST
  virtual err_t SearchCacheClear();
#endif

  virtual int Run();
  void OnDHTEvent(int event,
                  unsigned char *info_hash,
                  void *data, size_t data_len);

  //
  // Implement IOnTimer
  //
  virtual void OnTimer();
  void DHTPingSupernode();
  void DHTAnnounce();
  void DHTBroadcast();
  void DHTBroadcastReply(std::string sa, std::string info_hash, int port);
  void CheckPeerExpired();
  err_t SetBackground(); 
  err_t SetForeground(); 

 protected:
  void ThreadInitialize();
  void ThreadCleanUp();
  /**
   * @param is_lan: 1 if peer is in lan, otherwise 0.
   * @param is_ipv6: 1 if host is ipv6 address, otherwise 0.
   */
  /*
  void StoreBucket(const std::string& host, const int port,
                   const int is_ipv6);
  */
  bool ShouldStoreInfohash(const std::string &info_hash_bin);

 private:
  DiscoverWorker(DiscoverWorker&);
  void operator=(DiscoverWorker&);
  SearchResult* FindSerachResult(const std::string& info_hash);
  void StorePeer(const std::string& addr, const std::string& info_hash,
                 SearchResult* search_result, bool is_lan);
  void StorePeer6(const std::string& addr, const std::string& info_hash,
                  SearchResult* search_result, bool is_lan);
  // Recv msg from the udp and parse it.
  err_t Initialize();
  void HandleUdpMsg();
  void HandleCmd();
  void DHTPeriodic();

  const int ping_supernode_interval_with_announce;
  int announced_count;

  OsWorkerThread* thread_;
  OsUdpSocket *udp_socket_;
  ZmqSocket* cmd_socket_;
  ZmqSocket* exit_socket_;
  ZmqSocket* pull_socket_;
  time_t tosleep_time_;
  int64_t next_periodic_time_in_ms_;
  int route_port_;
  OsTimer discover_timer_;
  BroadcastTimerHandle broadcast_timer_handle_;
  OsTimer broadcast_timer_;
  CheckPeerExpiredOnTimer check_peer_expired_on_timer_;
  OsTimer check_peer_expired_timer_;
  std::unordered_map<std::string, unique_ptr<SearchResult>> search_result_cache_;
  std::map<MsgCode, MessageHandler*> cmd_handler_map_;
  std::map<MsgCode, MessageHandler*> pull_handler_map_;
  IDiscoverDataStore* data_store_;
  bool delete_data_store_;
  std::unordered_map<std::string, int> announce_items_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_DISCOVER_WORKER_H_
