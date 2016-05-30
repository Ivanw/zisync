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
#include <tuple>

#include <unordered_map>

#ifndef ZISYNC_XCODE_BUILD
extern "C"{
#include "zisync/kernel/libevent/dht.h"
}
#else 
#include "zisync/kernel/libevent/dht.h"
#endif

#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/libevent/discover_server.h"
#include "zisync/kernel/libevent/discover_data_source.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/libevent/libevent_base.h"
#include "zisync/kernel/sync_const.h"

void TransformUriToAddr(const char *dht_nodes, struct sockaddr_in *addr);

namespace zs {

using std::vector;

// const int UDP_BUF_SIZE = 4096;
const int broadcast_interval          = 10;     // 10s
const int dht_announce_interval       = 18;     // 18s
const int ping_supernode_interval     = 60;     // 60s
const int peer_expired_time_in_ms     = 360000; // 360s
const int check_peer_expired_interval = 60;     // 60s 
const int static_addrs_interval       = 10;     // 10s

static DiscoverServer s_instance(NULL);

static void DHTCallBack(
    void *closure, int event, unsigned char *info_hash,
    void *data, size_t data_len) {
  auto worker = static_cast<zs::DiscoverServer*>(closure);
  worker->OnDHTEvent(event, info_hash, data, data_len);
}

static void DispatchAnnounce(void* ctx) {
#ifdef ZS_TEST
  if (!Config::is_dht_announce_enabled()) {
    return;
  }
#endif
  DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
  server->DoDHTAnnounce();
  struct timeval tv = { dht_announce_interval, 0 };
  server->evbase()->DispatchAsync(DispatchAnnounce, ctx, &tv);
}

static void DispatchBroadcast(void* ctx) {
#ifdef ZS_TEST
  if (!Config::is_broadcast_enabled()) {
    return;
  }
#endif
  DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
  server->DoDHTBroadcast();
  struct timeval tv = { broadcast_interval, 0 };
  server->evbase()->DispatchAsync(DispatchBroadcast, ctx, &tv);
}

static void DispatchCheckPeerExpired(void* ctx) {
  DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
  server->DoCheckPeerExpired();
  struct timeval tv = { peer_expired_time_in_ms / 1000, 0 };
  server->evbase()->DispatchAsync(DispatchCheckPeerExpired, ctx, &tv);
}

static void DispatchPingSuperNode(void* ctx) {
  DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
  server->DoDHTPingSuperNode();
  struct timeval tv = { ping_supernode_interval, 0 };
  server->evbase()->DispatchAsync(DispatchPingSuperNode, ctx, &tv);
}

static void DispatchStaticAddrs(void* ctx) {
  DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
  server->DoStaticAddrs();
  struct timeval tv = { static_addrs_interval, 0 };
  server->evbase()->DispatchAsync(DispatchStaticAddrs, ctx, &tv);
}

IDiscoverServer* IDiscoverServer::GetInstance() {
  return &s_instance;
}

DiscoverServer* DiscoverServer::GetInstance() {
  return &s_instance;
}

class SuperNode : public IDnsEventDelegate
                , public IBufferEventDelegate {
 public:
  SuperNode(std::vector<std::string>& trackers);
  ~SuperNode();
  void PingSuperNode();
  void PingTracker();

  //
  // Implment IBufferEventDelegate
  //
  virtual void OnRead(struct bufferevent* bev);
  virtual void OnWrite(struct bufferevent* bev);
  virtual void OnEvent(struct bufferevent* bev, short event);

  //
  // Implment IDnsEventDelegate
  //
  virtual void OnDnsEvent(int result, evutil_addrinfo* addr);
 private:
  enum parse_status { PARSE_BEGIN, PARSE_HEAD, PARSE_DOUBLE_EOL };

  bufferevent* buf_event_;
  DnsEvent* dns_event_;

  int parse_status_;
  size_t tracker_index_;
  std::vector<std::string> trackers_;
};

SuperNode::SuperNode(std::vector<std::string>& trackers)
    : buf_event_(NULL), dns_event_(NULL)
    , parse_status_(PARSE_HEAD), tracker_index_(0) {
  trackers_ = trackers;
}

SuperNode::~SuperNode() {
  if (dns_event_) {
    delete dns_event_;
    dns_event_ = NULL;
  }
}

void SuperNode::PingSuperNode() {
  if (tracker_index_ >= trackers_.size()) {
    return;
  }
  if (dns_event_) {
    delete dns_event_;
    dns_event_ = NULL;
  }

  UrlParser url(trackers_[tracker_index_]);

  evutil_addrinfo hints;
  hints.ai_family   = AF_INET;
  hints.ai_flags    = EVUTIL_AI_CANONNAME;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  dns_event_ = new DnsEvent(this, url.host(), url.service(),  hints);
  dns_event_->AddToBase(GetEventBase());
  tracker_index_++;
}

void SuperNode::PingTracker() {
  std::vector<sockaddr_in> host_ipv4;
  std::vector<sockaddr_in6> host_ipv6;
  if (ListIpAddresses(&host_ipv4, &host_ipv6) != 0) {
    ZSLOG_WARNING("Failed to ListIpAddress");
  }

  struct evbuffer* b2 = evbuffer_new();
  evbuffer_add_printf(b2, "[");
  for (size_t i = 0; i < host_ipv4.size(); i++) {
    if (i != 0) {
      evbuffer_add_printf(b2, ", ");
    }
    const char* host = inet_ntoa(host_ipv4[i].sin_addr);
    evbuffer_add_printf(
        b2, "{\"uri\":\"udp://%s:%d\"}", host, Config::discover_port());
  }
  evbuffer_add_printf(b2, "]");

  UrlParser url(kTrackerUri);

  int content_length = (int) evbuffer_get_length(b2);
  struct evbuffer* b1 = evbuffer_new();
  evbuffer_add_printf(b1, "POST /supernode HTTP/1.1\r\n");
  evbuffer_add_printf(b1, "Host: %s\r\n", url.host().c_str());
  evbuffer_add_printf(b1, "Content-Type: text/json\r\n");
  evbuffer_add_printf(b1, "Content-Length: %d\r\n", content_length);
  evbuffer_add_printf(b1, "\r\n");

  buf_event_ = bufferevent_socket_new(
      GetEventBase()->event_base(), -1, BEV_OPT_CLOSE_ON_FREE);
  assert(buf_event_);

  // bufferevent_setcb(
  //     buf_event_, 
  //     [](struct bufferevent *bev, void *ctx) {
  //       reinterpret_cast<SuperNode*>(ctx)->OnRead(bev);
  //     },
  //     [](struct bufferevent *bev, void *ctx) {
  //       reinterpret_cast<SuperNode*>(ctx)->OnWrite(bev);
  //     },
  //     [](struct bufferevent *bev, short what, void *ctx) {
  //       reinterpret_cast<SuperNode*>(ctx)->OnEvent(bev, what);
  //     },
  //     this);
  bufferevent_setcb(
      buf_event_, 
      LambdaOnRead<SuperNode>,
      LambdaOnWrite<SuperNode>,
      LambdaOnEvent<SuperNode>,
      this);


  if (bufferevent_enable(buf_event_, EV_READ | EV_WRITE) == -1) {
    ZSLOG_ERROR("bufferevent_enable error");
  }

  struct evbuffer* output = bufferevent_get_output(buf_event_);
  assert(output);
  if (evbuffer_add_buffer(output, b1) == -1) {
    ZSLOG_ERROR("failed to evbuffer_add_buffer.");
    assert(0);
  }

  if (evbuffer_add_buffer(output, b2) == -1) {
    ZSLOG_ERROR("failed to evbuffer_add_buffer.");
    assert(0);
  }

  struct evdns_base* dns_base = GetEventBase()->evdns_base(); 
  bufferevent_socket_connect_hostname(
      buf_event_, dns_base, AF_INET, url.host().c_str(), url.port());
}

void SuperNode::OnRead(struct bufferevent* bev) {
  int ret = -1;
  assert(bev);
  std::unique_ptr<char, decltype(free)*> line(NULL, free);
  struct evbuffer* input = bufferevent_get_input(bev); 
  assert(input);

  char host[128] = {0};
  int port = 0;
  struct sockaddr_in sin;
   
  do {
    line.reset(evbuffer_readln(input, NULL, EVBUFFER_EOL_CRLF));
    if (line == NULL) return;

    switch (parse_status_) {
      case PARSE_BEGIN:
        if (strcmp(line.get(), "HTTP/1.1 200 OK") == 0) {
          parse_status_ = PARSE_HEAD;
        }
        break;
      case PARSE_HEAD:
        if (strcmp(line.get(), "") == 0) {
          parse_status_ = PARSE_DOUBLE_EOL;
        }
        break;
      case PARSE_DOUBLE_EOL:
        ret = sscanf(line.get(), "udp://%128[^:]:%d", host, &port);
        if (ret == 2) {
          sin.sin_family = AF_INET;
          evutil_inet_pton(AF_INET, host, &sin.sin_addr);
          sin.sin_port = htons(port);
          dht_ping_node((struct sockaddr*)&sin, sizeof(sin));
        }
        break;
      default:
        break;
    }
  } while (1);
}

void SuperNode::OnWrite(struct bufferevent* bev) {
  struct evbuffer* output = bufferevent_get_output(bev);
  if (evbuffer_get_length(output) == 0) {
    bufferevent_disable(bev, EV_WRITE);
  }
}

void SuperNode::OnEvent(struct bufferevent* bev, short event) {
  //
  // connect success
  //
  if (event & BEV_EVENT_CONNECTED) {
    ZSLOG_INFO("connect %s success.", kReportHost); 
    return;
  }

  if (event & BEV_EVENT_EOF) {
    ZSLOG_INFO("Connection closed.\n");
  }
  else if (event & BEV_EVENT_ERROR) {
    /* XXX win32 */
    ZSLOG_ERROR("Got an error on the connection: %s\n", strerror(errno));
  }
  /*
   * None of the other events can happen here, since we haven't
   * enabled timeouts
   */
  bufferevent_free(bev);
}

void SuperNode::OnDnsEvent(int result, evutil_addrinfo* addr) {
  if (result) {
    ZSLOG_ERROR("evdns_getaddrinfo error : %s", evutil_gai_strerror(result));
    return;
  } 

  struct evutil_addrinfo *ai;
  for (ai = addr; ai; ai = ai->ai_next) {
    char buf[128];
    if (ai->ai_family == AF_INET) {
      struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;
      evutil_inet_ntop(AF_INET, &sin->sin_addr, buf, 128);
      dht_ping_node((struct sockaddr*)&sin, sizeof(sin));
    }
    /*else if (ai->ai_family == AF_INET6) {
      struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ai->ai_addr;
      s = evutil_inet_ntop(AF_INET6, &sin6->sin6_addr, buf, 128);
      }*/
  } 
  evutil_freeaddrinfo(addr);
  PingSuperNode();
}

DiscoverServer::DiscoverServer(IDiscoverDataSource* data_source)
    : inner_push(NULL), evbase_(NULL), udp_socket_(NULL), tosleep_time_(0)
    , route_port_(0), delete_data_source_(false), data_source_(data_source)
    , dht_event_(NULL), super_node_(NULL) {
}

DiscoverServer::~DiscoverServer() {
  assert(udp_socket_ == NULL);
  assert(data_source_ == NULL);
  assert(dht_event_ == NULL);
  assert(super_node_ == NULL);
}

err_t DiscoverServer::ReInitUdpSocket(int32_t port) {
  string addr;
  StringFormat(&addr, "udp://*:%d", port);

  std::unique_ptr<OsUdpSocket> new_socket(new OsUdpSocket(addr));
  assert(new_socket != NULL);
  if (evutil_make_socket_nonblocking(new_socket->fd()) == -1) {
    ZSLOG_ERROR("evutil_make_socket_nonblocking fail");
  }

  int ret = new_socket->Bind();
  if (ret == EADDRINUSE) {
    ZSLOG_ERROR("port(%" PRId32") has been used", data_source_->GetDiscoverPort());
    return ZISYNC_ERROR_ADDRINUSE;
  } else if (ret != 0) {
    ZSLOG_ERROR("new_socket_.Bind(%s) fail", addr.c_str());
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
  ret = new_socket->EnableMulticast(multicast_addr);
  if (ret == -1) {
    ZSLOG_ERROR("Enable udp broadcast fail");
    return ZISYNC_ERROR_OS_SOCKET;
  }
  
  if (udp_socket_) {
    udp_socket_->Swap(new_socket.get());
  } else {
    udp_socket_ = new_socket.release();
  }

  return ZISYNC_SUCCESS;
}

err_t DiscoverServer::Initialize(int32_t route_port) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  assert(inner_push == NULL);
  inner_push = new ZmqSocket(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = inner_push->Connect(zs::router_inner_pull_fronter_uri); 
  assert(zisync_ret == ZISYNC_SUCCESS);


  if (data_source_ == NULL) {
    data_source_ = new DiscoverDataSource;
    delete_data_source_ = true;
  }

  route_port_ = route_port;

  // init udp socket
  zisync_ret = ReInitUdpSocket(data_source_->GetDiscoverPort());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  std::vector<std::string> super_nodes;
  data_source_->GetTrackers(&super_nodes);
  super_node_ = new SuperNode(super_nodes);

  return ZISYNC_SUCCESS;
}

err_t DiscoverServer::CleanUp() {
  if (dht_event_) {
    event_free(dht_event_);
    dht_event_ = NULL;
  }
  
  if (super_node_) {
    delete super_node_;
    super_node_ = NULL;
  }

  if (udp_socket_) {
    udp_socket_->Shutdown("rw");
    delete udp_socket_;
    udp_socket_ = NULL;
  }

  if (delete_data_source_ && data_source_ != NULL) {
    delete data_source_;
  }
  data_source_ = NULL;

  if (inner_push != NULL) {
	  delete inner_push;
    inner_push = NULL;
  }

  return ZISYNC_SUCCESS;
}

static void LambdaDHTEvent(evutil_socket_t fd, short events, void* ctx) {
  DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
  server->DhtEventCb(events);
}

err_t DiscoverServer::ScheduleUdpEvent() {
  if (dht_event_) {
    event_free(dht_event_);
    dht_event_ = NULL;
  }
  
  // add udp socket events
  dht_event_ = event_new(
      evbase_->event_base(), udp_socket_->fd(), EV_READ,
      LambdaDHTEvent, this);
  
  srand(time(NULL));
  
  struct timeval tv = { 0, rand() % 1000000 };
  event_add(dht_event_, &tv);

  return ZISYNC_SUCCESS;
}

err_t DiscoverServer::Startup(ILibEventBase* base) {
  evbase_ = base;
  // dht init
  std::string id = data_source_->GetMyNodeId();
  unsigned char v[4] = "ZIS";
  int ret = -1;
  const unsigned char* id2= reinterpret_cast<const unsigned char*>(id.data());
  assert(udp_socket_ != NULL);
  if ((ret = dht_init(udp_socket_->fd(), -1, id2, v)) < 0) {
    // @Bug here: if return, then something is not initialized.
    ZSLOG_ERROR("dht_init() fail.");
    return ZISYNC_ERROR_GENERAL;
  }
#ifdef ENABLE_DHT
  base->DispatchAsync(DispatchPingSuperNode,this, NULL);
  base->DispatchAsync(DispatchAnnounce, this, NULL);
#endif
  base->DispatchAsync(DispatchBroadcast, this, NULL);
  base->DispatchAsync(DispatchCheckPeerExpired, this, NULL);
  base->DispatchAsync(DispatchStaticAddrs, this, NULL);

  // add udp socket events
  ScheduleUdpEvent();
  
  return ZISYNC_SUCCESS; 
}

err_t DiscoverServer::Shutdown(ILibEventBase* base) {
  if (dht_event_) {
    event_free(dht_event_);
    dht_event_ = NULL;
  }

  // dht cleanup
  dht_uninit();
  search_result_cache_.clear();

  
  return ZISYNC_SUCCESS; 
}

static void LambdaSetForeground(void* ctx) {
  reinterpret_cast<DiscoverServer*>(ctx)->DoSetForeground();
}

void DiscoverServer::SetForeground() {  
  evbase_->DispatchAsync(
      LambdaSetForeground, this, NULL);
}

static void LambdaSetBackground(void* ctx) {
  reinterpret_cast<DiscoverServer*>(ctx)->DoSetBackground();
}
void DiscoverServer::SetBackground() {
  evbase_->DispatchAsync(
      LambdaSetBackground, this, NULL);
}

static void LambdaIssueBroadcast(void* ctx) {
  reinterpret_cast<DiscoverServer*>(ctx)->DoDHTBroadcast();
}

void DiscoverServer::IssueBroadcast() {
  evbase_->DispatchAsync(
      LambdaIssueBroadcast, this, NULL);
}

static void LambdaIssueAnnounce(void* ctx) {
  reinterpret_cast<DiscoverServer*>(ctx)->DoDHTAnnounce();
}
/*
static void LambdaIssueStaticAddrs(void* ctx) {
  reinterpret_cast<DiscoverServer*>(ctx)->DoStaticAddrs();
}
*/
void DiscoverServer::IssueAnnounce() {
  evbase_->DispatchAsync(
      LambdaIssueAnnounce, this, NULL);
}

static void LambdaPeerErase(void* ctx) {
  DiscoverServer* server;
  MsgDiscoverPeerEraseRequest* msg;
  std::tie(server, msg) = 
    *(std::tuple<DiscoverServer*, MsgDiscoverPeerEraseRequest*>*)ctx;

  server->DoPeerErase(msg);

  delete msg;
  delete (std::tuple<DiscoverServer*, MsgDiscoverPeerEraseRequest*>*)ctx;
}

void DiscoverServer::PeerErase(const MsgDiscoverPeerEraseRequest& msg) {
  auto request = new MsgDiscoverPeerEraseRequest(msg);
  auto context = new std::tuple<DiscoverServer*,
                            MsgDiscoverPeerEraseRequest*>(this, request);
  evbase_->DispatchAsync(
    LambdaPeerErase, context, NULL);
}

err_t LambdaSetDiscoverPort(void* ctx) {
  auto context = (std::tuple<int, DiscoverServer*>*)ctx;
  return std::get<1>(*context)->DoSetDiscoverPort(std::get<0>(*context));
}
err_t DiscoverServer::SetDiscoverPort(int32_t new_port) {
  std::tuple<int, DiscoverServer*> ctx(new_port, this);
  return evbase_->DispatchSync(LambdaSetDiscoverPort, &ctx);
}

err_t DiscoverServer::GetStaticPeers(std::vector<IpPort> *staticaddrs) {
  return data_source_->GetStaticPeers(staticaddrs);
}

err_t DiscoverServer::AddStaticPeers(const std::vector<IpPort> &staticaddrs) {
  err_t zisync_ret = ZISYNC_SUCCESS;
  IssueStaticPeers(staticaddrs);
  zisync_ret = data_source_->StoreStaticPeers(staticaddrs);
  return zisync_ret;
}

err_t DiscoverServer::DeleteStaticPeers(const std::vector<IpPort> &staticaddrs) {
  return data_source_->DeleteStaticPeers(staticaddrs);
}

err_t DiscoverServer::SaveStaticPeers(const std::vector<IpPort> &staticaddrs) {
  std::vector<IpPort> old_ones;
  data_source_->GetStaticPeers(&old_ones);

  std::vector<IpPort> new_ones;
  for (auto it = staticaddrs.begin(); it != staticaddrs.end(); ++it) {
    auto iter = std::find_if(
        old_ones.begin(), old_ones.end(),
        [it] (const IpPort &item)
        { return item.ip_ == it->ip_ && item.port_ == it->port_ ;});
    if (iter == old_ones.end()) {
      new_ones.push_back(*it);
    }
  }
  IssueStaticPeers(new_ones);
  return data_source_->SaveStaticPeers(staticaddrs);
}

#ifdef ZS_TEST
err_t DiscoverServer::SearchCacheClear() {
  evbase_->DispatchAsync(
      [](void* ctx) {
        DiscoverServer* server = reinterpret_cast<DiscoverServer*>(ctx);
        server->SearchCacheClearCb();
      }, this, NULL);
  return ZISYNC_SUCCESS;
}

void DiscoverServer::SearchCacheClearCb() {
  search_result_cache_.clear();
}
#endif 

err_t DiscoverServer::DoSetDiscoverPort(int new_port) {
  err_t zisync_ret = SetDiscoverPortInter(new_port);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("woker->SetDiscoverPort(%" PRId32 ") fail : %s",
                new_port, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  zisync_ret = SaveDiscoverPort(new_port);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("SaveDiscoverPort fail.");
    return zisync_ret;
  }
  Config::set_discover_port(new_port);

  return ZISYNC_SUCCESS;
}

void DiscoverServer::DoDHTPingSuperNode() {
  assert(super_node_);
  super_node_->PingSuperNode();
  super_node_->PingTracker();
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

void DiscoverServer::DoDHTAnnounce() {
  err_t ret = ZISYNC_SUCCESS;
  std::vector<struct sockaddr_in> ipv4;
  std::vector<struct sockaddr_in6> ipv6;
  ListIpAddresses(&ipv4, &ipv6);
  std::string str_ipv4, str_ipv6;
  AddrsIntoChars(ipv4, &str_ipv4, ipv6, &str_ipv6);

  std::vector<std::string> infohashes_bin;
  ret = data_source_->GetInfoHashes(&infohashes_bin);
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
  
  int ret_val = -1;
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
        ZSLOG_ERROR("bind failed: %s", strerror(errno));
        continue;
      }

      int socket_fd = static_cast<int>(socket4); // safe cast
      ret_val = broadcast(
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
      ret_val = broadcast(
          info_hash_bin.data(),
          Config::route_port(),
          reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa), socket_fd);
      closesocket(socket6);
    }
  }
#else
  ret_val = broadcast(
      info_hash_bin.data(),
      Config::route_port(),
      reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa), -1);
