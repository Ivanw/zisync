#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <iphlpapi.h>
#include <time.h>
#pragma comment(lib, "IPHLPAPI.lib")
#endif

#include <stdlib.h>

#include <string>
#include <vector>
#include <algorithm>

#include <unordered_map>

#ifndef ZISYNC_XCODE_BUILD
extern "C"{
#include "zisync/kernel/discover/dht.h"
}
#else 
#include "zisync/kernel/discover/dht.h"
#endif

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/discover/discover_worker.h"
#include "zisync/kernel/discover/discover_data_store.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/context.h"

void TransformUriToAddr(const char *dht_nodes, struct sockaddr_in *addr);

namespace zs {

using std::vector;

// const int UDP_BUF_SIZE = 4096;
const char IDiscoverWorker::cmd_uri[] = "inproc://discover_cmd";
const char IDiscoverWorker::pull_uri[] = "inproc://discover_pull";
const int broadcast_interval_in_ms = 10000;
const int dht_announce_interval_in_ms = 18000;
const int peer_expired_time_in_s = 360;
const int check_peer_expired_interval_in_ms = 60000;

const int ping_supernode_interval_with_announce = 10;
int announced_count = 0;

static DiscoverWorker s_instance(NULL);

static void DHTCallBack(
    void *closure, int event, unsigned char *info_hash,
    void *data, size_t data_len) {
  auto worker = static_cast<zs::DiscoverWorker*>(closure);
  worker->OnDHTEvent(event, info_hash, data, data_len);
}


IDiscoverWorker* IDiscoverWorker::GetInstance() {
  return &s_instance;
}

class SetDiscoverPortHandler : public MessageHandler {
 public:
  virtual ~SetDiscoverPortHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_SET_DISCOVER_PORT_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return &request_msg_; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    DiscoverWorker* worker = static_cast<DiscoverWorker*>(userdata);
    err_t zisync_ret = worker->SetDiscoverPort(request_msg_.new_port());
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("woker->SetDiscoverPort(%" PRId32 ") fail : %s",
                  request_msg_.new_port(), zisync_strerror(zisync_ret));
      return zisync_ret;
    }

    zisync_ret = SaveDiscoverPort(request_msg_.new_port());
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("SaveDiscoverPort fail.");
      return zisync_ret;
    }
    Config::set_discover_port(request_msg_.new_port());

    SetDiscoverPortResponse response;
    zisync_ret = response.SendTo(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);

    return ZISYNC_SUCCESS;
  }

 private:
  MsgSetDiscoverPortRequest request_msg_;
};

#ifdef ZS_TEST
class DiscoverCacheClearHandler : public MessageHandler {
 public:
  virtual ~DiscoverCacheClearHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_DISCOVER_CACHE_CLEAR_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    DiscoverWorker* worker = static_cast<DiscoverWorker*>(userdata);
    worker->search_result_cache_.clear();
    DiscoverCacheClearResponse response;
    err_t zisync_ret = response.SendTo(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);
    return ZISYNC_SUCCESS;
  }
};
#endif

class DiscoverPeerEraseHandler : public MessageHandler {
 public:
  virtual ~DiscoverPeerEraseHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_DISCOVER_PEER_ERASE_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return &request_msg_; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    DiscoverWorker* worker = static_cast<DiscoverWorker*>(userdata);
    err_t zisync_ret = worker->PeerErase(request_msg_);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_WARNING("woker->PeerErase(%s) fail : %s",
                  request_msg_.info_hash_hex().c_str(),
                  zisync_strerror(zisync_ret));
    }
    DiscoverPeerEraseResponse response;
    zisync_ret = response.SendTo(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);

    return ZISYNC_SUCCESS;
  }

 private:
  MsgDiscoverPeerEraseRequest request_msg_;
};

class DHTAnnounceHandler : public MessageHandler {
 public:
  virtual ~DHTAnnounceHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_DHT_ANNOUNCE_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    // DhtAnnounceResponse response;
    // response.SendTo(socket);
    
    static_cast<DiscoverWorker*>(userdata)->DHTAnnounce();
    if (announced_count++ == ping_supernode_interval_with_announce) {
      announced_count %= ping_supernode_interval_with_announce;
      static_cast<DiscoverWorker*>(userdata)->DHTPingSupernode();
    }

    return ZISYNC_SUCCESS;
  }
};

class DHTBroadcastHandler : public MessageHandler {
 public:
  virtual ~DHTBroadcastHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_BROADCAST_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    // BroadcastResponse response;
    // response.SendTo(socket);
    static_cast<DiscoverWorker*>(userdata)->DHTBroadcast();
    return ZISYNC_SUCCESS;
  }
};

class CheckPeerExpiredHandler : public MessageHandler {
 public:
  virtual ~CheckPeerExpiredHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_CHECK_PEER_EXPIRED_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    // CheckPeerExpiredResponse response;
    // response.SendTo(socket);
    static_cast<DiscoverWorker*>(userdata)->CheckPeerExpired();
    return ZISYNC_SUCCESS;
  }
};

class SetBackgroundHandler : public MessageHandler {
 public:
  virtual ~SetBackgroundHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_CHECK_PEER_EXPIRED_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    // BackgroundResponse response;
    // response.SendTo(socket);
    return static_cast<DiscoverWorker*>(userdata)->SetBackground();
  }
};