#endif 
  if (ret_val < 0) {
    ZSLOG_ERROR("Broadcast failed: %s", strerror(errno));
  }
}

void DiscoverServer::DoDHTBroadcast() {
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
  assert(data_source_);
  zisync_t = data_source_->GetInfoHashes(&infohashes_bin);
  assert(zisync_t == ZISYNC_SUCCESS);

  for (auto it = infohashes_bin.begin(); it != infohashes_bin.end(); ++it) {
    Broadcast(*it, send_sa, sizeof(send_sa));
  }
}

void DiscoverServer::DhtEventCb(short event) {
  if (event & EV_READ) {
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
  }

  if (event & EV_TIMEOUT) {
    dht_periodic(NULL, 0, NULL, 0, &tosleep_time_, DHTCallBack, this);
  }
  
  
  struct timeval tv = {tosleep_time_, 0};
  event_add(dht_event_, &tv);
}

bool DiscoverServer::ShouldStoreInfohash(const std::string &infohash) {
  err_t zisync_t = ZISYNC_SUCCESS;
  std::vector<std::string> infohashes;
  zisync_t = data_source_->GetInfoHashes(&infohashes);
  if (zisync_t != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("GetInfoHashes failed!");
  }

  return std::find(
      infohashes.begin(), infohashes.end(), infohash) != infohashes.end();
}

SearchResult* DiscoverServer::FindSerachResult(const std::string& info_hash) {
  unique_ptr<SearchResult> &result = search_result_cache_[info_hash];
  if (!result) {
    std::string info_hash_hex;
    BinToHex('x', info_hash, &info_hash_hex);
    result.reset(new SearchResult(info_hash_hex.c_str()));
  }
  return result.get();
}

void DiscoverServer::StorePeers(const std::vector<std::string> &addrs,
                               const std::string& info_hash, bool is_lan) {
  std::vector<SearchNode> update_peers;
  std::vector<SearchNode> insert_peers;
  SearchResult* search_result = FindSerachResult(info_hash);
  std::string info_hash_hex;
  
  for (size_t i = 0; i < addrs.size(); i++) {
    auto find = search_result->nodes.find(addrs[i]);
    if (find == search_result->nodes.end()) {  // new
      std::string host;
      int port = 0;
      struct sockaddr_in sockaddr;
      sockaddr.sin_family = AF_INET;
      sockaddr.sin_port = 
          *reinterpret_cast<const short*>(addrs[i].substr(4, 2).data());
      sockaddr.sin_addr = 
          *reinterpret_cast<const struct in_addr*>(addrs[i].substr(0, 4).data()); 
      if (SockAddrToString(&sockaddr, &host) != 0) {
        ZSLOG_ERROR("transform ipv4 addr to str failed.");
      } else {
        ZSLOG_INFO("Found DHTPeer %s", host.data());
        unsigned short int nport;
        memcpy(&nport, addrs[i].substr(4, 2).data(), 2);
        port = ntohs(nport);

        SearchNode* node = new SearchNode(host.c_str(), port);
        node->is_lan_ = is_lan;
        assert(node != NULL);
        search_result->nodes[addrs[i]].reset(node);

        BinToHex('x', info_hash, &info_hash_hex);

        insert_peers.push_back(*node);
      }
    } else {
      SearchNode &node = *find->second;
      find->second->store_time_ = OsTimeInS();
      if (is_lan == true && node.is_lan_ == false) {
        node.is_lan_ = is_lan;
        BinToHex('x', info_hash, &info_hash_hex);
        
        update_peers.push_back(node);
      }
    }
  }
  update_peers.insert(update_peers.end(), insert_peers.begin(), insert_peers.end());
  if (!update_peers.empty()) {
    data_source_->StorePeers(info_hash_hex, false, update_peers);
  }
  if (!insert_peers.empty()) {
    IssueInsertNode(info_hash_hex, false, insert_peers);
  }
}