class SetForegroundHandler : public MessageHandler {
 public:
  virtual ~SetForegroundHandler() {}
  virtual MsgCode GetMsgCode() const { return MC_CHECK_PEER_EXPIRED_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) {
    // ForegroundResponse response;
    // response.SendTo(socket);
    return static_cast<DiscoverWorker*>(userdata)->SetForeground();
  }
};

void BroadcastTimerHandle::OnTimer() {
#ifdef ZS_TEST
  if (!Config::is_broadcast_enabled()) {
    return;
  }
#endif

  err_t zisync_ret = ZISYNC_SUCCESS;
  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = req.Connect(IDiscoverWorker::pull_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connet to discover port fail: %s",
                zisync_strerror(zisync_ret));
    return;
  }
  BroadcastRequest request;
  zisync_ret = request.SendTo(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send DHTBroadcastRequsetfail : %s",
                zisync_strerror(zisync_ret));
  }
}

void CheckPeerExpiredOnTimer::OnTimer() {
  err_t zisync_ret = ZISYNC_SUCCESS;
  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = req.Connect(IDiscoverWorker::pull_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connet to discover port fail: %s",
                zisync_strerror(zisync_ret));
    return;
  }
  CheckPeerExpiredRequest request;
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

DiscoverWorker::DiscoverWorker(IDiscoverDataStore* data_store)
  : ping_supernode_interval_with_announce(20), announced_count(0),
    thread_(NULL), udp_socket_(NULL), 
    cmd_socket_(NULL), exit_socket_(NULL), pull_socket_(NULL),
    tosleep_time_(0),
    discover_timer_(dht_announce_interval_in_ms, this),
    broadcast_timer_(broadcast_interval_in_ms, &broadcast_timer_handle_), 
    check_peer_expired_timer_(
        check_peer_expired_interval_in_ms, &check_peer_expired_on_timer_),
    data_store_(data_store), delete_data_store_(false) {
      cmd_handler_map_[MC_SET_DISCOVER_PORT_REQUEST] = new SetDiscoverPortHandler;
      cmd_handler_map_[MC_DISCOVER_PEER_ERASE_REQUEST] = new DiscoverPeerEraseHandler;
      pull_handler_map_[MC_DHT_ANNOUNCE_REQUEST] = new DHTAnnounceHandler;
      pull_handler_map_[MC_BROADCAST_REQUEST] = new DHTBroadcastHandler;
      pull_handler_map_[MC_CHECK_PEER_EXPIRED_REQUEST] = new CheckPeerExpiredHandler;
      pull_handler_map_[MC_DISCOVER_SET_BACKGROUND_REQUEST] = new SetBackgroundHandler;
      pull_handler_map_[MC_DISCOVER_SET_FOREGROUND_REQUEST] = new SetForegroundHandler;
#ifdef ZS_TEST
      cmd_handler_map_[MC_DISCOVER_CACHE_CLEAR_REQUEST] = new DiscoverCacheClearHandler;
#endif
    }

DiscoverWorker::~DiscoverWorker() {
  assert(udp_socket_ == NULL);
  assert(cmd_socket_ == NULL);
  assert(exit_socket_ == NULL);
  assert(pull_socket_ == NULL);

  for (auto it = cmd_handler_map_.begin();
       it != cmd_handler_map_.end(); ++it) {
    delete it->second;
  }
  cmd_handler_map_.clear();

  if (delete_data_store_ && data_store_ != NULL) {
    delete data_store_;
    data_store_ = NULL;
  }
}