void DiscoverServer::StorePeer6s(const std::vector<std::string> &addrs,
                                const std::string& info_hash, bool is_lan) {
  std::vector<SearchNode> update_peers;
  std::vector<SearchNode> insert_peers;
  SearchResult* search_result = FindSerachResult(info_hash);
  std::string info_hash_hex;

  for (size_t i = 0; i < addrs.size(); i++) {
    auto find = search_result->nodes6.find(addrs[i]);
    if (find == search_result->nodes6.end()) {  // new
      std::string host;
      int port = 0;
      struct sockaddr_in6 sockaddr;
      sockaddr.sin6_family = AF_INET6;
      sockaddr.sin6_port = 
          *reinterpret_cast<const short*>(addrs[i].substr(16, 2).data());
      sockaddr.sin6_addr = 
          *reinterpret_cast<const in6_addr*>(addrs[i].substr(0, 16).data()); 
      if (SockAddrToString(
              reinterpret_cast<sockaddr_in*>(&sockaddr), &host) != 0) {
        ZSLOG_ERROR("transform ipv6 addr to str failed.");
      } else {
        unsigned short int nport;
        memcpy(&nport, addrs[i].substr(16, 2).data(), 2);
        port = ntohs(nport);

        SearchNode* node = new SearchNode(host.c_str(), port);
        assert(node != NULL);
        search_result->nodes6[addrs[i]].reset(node);

        BinToHex('x', info_hash, &info_hash_hex);

        insert_peers.push_back(*node);
      }
    } else { 
      SearchNode &node = *find->second;
      find->second->store_time_ = OsTimeInS();
      if (is_lan == true && node.is_lan_ == false) {
        BinToHex('x', info_hash, &info_hash_hex);

        update_peers.push_back(node);
      }
    }
  }
  update_peers.insert(update_peers.end(), insert_peers.begin(), insert_peers.end());
  data_source_->StorePeers(info_hash_hex, true, update_peers);
  IssueInsertNode(info_hash_hex, true, insert_peers);
}

err_t DiscoverServer::SetDiscoverPortInter(int32_t new_port) {
  err_t rc = ZISYNC_SUCCESS;
  rc = ReInitUdpSocket(new_port);
  if (rc != ZISYNC_SUCCESS) {
    return rc;
  }

  // reinit dht
  dht_reset_socket(udp_socket_->fd(), -1);
  search_result_cache_.clear();

  ScheduleUdpEvent();

  return ZISYNC_SUCCESS;
}

void DiscoverServer::IssueInsertNode(const std::string info_hash, const bool is_ipv6,
                                     const std::vector<SearchNode> &peers) {
  for (size_t i = 0; i < peers.size(); i++) {
    // only insert need to DeviceInfo
    string user_sha1;
    Sha1Hex(data_source_->GetAccount(), &user_sha1);
    if (user_sha1 == info_hash) {
      IssueDeviceInfo(peers[i].host_, peers[i].port_, NULL, is_ipv6);
      // IssuePushDeviceInfo(host, port, NULL, is_ipv6);
    } else {
      std::string uuid_hex;
      std::vector<std::string> sync_uuids;
      err_t zisync_t = ZISYNC_SUCCESS;
      zisync_t = data_source_->GetShareSyncs(&sync_uuids);
      if (zisync_t != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("GetShareSynce failed.");
        continue;
      }
      size_t size = sync_uuids.size();
      for (size_t j = 0; j < size; ++j) {
        Sha1Hex(sync_uuids[j], &uuid_hex);
        if (info_hash == uuid_hex) {
          IssueDeviceInfo(peers[i].host_, peers[i].port_, sync_uuids[i].c_str(),
                          is_ipv6);
          // IssuePushDeviceInfo(host, port, sync_uuids[i].c_str(), is_ipv6);
          continue;
        }
      }
    }
  }
}