err_t DiscoverWorker::Initialize(int32_t route_port) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  if (data_store_ == NULL) {
    data_store_ = new DiscoverDataStore;
    delete_data_store_ = true;
  }
  zisync_ret = data_store_->Initialize();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("DiscoverDataStore Initialize fail");
    return zisync_ret;
  }

  route_port_ = route_port;

  // init udp socket
  if (udp_socket_ == NULL) {
    string addr;
    StringFormat(&addr, "udp://*:%d", data_store_->GetPort());
    udp_socket_ = new OsUdpSocket(addr);
    assert(udp_socket_ != NULL);

    int ret = udp_socket_->Bind();
    if (ret == EADDRINUSE) {
      ZSLOG_ERROR("port(%" PRId32") has been used", data_store_->GetPort());
      return ZISYNC_ERROR_ADDRINUSE;
    } else if (ret != 0) {
      ZSLOG_ERROR("udp_socket_.Bind() fail");
      return ZISYNC_ERROR_OS_SOCKET;
    }
    /* Enable broadcast */
    /*ret = udp_socket_->EnableBroadcast();
      if (ret == -1) {
      ZSLOG_ERROR("Enable udp broadcast fail");
      return ZISYNC_ERROR_OS_SOCKET;
      }
      */
    /* Enable multicast */
    std::string multicast_addr = "224.0.0.88";
    ret = udp_socket_->EnableMulticast(multicast_addr);
    if (ret == -1) {
      ZSLOG_ERROR("Enable udp broadcast fail");
      return ZISYNC_ERROR_OS_SOCKET;
    }
  }

  const ZmqContext &zmq_context = GetGlobalContext();
  // init cmd_socket_, exit_socket_ and pull_socket_
  cmd_socket_ = new ZmqSocket(zmq_context, ZMQ_ROUTER);
  exit_socket_ = new ZmqSocket(zmq_context, ZMQ_SUB);
  pull_socket_ = new ZmqSocket(zmq_context, ZMQ_PULL);
  assert(cmd_socket_ != NULL && exit_socket_ != NULL && pull_socket_ != NULL);

  zisync_ret = cmd_socket_->Bind(cmd_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    assert(false);  // should never happen
    ZSLOG_ERROR("DisoverWorker bind cmd(%s) fail : %s",
                cmd_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  zisync_ret = exit_socket_->Connect(exit_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    assert(false);  // should never happen
    ZSLOG_ERROR("DisoverWorker connect exit_backend_uri(%s) fail : %s",
                exit_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  zisync_ret = pull_socket_->Bind(pull_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    assert(false);  // should never happen
    ZSLOG_ERROR("DisoverWorker bind pull(%s) fail : %s",
      pull_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  thread_ = new OsWorkerThread("DiscoverWorker", this, false);
  assert(thread_ != NULL);

  int ret = thread_->Startup();
  if (ret != 0) {
    ZSLOG_ERROR("start thread fail");
    return ZISYNC_ERROR_OS_THREAD;
  }

  this->OnTimer();
  if (discover_timer_.Initialize() != 0) {
    ZSLOG_ERROR("discover_timer_ Initialize failed.");
  }

  broadcast_timer_handle_.OnTimer();
  if (broadcast_timer_.Initialize() != 0) {
    ZSLOG_ERROR("broadcast_timer_ Initialize failed.");
  }

  if (check_peer_expired_timer_.Initialize() != 0) {
    ZSLOG_ERROR("check_peer_expired_timer_ Initialize failed.");
  }

  return ZISYNC_SUCCESS;
}

err_t DiscoverWorker::CleanUp() {
  if (broadcast_timer_.CleanUp() != 0) {
    ZSLOG_ERROR("broadcast_timer_ CleanUp() failed.");
  }
  if (discover_timer_.CleanUp() != 0) {
    ZSLOG_ERROR("discover_timer_ CleanUp() failed.");
  }
  if (check_peer_expired_timer_.CleanUp() != 0) {
    ZSLOG_ERROR("discover_timer_ CleanUp() failed.");
  }

  if (thread_ != NULL) {
    int ret = thread_->Shutdown();
    if (ret != 0) {
      ZSLOG_ERROR("shutdown discover worker failed");
    }
    delete thread_;
    thread_ = NULL;
  }

  if (udp_socket_) {
    udp_socket_->Shutdown("rw");
    delete udp_socket_;
    udp_socket_ = NULL;
  }

  if (cmd_socket_) {
    delete cmd_socket_;
    cmd_socket_ = NULL;
  }

  if (exit_socket_) {
    delete exit_socket_;
    exit_socket_ = NULL;
  }

  if (pull_socket_) {
    delete pull_socket_;
    pull_socket_ = NULL;
  }

  if (delete_data_store_ && data_store_ != NULL) {
    delete data_store_;
    data_store_ = NULL;
  }

  return ZISYNC_SUCCESS;
}

err_t DiscoverWorker::SetDiscoverPort(int32_t new_port) {
  // uninit dht first
  if (dht_uninit() != 1) {
    ZSLOG_ERROR("dht_uninit failed.");
  }

  assert(udp_socket_ != NULL);
  string addr;
  StringFormat(&addr, "udp://*:%d", new_port);

  OsUdpSocket new_socket(addr);
  int ret = new_socket.Bind();
  if (ret == EADDRINUSE) {
    ZSLOG_ERROR("port(%" PRId32") has been used", data_store_->GetPort());
    return ZISYNC_ERROR_ADDRINUSE;
  } else if (ret != 0) {
    ZSLOG_ERROR("udp_socket_.Bind(%s) fail", addr.c_str());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  std::string multicast_addr = "224.0.0.88";
  ret = new_socket.EnableMulticast(multicast_addr);
  if (ret == -1) {
    ZSLOG_ERROR("Enable udp broadcast fail");
    return ZISYNC_ERROR_OS_SOCKET;
  }

  udp_socket_->Swap(&new_socket);

  // reinit dht
  ThreadCleanUp();
  ThreadInitialize();

  return ZISYNC_SUCCESS;
}

void DiscoverWorker::DHTPingSupernode() {
  return;
  std::vector<std::string> super_nodes;
  data_store_->GetSuperNodes(&super_nodes);
  struct sockaddr* sa = NULL;
  for (size_t i = 0; i < super_nodes.size(); i++) {
    if (zs::IsAborted()) {
      return;
    }
    OsSocketAddress sock_addr(super_nodes.at(i));
    while ((sa = sock_addr.NextSockAddr()) != NULL) {
      if (sa->sa_family == AF_INET) {
        dht_ping_node(sa, sizeof(struct sockaddr_in));
      } 
      else if (sa->sa_family == AF_INET6) {
        dht_ping_node(sa, sizeof(struct sockaddr_in6));
      }
    } 
  }
}

void DiscoverWorker::CheckPeerExpired() {
  int64_t latest_store_time = OsTimeInS() - peer_expired_time_in_s;
  OperationList op_list;
  for (auto iter = search_result_cache_.begin(); 
       iter != search_result_cache_.end(); iter ++) {
    SearchResult &result = *iter->second;
    const string &info_hash = result.info_hash_hex;
    for (auto node_iter = result.nodes.begin(); 
         node_iter != result.nodes.end();) {
      auto last_iter = node_iter;
      node_iter ++;
      if (last_iter->second->store_time_ < latest_store_time) {
        ZSLOG_INFO("Node(%s) has expired", last_iter->second->host_.c_str());
        op_list.NewDelete(
            TableDHTPeer::URI, 
            "%s = '%s' AND %s = '%s' AND %s = %d AND %s = %d",
            TableDHTPeer::COLUMN_INFO_HASH, info_hash.c_str(),
            TableDHTPeer::COLUMN_PEER_HOST, last_iter->second->host_.c_str(),
            TableDHTPeer::COLUMN_PEER_PORT, last_iter->second->port_,
            TableDHTPeer::COLUMN_PEER_IS_IPV6, false);
        // ZSLOG_ERROR(
        //     "%s = '%s' AND %s = '%s' AND %s = %d AND %s = %d",
        //     TableDHTPeer::COLUMN_INFO_HASH, info_hash.c_str(),
        //     TableDHTPeer::COLUMN_PEER_HOST, last_iter->second->host_.c_str(),
        //     TableDHTPeer::COLUMN_PEER_PORT, last_iter->second->port_,
        //     TableDHTPeer::COLUMN_PEER_IS_IPV6, false);
        result.nodes.erase(last_iter);
      }
    }
    for (auto node_iter = result.nodes6.begin(); 
         node_iter != result.nodes6.end();) {
      auto last_iter = node_iter;
      node_iter ++;
      if (last_iter->second->store_time_ < latest_store_time) {
        ZSLOG_INFO("Node(%s) has expired", last_iter->first.c_str());
        op_list.NewDelete(
            TableDHTPeer::URI, 
            "%s = '%s' AND %s = '%s' AND %s = %d AND %s = %d",
            TableDHTPeer::COLUMN_INFO_HASH, info_hash.c_str(),
            TableDHTPeer::COLUMN_PEER_HOST, last_iter->second->host_.c_str(),
            TableDHTPeer::COLUMN_PEER_PORT, last_iter->second->port_,
            TableDHTPeer::COLUMN_PEER_IS_IPV6, false);
        result.nodes6.erase(last_iter);
      }
    }
  }

  GetContentResolver()->ApplyBatch(ContentProvider::AUTHORITY, &op_list); 
}

static void AddrsIntoChars(
    std::vector<struct sockaddr_in>& ipv4, std::string* str_ipv4,
    std::vector<struct sockaddr_in6>& ipv6, std::string* str_ipv6) {

  for (auto it = ipv4.begin(); it != ipv4.end(); ++it) {
    str_ipv4->append(reinterpret_cast<const char*>(&it->sin_addr), 4);
  }

  for (auto it = ipv6.begin(); it != ipv6.end(); ++it) {
    str_ipv6->append(reinterpret_cast<const char*>(&it->sin6_addr), 16);
  }
}

void DiscoverWorker::DHTAnnounce() {
  err_t ret = ZISYNC_SUCCESS;

  std::vector<struct sockaddr_in> ipv4;
  std::vector<struct sockaddr_in6> ipv6;
  ListIpAddresses(&ipv4, &ipv6);
  std::string str_ipv4, str_ipv6;
  AddrsIntoChars(ipv4, &str_ipv4, ipv6, &str_ipv6);

  std::vector<std::string> infohashes_bin;
  ret = data_store_->GetInfoHashes(&infohashes_bin);
  assert(ret == ZISYNC_SUCCESS);

  for (auto it = infohashes_bin.begin(); it != infohashes_bin.end(); ++it) {
    auto it2 = announce_items_.find(*it);
    if (it2 == announce_items_.end()) {
      announce_items_[*it] = 0;
      it2 = announce_items_.find(*it);
    }

    if (it2->second == 0) {
      it2->second = 1;
      unsigned char* info_hash =
          reinterpret_cast<unsigned char*>(const_cast<char*>(it->data()));
      if (dht_search(
          info_hash, Config::route_port(), AF_INET,
                     const_cast<char*>(str_ipv4.data()),
                     static_cast<int>(str_ipv4.size()), DHTCallBack, this) == 1) {

        std::string sha1_hex;
        BinToHex('x', *it, &sha1_hex);
        ZSLOG_INFO("DHT Announce %s", sha1_hex.data());
      } else {
        ZSLOG_ERROR("dht_search failed.");
      }
    }
  }
}
static void Broadcast(std::string &info_hash_bin, struct sockaddr_in &sa,
                      int salen) {
#ifdef _WIN32
  std::vector<SOCKADDR_IN> ipv4;
  std::vector<SOCKADDR_IN6> ipv6;
  ListIpAddresses(&ipv4, &ipv6);

  USHORT sin_port = htons(Config::discover_port());

  if (sa.sin_family == AF_INET) {
    for (size_t i = 0; i < ipv4.size(); i++) {
      SOCKADDR_IN& sin = ipv4[i];
      sin.sin_port = sin_port;
      // sin.sin_addr.s_addr = inet_addr("10.88.1.125");
      SOCKET socket4 = socket(AF_INET, SOCK_DGRAM, 0);
      if (socket4 == -1) {
        ZSLOG_ERROR("socket failed!");
        return;
      }

      int optval = 1;
      if (setsockopt(socket4, SOL_SOCKET, SO_REUSEADDR, 
                     (const char*)&optval, sizeof(int)) < 0) {
        ZSLOG_ERROR("setsockopt failed!");
        return;
      }

      if (bind(socket4, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        ZSLOG_ERROR("bind failed!");
        continue;
      }

      int socket_fd = static_cast<int>(socket4); // safe cast
      broadcast(
          info_hash_bin.data(),
          Config::route_port(),
          reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa), socket_fd);
      closesocket(socket4);
    }
  } else {
    // sa.sin_family = AF_INET6;
    for (size_t i = 0; i < ipv6.size(); i++) {
      SOCKADDR_IN6& sin6 = ipv6[i];
      sin6.sin6_port = sin_port;

      SOCKET socket6 = socket(AF_INET6, SOCK_DGRAM, 0);
      if (socket6 == -1) {
        ZSLOG_INFO("socket failed!");
        return;
      }

      if (bind(socket6, (struct sockaddr *)&sin6, sizeof(sin6)) != 0) {
        ZSLOG_INFO("bind failed!");
        continue;
      }

      int socket_fd = static_cast<int>(socket6);  // safe cast
      broadcast(
          info_hash_bin.data(),
          Config::route_port(),
          reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa), socket_fd);
      closesocket(socket6);
    }
  }
#else
  broadcast(
      info_hash_bin.data(),
      Config::route_port(),
      reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa), -1);
#endif 
  //ZSLOG_INFO("broadcast, info_hash:%s, route_port:%d, discover_port:%d\n",
  //       account_hex.substr(4).data(), route_port_, Config::discover_port());
}

void DiscoverWorker::DHTBroadcast() {
#ifdef ZS_TEST
  if (!Config::is_broadcast_enabled()) {
    return;
  }
#endif
  struct sockaddr_in send_sa;
  send_sa.sin_family = AF_INET;
  send_sa.sin_addr.s_addr = inet_addr("224.0.0.88");
  send_sa.sin_port = htons(Config::discover_port());

  std::vector<std::string> infohashes_bin;
  err_t zisync_t = ZISYNC_SUCCESS;
  zisync_t = data_store_->GetInfoHashes(&infohashes_bin);
  assert(zisync_t == ZISYNC_SUCCESS);

  for (auto it = infohashes_bin.begin(); it != infohashes_bin.end(); ++it) {
    Broadcast(*it, send_sa, sizeof(send_sa));
  }
}

void DiscoverWorker::DHTBroadcastReply(std::string sa, std::string info_hash,
                                       int port) {
  broadcast_reply(
      reinterpret_cast<unsigned char*>(const_cast<char*>(info_hash.data())),
      port,
      reinterpret_cast<struct sockaddr*>(const_cast<char*>(sa.data())),
      static_cast<int>(sa.size()));
}

void DiscoverWorker::DHTPeriodic() {
  dht_periodic(NULL, 0, NULL, 0, &tosleep_time_, DHTCallBack, this);
}

void DiscoverWorker::ThreadInitialize() {
  // dht init
  std::string id = data_store_->GetMyNodeId();
  unsigned char v[4] = "ZIS";
  int ret = -1;
  const unsigned char* id2= reinterpret_cast<const unsigned char*>(id.data());
  assert(udp_socket_ != NULL);
  if ((ret = dht_init(udp_socket_->fd(), -1, id2, v)) < 0) {
    // @Bug here: if return, then something is not initialized.
    ZSLOG_ERROR("dht_init() fail.");
    return;
  }

  DHTPingSupernode();

  DHTBroadcast();

  DHTAnnounce();
}

void DiscoverWorker::ThreadCleanUp() {
  // dht cleanup
  dht_uninit();

  search_result_cache_.clear();
}

int DiscoverWorker::Run() {
  ThreadInitialize();
  next_periodic_time_in_ms_ = 
      OsTimeInMs() + static_cast<int64_t>(tosleep_time_) * 1000;

  while (1) {
    int64_t timeout_in_ms = next_periodic_time_in_ms_ - OsTimeInMs();
    if (timeout_in_ms < 0) {
      timeout_in_ms = 0;
    }

    int udp_socket_fd = udp_socket_ == NULL ? -1 : udp_socket_->fd();
    zmq_pollitem_t items[] = {
      { NULL, udp_socket_fd ,  ZMQ_POLLIN, 0 },
      { cmd_socket_->socket(),  0, ZMQ_POLLIN, 0 },
      { pull_socket_->socket(), 0, ZMQ_POLLIN, 0 },   
      { exit_socket_->socket(), 0, ZMQ_POLLIN, 0 },
    };

    int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t), 
                       static_cast<int>(timeout_in_ms * 1000));
    if (ret == -1) {
      ZSLOG_ERROR(
          "zmq_poll failed for discover worker: %s", zmq_strerror(zmq_errno()));
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      HandleUdpMsg();
    }
    
    if (items[1].revents & ZMQ_POLLIN) {
      MessageContainer container(cmd_handler_map_, true);
      container.RecvAndHandleSingleMessage(*cmd_socket_, this);
      //
      // MUST call dht_periodic here, 
      // otherwise deadlock because time elapsed
      //
      DHTPeriodic();
    }

    if (items[2].revents & ZMQ_POLLIN) {
      MessageContainer container(pull_handler_map_, false);
      container.RecvAndHandleSingleMessage(*pull_socket_, this);
      //
      // MUST call dht_periodic here,
      // otherwise deadlock because time elapsed
      //
      DHTPeriodic();
    }

    if (items[3].revents & ZMQ_POLLIN) {
      break;
    }

    if (OsTimeInMs() > next_periodic_time_in_ms_) {
      dht_periodic(NULL, 0, NULL, 0, &tosleep_time_, DHTCallBack, this);
      next_periodic_time_in_ms_ = 
          OsTimeInMs() + static_cast<int64_t>(tosleep_time_) * 1000;
    }
  }

  ThreadCleanUp();

  return 0;
}

void DiscoverWorker::OnTimer() {
#ifdef ZS_TEST
  if (!Config::is_dht_announce_enabled()) {
    return;
  }
#endif
  err_t zisync_ret = ZISYNC_SUCCESS;
  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = req.Connect(IDiscoverWorker::pull_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connet to discover port fail: %s",
                zisync_strerror(zisync_ret));
    return;
  }
  DhtAnnounceRequest request;
  zisync_ret = request.SendTo(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send DHTAnnouncetRequest fail : %s",
                zisync_strerror(zisync_ret));
  }
}

void DiscoverWorker::HandleUdpMsg() {
  string buffer, src_addr;
  assert(udp_socket_ != NULL);
  int nbytes = udp_socket_->RecvFrom(&buffer, 0, &src_addr);
  if (nbytes < 0) {
    ZSLOG_ERROR("udp_socket_ receive message failed.");
    return;
  }
  struct sockaddr from_addr;
  memcpy(&from_addr,
         reinterpret_cast<sockaddr*>(const_cast<char*>(src_addr.data())),
         src_addr.length());
  dht_periodic(buffer.data(), (int)buffer.size(), &from_addr,
               sizeof(from_addr), &tosleep_time_, DHTCallBack, this);
  next_periodic_time_in_ms_ = 
      OsTimeInMs() + static_cast<int64_t>(tosleep_time_) * 1000;
}

bool DiscoverWorker::ShouldStoreInfohash(const std::string &infohash) {
  err_t zisync_t = ZISYNC_SUCCESS;
  std::vector<std::string> infohashes;
  zisync_t = data_store_->GetInfoHashes(&infohashes);
  if (zisync_t != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetInfoHashes failed!");
  }

  return std::find(
    infohashes.begin(), infohashes.end(), infohash) != infohashes.end();
}

SearchResult* DiscoverWorker::FindSerachResult(const std::string& info_hash) {
  unique_ptr<SearchResult> &result = search_result_cache_[info_hash];
  if (!result) {
    std::string info_hash_hex;
    BinToHex('x', info_hash, &info_hash_hex);
    result.reset(new SearchResult(info_hash_hex.c_str()));
  }
  return result.get();
}