void DiscoverServer::IssueDeviceInfo(
    const std::string& host, int port, const char *sync_uuid, 
    bool is_ipv6) {
  if (inner_push == NULL) {
    return;
  }
  ZSLOG_INFO("Send IssueDeviceInfoRequest(%s:%d)", host.c_str(), port);
  IssueDeviceInfoRequest request;
  MsgUri *uri = request.mutable_request()->mutable_uri();
  uri->set_host(host);
  uri->set_port(port);
  uri->set_is_ipv6(is_ipv6);
  if (sync_uuid != NULL) {
    request.mutable_request()->set_sync_uuid(sync_uuid);
  }
  err_t zisync_ret = request.SendTo(*inner_push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void DiscoverServer::IssueStaticPeers(const std::vector<IpPort> &staticaddrs) {
  for (auto it = staticaddrs.begin(); it != staticaddrs.end(); ++it) {
    DoStaticPeersStep(*it);
  }
}

void DiscoverServer::OnDHTEvent(
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
        std::vector<std::string> addrs;
        addrs.push_back(addr);

        if (ShouldStoreInfohash(
                std::string(reinterpret_cast<char*>(info_hash), 20))) {
          std::string info_hash2(reinterpret_cast<char*>(info_hash), 20);
          StorePeers(addrs, info_hash2, true);
        } else {
          std::string info_hash2(TableDHTPeer::INFO_HASH_STRANGER);
          StorePeers(addrs, info_hash2, true);
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
        std::vector<std::string> addrs;
        addrs.push_back(addr);

        if (ShouldStoreInfohash(
                std::string(reinterpret_cast<char*>(info_hash), 20))) {
          std::string info_hash2(
              const_cast<const char*>(reinterpret_cast<char*>(info_hash)), 20);
          StorePeer6s(addrs, info_hash2, true);
        } else {
          std::string info_hash2(TableDHTPeer::INFO_HASH_STRANGER);
          StorePeer6s(addrs, info_hash2, true);
        }
      }
      break;
    case DHT_EVENT_VALUES:
      {
        std::string info_hash2(
            const_cast<const char*>(reinterpret_cast<char*>(info_hash)), 20);
        const char* d = static_cast<char*>(data);
        int nodes_num = data_size / 6;
        std::vector<std::string> addrs;
        for (int i = 0; i < nodes_num; i++) {
          std::string addr(d + i * 6, 6);
          addrs.push_back(addr);
        }
        StorePeers(addrs, info_hash2, false);
      }
      break;
    case DHT_EVENT_VALUES6:
      {
        std::string info_hash2(
            const_cast<const char*>(reinterpret_cast<char*>(info_hash)), 20);
        const char* d = static_cast<char*>(data);

        int nodes_num = data_size / 18;
        std::vector<std::string> addrs;
        for (int i = 0; i < nodes_num; i++) {
          std::string addr(d + i * 18, 18);
          addrs.push_back(addr);
        }
        StorePeer6s(addrs, info_hash2, false);
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

void DiscoverServer::DoPeerErase(MsgDiscoverPeerEraseRequest* request) {
  string info_hash_bin;
  HexToBin(request->info_hash_hex(), &info_hash_bin);
  auto find = search_result_cache_.find(info_hash_bin);
  IContentResolver *resolver = GetContentResolver();
  if (find != search_result_cache_.end()) {
    SearchResult *result = find->second.get();
    if (request->erase_all()) {
      result->nodes.clear();
      result->nodes6.clear();
      resolver->Delete(
          TableDHTPeer::URI, "%s = '%s'",
          TableDHTPeer::COLUMN_INFO_HASH,
          request->info_hash_hex().c_str());
    } else {
      OperationList op_list;
      for (int i = 0; i < request->peers_size(); i ++) {
        const MsgUri &uri = request->peers(i);
        op_list.NewDelete(
            TableDHTPeer::URI, "%s = '%s' AND %s = '%s' AND %s = %d",
            TableDHTPeer::COLUMN_INFO_HASH,
            request->info_hash_hex().c_str(),
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
}

err_t DiscoverServer::DoSetForeground() {
  if (udp_socket_ == NULL) {
    string addr;
    StringFormat(&addr, "udp://*:%d", data_source_->GetDiscoverPort());
    udp_socket_ = new OsUdpSocket(addr);
    assert(udp_socket_ != NULL);
    
    int ret = udp_socket_->Bind();
    if (ret == EADDRINUSE) {
      ZSLOG_ERROR("port(%" PRId32") has been used", data_source_->GetDiscoverPort());
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

  /*
    if (check_peer_expired_timer_.Initialize() != 0) {
    ZSLOG_WARNING("check_peer_expired_timer_ Initialize failed.");
    }
  */
  return ZISYNC_SUCCESS;
}

err_t DiscoverServer::DoSetBackground() {
  if (udp_socket_) {
    udp_socket_->Shutdown("rw");
    delete udp_socket_;
    udp_socket_ = NULL;
  }
  return ZISYNC_SUCCESS;
}

void DiscoverServer::DoCheckPeerExpired() {
  int64_t latest_store_time = OsTimeInS() - peer_expired_time_in_ms / 1000;
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

void DiscoverServer::DoStaticAddrs() {
  err_t zisync_ret = ZISYNC_SUCCESS;
  std::vector<IpPort> staticaddrs;
  zisync_ret = data_source_->GetStaticPeers(&staticaddrs);
  assert(zisync_ret == ZISYNC_SUCCESS);
  
  for (auto it = staticaddrs.begin(); it != staticaddrs.end(); ++it) {
    DoStaticPeersStep(*it);
  }
}

void DiscoverServer::DoStaticPeersStep(const IpPort &addr) {
  std::vector<std::string> infohashes_bin;
  err_t zisync_t = ZISYNC_SUCCESS;
  assert(data_source_);
  zisync_t = data_source_->GetInfoHashes(&infohashes_bin);
  assert(zisync_t == ZISYNC_SUCCESS);
   
  struct sockaddr_in send_sa;
  send_sa.sin_family = AF_INET;
  send_sa.sin_addr.s_addr = inet_addr(addr.ip_.c_str());
  send_sa.sin_port = htons(addr.port_);

  for (auto it = infohashes_bin.begin(); it != infohashes_bin.end(); ++it) {
    Broadcast(*it, send_sa, sizeof(send_sa));
  }
}

}  // namespace zs