void DiscoverWorker::StorePeer(const std::string& addr, const std::string& info_hash,
                               SearchResult* search_result, bool is_lan) {
  auto find = search_result->nodes.find(addr);
  if (find == search_result->nodes.end()) {  // new
    std::string host;
    int port = 0;
    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = 
        *reinterpret_cast<const short*>(addr.substr(4, 2).data());
    sockaddr.sin_addr = 
        *reinterpret_cast<const struct in_addr*>(addr.substr(0, 4).data()); 
    if (SockAddrToString(&sockaddr, &host) != 0) {
      ZSLOG_ERROR("transform ipv4 addr to str failed.");
    } else {
      ZSLOG_INFO("Found DHTPeer %s", host.data());
      unsigned short int nport;
      memcpy(&nport, addr.substr(4, 2).data(), 2);
      port = ntohs(nport);
      
      SearchNode* node_ = new SearchNode(host.c_str(), port);
      node_->is_lan_ = is_lan;
      assert(node_ != NULL);
      search_result->nodes[addr].reset(node_);

      std::string info_hash_hex;
      BinToHex('x', info_hash, &info_hash_hex);
      data_store_->StorePeer(info_hash_hex, host, port, is_lan, false);
    }
  } else {
    SearchNode &node = *find->second;
    find->second->store_time_ = OsTimeInS();
    if (is_lan == true && node.is_lan_ == false) {
      node.is_lan_ = is_lan;
      std::string info_hash_hex;
      BinToHex('x', info_hash, &info_hash_hex);
      data_store_->StorePeer(
          info_hash_hex, node.host_, node.port_, is_lan, false);
    }
  }
}

void DiscoverWorker::StorePeer6(const std::string& addr, const std::string& info_hash,
                                SearchResult* search_result, bool is_lan) {
  auto find = search_result->nodes6.find(addr);
  if (find == search_result->nodes6.end()) {  // new
    std::string host;
    int port = 0;
    struct sockaddr_in6 sockaddr;
    sockaddr.sin6_family = AF_INET6;
    sockaddr.sin6_port = 
        *reinterpret_cast<const short*>(addr.substr(16, 2).data());
    sockaddr.sin6_addr = 
        *reinterpret_cast<const in6_addr*>(addr.substr(0, 16).data()); 
    if (SockAddrToString(
            reinterpret_cast<sockaddr_in*>(&sockaddr), &host) != 0) {
      ZSLOG_ERROR("transform ipv6 addr to str failed.");
    } else {
      unsigned short int nport;
      memcpy(&nport, addr.substr(16, 2).data(), 2);
      port = ntohs(nport);

      SearchNode* node_ = new SearchNode(host.c_str(), port);
      assert(node_ != NULL);
      search_result->nodes6[addr].reset(node_);

      std::string info_hash_hex;
      BinToHex('x', info_hash, &info_hash_hex);
      data_store_->StorePeer(info_hash_hex, host, port, is_lan, true);
    }
  } else { 
    SearchNode &node = *find->second;
    find->second->store_time_ = OsTimeInS();
    if (is_lan == true && node.is_lan_ == false) {
      std::string info_hash_hex;
      BinToHex('x', info_hash, &info_hash_hex);
      data_store_->StorePeer(
          info_hash_hex, node.host_, node.port_, is_lan, true);
    }
  }

}

void DiscoverWorker::OnDHTEvent(
    int event, unsigned char *info_hash, void *data, size_t data_len) {
  std::string item_sha1;
  std::unordered_map<std::string, int>::iterator it;
  int data_size = static_cast<int>(data_len); 
  switch (event) {
    case DHT_EVENT_BROADCAST_REQUEST:
#ifdef ZS_TEST
      if (!Config::is_broadcast_enabled()) {
        break;
      }
#endif
      {
        //ZSLOG_INFO("DHT_EVENT_BROADCAST_REQUEST\n");
        if (ShouldStoreInfohash(
                std::string(reinterpret_cast<char*>(info_hash), 20))) {
          broadcast_reply(info_hash, Config::route_port(),
                          reinterpret_cast<struct sockaddr*>(data), data_size);
          //ZSLOG_INFO("send broadcast reply, info_hash:%s\n", info_hash);
        }
      }
      break;
    case DHT_EVENT_BROADCAST_REPLY:
#ifdef ZS_TEST
      if (!Config::is_broadcast_enabled()) {
        break;
      }
#endif
      {
        const char* d = static_cast<char*>(data);
        std::string addr(d, 6);
        if (ShouldStoreInfohash(
                std::string(reinterpret_cast<char*>(info_hash), 20))) {
          std::string info_hash2(reinterpret_cast<char*>(info_hash), 20);
          SearchResult* search_result = FindSerachResult(info_hash2);
          StorePeer(addr, info_hash2, search_result, true);
        } else {
          std::string info_hash2(TableDHTPeer::INFO_HASH_STRANGER);
          SearchResult* search_result = FindSerachResult(info_hash2);
          StorePeer(addr, info_hash2, search_result, true);
        }
      }
      break;
    case DHT_EVENT_BROADCAST6_REPLY:
#ifdef ZS_TEST
      if (!Config::is_broadcast_enabled()) {
        break;
      }
#endif
      {
        const char* d = static_cast<char*>(data);
        std::string addr(d, 18);
        if (ShouldStoreInfohash(
                std::string(reinterpret_cast<char*>(info_hash), 20))) {
          std::string info_hash2(
              const_cast<const char*>(reinterpret_cast<char*>(info_hash)), 20);
          SearchResult* search_result = FindSerachResult(info_hash2);
          StorePeer6(addr, info_hash2, search_result, true);
        } else {
          std::string info_hash2(TableDHTPeer::INFO_HASH_STRANGER);
          SearchResult* search_result = FindSerachResult(info_hash2);
          StorePeer6(addr, info_hash2, search_result, true);
        }
      }
      break;
    case DHT_EVENT_VALUES:
      {
        std::string info_hash2(
            const_cast<const char*>(reinterpret_cast<char*>(info_hash)), 20);
        SearchResult* search_result = FindSerachResult(info_hash2);
        const char* d = static_cast<char*>(data);
        int nodes_num = data_size / 6;
        for (int i = 0; i < nodes_num; i++) {
          std::string addr(d + i * 6, 6);

          StorePeer(addr, info_hash2, search_result, 0);
        }
      }
      break;
    case DHT_EVENT_VALUES6:
      {
        std::string info_hash2(
            const_cast<const char*>(reinterpret_cast<char*>(info_hash)), 20);
        SearchResult* search_result = FindSerachResult(info_hash2);
        const char* d = static_cast<char*>(data);

        int nodes_num = data_size / 18;
        for (int i = 0; i < nodes_num; i++) {
          std::string addr(d + i * 18, 18);

          StorePeer6(addr, info_hash2, search_result, false);
        }
      }
      break;

    case DHT_EVENT_SEARCH_DONE:
    case DHT_EVENT_SEARCH_DONE6:
      item_sha1.assign(reinterpret_cast<char*>(info_hash), 20);
      if (announce_items_.find(item_sha1) != announce_items_.end()) {
        announce_items_[item_sha1] = 0;
      }
      break;

    default:
      break;
  }
}

#ifdef ZS_TEST
err_t DiscoverWorker::SearchCacheClear() {
  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  err_t zisync_ret = req.Connect(IDiscoverWorker::cmd_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  DiscoverCacheClearRequest request;
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);

  DiscoverCacheClearResponse response;
  return response.RecvFrom(req);
}
#endif 

err_t DiscoverWorker::PeerErase(const MsgDiscoverPeerEraseRequest &request) {
  string info_hash_bin;
  HexToBin(request.info_hash_hex(), &info_hash_bin);
  auto find = search_result_cache_.find(info_hash_bin);
  IContentResolver *resolver = GetContentResolver();
  if (find != search_result_cache_.end()) {
    SearchResult *result = find->second.get();
    if (request.erase_all()) {
      result->nodes.clear();
      result->nodes6.clear();
      resolver->Delete(
          TableDHTPeer::URI, "%s = '%s'",
          TableDHTPeer::COLUMN_INFO_HASH, request.info_hash_hex().c_str());
    } else {
      OperationList op_list;
      for (int i = 0; i < request.peers_size(); i ++) {
        const MsgUri &uri = request.peers(i);
        op_list.NewDelete(
            TableDHTPeer::URI, "%s = '%s' AND %s = '%s' AND %s = %d",
            TableDHTPeer::COLUMN_INFO_HASH, request.info_hash_hex().c_str(),
            TableDHTPeer::COLUMN_PEER_HOST, uri.host().c_str(),
            TableDHTPeer::COLUMN_PEER_PORT, uri.port());
        if (uri.is_ipv6()) {
          auto iter = std::find_if(
              result->nodes6.begin(), result->nodes6.end(),
              [uri] (const std::pair<const string, unique_ptr<SearchNode>> &node)
              { return node.second->host_ == uri.host() && 
              node.second->port_ == uri.port(); });
          if (iter != result->nodes6.end()) {
            result->nodes6.erase(iter);
          }
        } else {
          auto iter = std::find_if(
              result->nodes.begin(), result->nodes.end(),
              [uri] (const std::pair<const string, unique_ptr<SearchNode>> &node)
              { return node.second->host_ == uri.host() && 
              node.second->port_ == uri.port(); });
          if (iter != result->nodes.end()) {
            result->nodes.erase(iter);
          }
        }
      }
      resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
    }
  }
  return ZISYNC_SUCCESS;
}

err_t DiscoverWorker::SetBackground() {
  broadcast_timer_.CleanUp();
  discover_timer_.CleanUp();
  check_peer_expired_timer_.CleanUp();
  if (udp_socket_) {
    udp_socket_->Shutdown("rw");
    delete udp_socket_;
    udp_socket_ = NULL;
  }
  return ZISYNC_SUCCESS;
}

err_t DiscoverWorker::SetForeground() {
  if (udp_socket_ == NULL) {
    string addr;
    StringFormat(&addr, "udp://*:%d", data_store_->GetPort());
    udp_socket_ = new OsUdpSocket(addr);
    assert(udp_socket_ != NULL);
    
    int ret = udp_socket_->Bind();
    if (ret == EADDRINUSE) {
      ZSLOG_ERROR("port(%" PRId32") has been used", data_store_->GetPort());
      delete udp_socket_;
      udp_socket_ = NULL;
      return ZISYNC_ERROR_ADDRINUSE;
    } else if (ret != 0) {
      ZSLOG_ERROR("udp_socket_.Bind() fail");
      delete udp_socket_;
      udp_socket_ = NULL;
      return ZISYNC_ERROR_OS_SOCKET;
    }
    /* Enable broadcast */
    /*ret = udp_socket_->EnableBroadcast();
      if (ret == -1) {
      ZSLOG_ERROR("Enable udp broadcast fail");
      return ZISYNC_ERROR_OS_SOCKET;
      }
      */
    /* Enable multicast */
    std::string multicast_addr = "224.0.0.88";
    ret = udp_socket_->EnableMulticast(multicast_addr);
    if (ret == -1) {
      ZSLOG_ERROR("Enable udp broadcast fail");
      delete udp_socket_;
      udp_socket_ = NULL;
      return ZISYNC_ERROR_OS_SOCKET;
    }
    ret = dht_reset_socket(udp_socket_->fd(), -1);
    if (ret == -1) {
      ZSLOG_ERROR("dht_reset_socket fail");
      delete udp_socket_;
      udp_socket_ = NULL;
      return ZISYNC_ERROR_OS_SOCKET;
    }
  }

  this->OnTimer();
  if (discover_timer_.Initialize() != 0) {
    ZSLOG_WARNING("discover_timer_ Initialize failed.");
  }

  broadcast_timer_handle_.OnTimer();
  if (broadcast_timer_.Initialize() != 0) {
    ZSLOG_WARNING("broadcast_timer_ Initialize failed.");
  }

  if (check_peer_expired_timer_.Initialize() != 0) {
    ZSLOG_WARNING("check_peer_expired_timer_ Initialize failed.");
  }
  return ZISYNC_SUCCESS;
}

}  // namespace zs
