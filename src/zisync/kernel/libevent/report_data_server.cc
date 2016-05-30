/**
 * @file report_data_server.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief implement of virtual server for report data with libevent.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#include "zisync/kernel/platform/platform.h"
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <string.h>

#include "zisync/kernel/libevent/report_data_server.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/query_cache.h"
#include "zisync/kernel/permission.h"
#include "zisync/kernel/utils/base64.h"
#include "zisync/kernel/utils/url.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/openssl_hostname_validation.h"
#include "zisync/kernel/libevent/hostcheck.h"
#include "zisync/kernel/libevent/libevent_base.h"

namespace zs {

class LambdaCtx {
 public:
  LambdaCtx(ReportDataServer *server, IHttpRequest* request,
            const std::string &scheme) : server_(server),
    request_(request), scheme_(scheme) {
  }

  ~LambdaCtx() {}

  ReportDataServer *server_;
  IHttpRequest* request_;
  std::string scheme_;
};

static const char *get_ssl_error_string() {
  return ERR_error_string(ERR_get_error(), NULL);
}

static void LambdaOnHandleResponse(
                                   const std::string &method, const std::string &content, const std::string &custom_data,
    evhttp_err_t err) {
  if (method == "verify") {
    GetPermission()->Reset(content, custom_data, METHOD_VERIFY);
  } else if (method == "bind") {
    GetPermission()->Reset(content, custom_data, METHOD_BIND);
  } else if (method == "unbind") {
    GetPermission()->Reset(content, custom_data, METHOD_UNBIND);
    
  } else if (method == "error" && content != "feedback") {
    GetPermission()->Reset(content, custom_data, METHOD_ERROR, err);
  } else if (method == "feedback") {
    /* nothing to do */
  } else if (method == "report_mactoken") {
    /* nothing to do */
  } else if (method == "report") {
    /* nothing to do */
  }
  
  if (GetLicences()->ShouldBindTrial()) {
    ReportDataServer::GetInstance()->DelayAndBind();
  }
}

static inline void http_request_done(struct evhttp_request *req, void *arg) {
  IHttpRequest* request = reinterpret_cast<IHttpRequest*>(arg);
  request->HandleRequest(req);
  delete request;
}

static bool GetStatisticDataWithJson(const char*  report_type,
                                     std::string* json_data);
ReportDataServer ReportDataServer::s_instance;

ReportDataServer::ReportDataServer()
    : timer_event_(NULL), base_(NULL), ssl_ctx_(NULL), will_bind_(false) {
}

ReportDataServer::~ReportDataServer() {
  assert(timer_event_ == NULL);
}


static int cert_verify_callback(X509_STORE_CTX *x509_ctx, void *arg)
{
	const char *host = reinterpret_cast<const char*>(arg);

	/* This is the function that OpenSSL would call if we hadn't called
	 * SSL_CTX_set_cert_verify_callback().  Therefore, we are "wrapping"
	 * the default functionality, rather than replacing it. */
	if (X509_verify_cert(x509_ctx) != 1) {
    ZSLOG_ERROR(
        "X509 verify fail: %s",
        X509_verify_cert_error_string(X509_STORE_CTX_get_error(x509_ctx)));
    return 0;
  } else {
    X509 *server_cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    assert(server_cert != NULL);
    if (validate_hostname(host, server_cert) != MatchFound) {
      char * cert_str = X509_NAME_oneline(
          X509_get_subject_name(server_cert), NULL, 0);
      assert(cert_str != NULL);
	    ZSLOG_ERROR("Match hostname(%s) and certificate(%s) fail.",
                  host, cert_str);
      free(cert_str);
		  return 0;
    }
  }

	return 1;
}
  
err_t ReportDataServer::Initialize(std::shared_ptr<IReportDataSource> data_source) {
  data_source_ = data_source;
  // Init SSL
  SSL_library_init();
  ERR_load_crypto_strings();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  if (RAND_poll() == 0) {
    ZSLOG_ERROR("Get rand num fail: %s", get_ssl_error_string());
    return ZISYNC_ERROR_SSL;
  }

  ssl_ctx_ = SSL_CTX_new(SSLv3_method());
  if (!ssl_ctx_) {
    ZSLOG_ERROR("Create ssl ctx fail: %s", get_ssl_error_string());
    return ZISYNC_ERROR_SSL;
  }

  // load ca cert
  std::string temp_str(Config::ca_cert());
  char *in1 = const_cast<char*>(temp_str.data());
  const unsigned char *in2 = reinterpret_cast<unsigned char*>(in1);
  
  cert_ = d2i_X509(NULL, &in2, Config::ca_cert().size());
  if (cert_ == NULL) {
    ZSLOG_ERROR("Get cert fail: %s", get_ssl_error_string());
    return ZISYNC_ERROR_SSL;
  }

  x509_store_ = X509_STORE_new();
  if (x509_store_ == NULL) {
    ZSLOG_ERROR("Create X509_STORE fail: %s", get_ssl_error_string());
    return ZISYNC_ERROR_SSL;
  }

  if (X509_STORE_add_cert(x509_store_, cert_) != 1) {
    ZSLOG_ERROR("Add cert fail: %s", get_ssl_error_string());
    return ZISYNC_ERROR_SSL;
  }

  SSL_CTX_set_cert_store(ssl_ctx_, x509_store_);

  SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, NULL);
  SSL_CTX_set_verify_depth(ssl_ctx_, 1);
  SSL_CTX_set_cert_verify_callback(
      ssl_ctx_, cert_verify_callback, const_cast<char*>(cert_host));

  return ZISYNC_SUCCESS;
}

void ReportDataServer::CleanUp() {
  if (cert_) {
    X509_free(cert_);
    cert_ = NULL;
  }
  //if (x509_store_) {
  //  X509_STORE_free(x509_store_);
  //  x509_store_ = NULL;
  //}
  if (ssl_ctx_ != NULL) {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = NULL;
  }
}

static void LambdaSendRequest(void* ctx) {
  LambdaCtx *lambda_ctx = reinterpret_cast<LambdaCtx*>(ctx);
  lambda_ctx->server_->SendRequest(lambda_ctx->scheme_, lambda_ctx->request_);
  delete lambda_ctx;
}

IHttpRequest *ReportDataServer::CreateVerifyRequest(const std::string &mactoken,
                                                    const std::string &keycode,
                                                    const std::string &data) {
  HttpRequest *request = new HttpRequest(
      http_request_done, "/verify", EVHTTP_REQ_POST,
      LambdaOnHandleResponse, "verify", data);
  request->AddHeader("Host", kReportHost);
  request->AddHeader("Connection", "close");
  request->AddHeader("Content-Type", "appliation/octet-stream");

  Http::VerifyRequest request_msg;
  std::string message;
  std::string json_data;
  if (GetStatisticDataWithJson("start", &json_data) == false) {
    ZSLOG_ERROR("GetStatisticDataWithJson fail.");
    delete request;
    return NULL;
  }
  request_msg.set_stat(json_data);
  request_msg.set_keycode(keycode);
  request_msg.set_mactoken(mactoken);
  if (!request_msg.SerializeToString(&message)) {
    ZSLOG_ERROR("Serialize verify request to string fail.");
    delete request;
    return NULL;
  }
  std::string message_encode64 =
      base64_encode((unsigned char const*)message.c_str(), message.size());
  request->AddContent(message_encode64);
  std::string str_length;
  StringFormat(&str_length, "%d", static_cast<int>(message_encode64.size()));
  request->AddHeader("Content-Length", str_length);

  return request;
}

void ReportDataServer::Verify(
    const std::string &mactoken, const std::string &keycode) {
  IHttpRequest* request = CreateVerifyRequest(mactoken, keycode, keycode);
  if (request == NULL) {
    ZSLOG_ERROR("Create verify requst fail.");
    return;
  }
  LambdaCtx *lambda_ctx = new LambdaCtx(this, request, "https");
  base_->DispatchAsync(LambdaSendRequest, lambda_ctx, NULL); 
}

void ReportDataServer::Bind(
                            const std::string &mactoken, const std::string &keycode, const std::string &data) {
  std::string uri;
  StringFormat(&uri, "/bind/%s/%s", mactoken.c_str(), keycode.c_str());
  GenFixedStringForHttpUri(&uri);
  IHttpRequest* request = new HttpRequest(
      http_request_done, uri, EVHTTP_REQ_GET,
      LambdaOnHandleResponse, "bind", data);
  request->AddHeader("Host", kReportHost);
  request->AddHeader("Connection", "close");
  LambdaCtx *lambda_ctx = new LambdaCtx(this, request, "https");
  base_->DispatchAsync(LambdaSendRequest, lambda_ctx, NULL);
}

void ReportDataServer::Unbind(
                              const std::string &mactoken, const std::string &keycode, const std::string &data) {
  std::string uri;
  StringFormat(&uri, "/unbind/%s/%s", mactoken.c_str(), keycode.c_str());
  GenFixedStringForHttpUri(&uri);
  IHttpRequest* request = new HttpRequest(
      http_request_done, uri, EVHTTP_REQ_GET,
      LambdaOnHandleResponse, "unbind", data);
  request->AddHeader("Host", kReportHost);
  request->AddHeader("Connection", "close");
  LambdaCtx *lambda_ctx = new LambdaCtx(this, request, "https");
  base_->DispatchAsync(LambdaSendRequest, lambda_ctx, NULL);
}

void ReportDataServer::Feedback(
    const std::string &type, const std::string &version,
                                const std::string &message, const std::string &contact, const std::string &data) {
  std::string msg;
  std::string cont;
  if (message.empty()) {
    msg = "NONE";
  } else {
    msg = message;
  }

  if (contact.empty()) {
    cont = "NONE";
  } else {
    cont = contact;
  }
  std::string url_msg, url_cont, url_type, url_version, url_uuid;
  url_msg = UrlEncode(msg);
  url_cont = UrlEncode(cont);
  url_type= UrlEncode(type);
  url_version = UrlEncode(version);
  url_uuid = UrlEncode(Config::device_uuid());

  std::string uri;
  StringFormat(&uri, "/feedback/%s/%s/%s/%s/%s",
               url_uuid.c_str(),
               url_version.c_str(), url_type.c_str(),
               url_msg.c_str(), url_cont.c_str());
  GenFixedStringForHttpUri(&uri);

  IHttpRequest *request = new HttpRequest(
      http_request_done, uri, EVHTTP_REQ_GET,
      LambdaOnHandleResponse, "feedback", data);
  request->AddHeader("Host", kReportHost);
  request->AddHeader("Connection", "close");
  LambdaCtx *lambda_ctx = new LambdaCtx(this, request, "http");
  base_->DispatchAsync(LambdaSendRequest, lambda_ctx, NULL);
}

void ReportDataServer::ReportMactoken(const std::string &mactoken) {
  std::string uri;
  StringFormat(&uri, "/action/first_startup/%s", mactoken.c_str());
  IHttpRequest *request = new HttpRequest(
      http_request_done, uri, EVHTTP_REQ_GET,
      LambdaOnHandleResponse, "report_mactoken");
  request->AddHeader("Host", kReportHost);
  request->AddHeader("Connection", "close");
  LambdaCtx *lambda_ctx = new LambdaCtx(this, request, "https");
  base_->DispatchAsync(LambdaSendRequest, lambda_ctx, NULL);
}

void ReportDataServer::ReportData(const std::string &type) {
  std::string json_data;
  if (GetStatisticDataWithJson(type.c_str(), &json_data) == false) {
    ZSLOG_ERROR("GetStatisticDataWithJson fail.");
    return;
  }
  IHttpRequest *request = new HttpRequest(
      http_request_done, "/report", EVHTTP_REQ_POST,
      LambdaOnHandleResponse, "report");
  request->AddHeader("Host", kReportHost);
  request->AddHeader("Connection", "close");
  request->AddHeader("Content-Type", "application/json");
  request->AddContent(json_data);
  std::string str_length;
  StringFormat(&str_length, "%d", static_cast<int>(json_data.size()));
  request->AddHeader("Content-Length", str_length);
  LambdaCtx *lambda_ctx = new LambdaCtx(this, request, "http");
  base_->DispatchAsync(LambdaSendRequest, lambda_ctx, NULL);
}

err_t ReportDataServer::Startup(ILibEventBase* base) {
  timer_event_ = new TimerEvent(this, true);
  timer_event_->AddToBase(GetEventBase(), REPORT_INTERVAL_IN_MS);

  if (data_source_ == NULL) {
    data_source_.reset(new StatisticDataSource);
    assert(data_source_);
  }
  base_ = base;

  ReportData("start");

  //DelayAndBind();
  return ZISYNC_SUCCESS;
}

err_t ReportDataServer::Shutdown(ILibEventBase* base) {
  ReportData("stop");
  if (timer_event_) {
    delete timer_event_;
    timer_event_ = NULL;
  }

  return ZISYNC_SUCCESS;
}

void ReportDataServer::SendRequest(const std::string &scheme,
                                   IHttpRequest *request) {
  assert(request != NULL);
  struct evhttp_connection *evcon = NULL;
  struct bufferevent *bev = NULL;
  SSL *ssl = NULL;

  if (scheme == "https") {
    ssl = SSL_new(ssl_ctx_);
    if (ssl == NULL) {
      delete request;
      ZSLOG_ERROR("Create ssl for socket fail: %s", get_ssl_error_string());
      return;
    }

    SSL_set_tlsext_host_name(ssl, cert_host);
    bev = bufferevent_openssl_socket_new(
        GetEventBase()->event_base(), -1, ssl, BUFFEREVENT_SSL_CONNECTING,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (bev == NULL) {
      delete request;
      SSL_free(ssl);
      ZSLOG_ERROR("Bufferevent openssl socket new failed.");
      return;
    }

    bufferevent_openssl_set_allow_dirty_shutdown(bev, 1);
    evcon = evhttp_connection_base_bufferevent_new(
        GetEventBase()->event_base(), GetEventBase()->evdns_base(), bev,
        kReportHost, 443);
    request->SetBev(bev);
  } else if (scheme == "http") {
    bev = bufferevent_socket_new(
        GetEventBase()->event_base(), -1, BEV_OPT_CLOSE_ON_FREE);
    if (bev == NULL) {
      ZSLOG_ERROR("Create bufferevent fail.");
      return;
    }
    evcon = evhttp_connection_base_bufferevent_new(
        GetEventBase()->event_base(), GetEventBase()->evdns_base(), bev,
        kReportHost, 80);
    request->SetBev(bev);
  } else {
    assert(0);
  }
  if (evcon == NULL) {
    ZSLOG_ERROR("Create evhttp_connection fail.");
    delete request;
    bufferevent_free(bev);
    if (ssl != NULL) {
      SSL_free(ssl);
    }
    return;
  }

  evhttp_connection_set_timeout(evcon, HTTP_CONNECT_TIMEOUT * 2);
  if (request->SendRequest(evcon) != ZISYNC_SUCCESS) {
    delete request;
    ZSLOG_ERROR("Make http request fail.");
    return;
  }
}

void ReportDataServer::OnTimer(TimerEvent* timer_event) {
  //GetPermission()->CheckExpired();
  //Http::VerifyRequest temp_request;
  //if (GetPermission()->VerifyRequest(&temp_request) == ZISYNC_SUCCESS) {
  //  IHttpRequest* request = CreateVerifyRequest(temp_request.mactoken(),
  //                                              temp_request.keycode());
  //  if (request == NULL) {
  //    ZSLOG_ERROR("Create verify request fail.");
  //    return;
  //  }
  //  
  //  SendRequest("https", request);
  //}
  
  ReportData("period");
}
  
static void bind_timer_handler(evutil_socket_t fd, short event, void *ctx) {
  if (GetLicences()->ShouldBindTrial()) {
    ReportDataServer::GetInstance()->Bind(GetLicences()->mac_address()
                                          , DefaultKeyCode, DefaultKeyCode);
  }
  struct event *tm = (struct event*)ctx;
  event_free(tm);
  ReportDataServer::GetInstance()->SetWillBind(false);
}

void ReportDataServer::DelayAndBind() {
  if (!WillBind() && GetLicences()->ShouldBindTrial()) {
    struct event *bind_timer_out = event_new(GetEventBase()->event_base()
                                             , -1, 0, bind_timer_handler
                                             , event_self_cbarg());
    ReportDataServer::GetInstance()->SetWillBind(true);
    struct timeval tv;
    tv.tv_sec = AUTO_BIND_INTERVAL_S;
    tv.tv_usec = 0;
    event_add(bind_timer_out, &tv);
  }
}
  
static bool GetStatisticDataWithJson(const char*  report_type,
                                     std::string* json_data) {
  std::string rt_type(report_type);
  assert(json_data != NULL);
  json_data->clear();

  QuerySyncInfoResult syncinfo;
  QueryBackupInfoResult backupinfo;

  QueryCache* query_cache = QueryCache::GetInstance();
  assert(query_cache);
  if (query_cache->QuerySyncInfo(&syncinfo) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("QuerySyncInfo fail.");
    return false;
  }

  if (query_cache->QueryBackupInfo(&backupinfo) != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("QueryBackupInfo fail.");
    return false;
  }

  std::string device_uuid;
  int32_t device_type;
  device_uuid = Config::device_uuid();
  device_type = GetPlatformWithNum();

  IContentResolver *resolver = GetContentResolver();
  assert(resolver);
  std::string length_proj;
  StringFormat(&length_proj, "SUM(%s)", TableFile::COLUMN_LENGTH);
  const char* projection_count[] = {"COUNT(*)", length_proj.data()};

  std::string sync_items;
  for (size_t i = 0; i < syncinfo.sync_infos.size(); i++) {
    std::string tree_items;
    int32_t file_count = 0;
    int64_t total_size = 0;
    for (size_t k= 0; k < syncinfo.sync_infos[i].trees.size(); k++) {
      if (syncinfo.sync_infos[i].trees[k].is_local == false) continue;
      {
        std::unique_ptr<ICursor2> cursor_count(resolver->Query(
                TableFile::GenUri(syncinfo.sync_infos[i].trees[k].tree_uuid.c_str()),
                projection_count, 2, "%s = %d and %s = %d",
                TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
                TableFile::COLUMN_TYPE, OS_FILE_TYPE_REG));
        if (!cursor_count->MoveToNext()) {
          continue;
        }
        file_count = cursor_count->GetInt32(0);
        total_size = cursor_count->GetInt64(1);
      }

      if (k != 0) {
        tree_items.append(",");
      }
      StringAppendFormat(&tree_items,
                         "{\"tree_uuid\":\"%s\"," 
                         "\"file_count\":\"%d\","
                         "\"total_size\":\"%" PRId64 "\"}",
                         syncinfo.sync_infos[i].trees[k].tree_uuid.c_str(), file_count,
                         total_size);
    }

    if (i != 0) {
      sync_items.append(",");
    }

    int32_t sync_type = -1;
    if (syncinfo.sync_infos[i].is_share) {
      switch (syncinfo.sync_infos[i].sync_perm) {
        case 1:
          sync_type = 3;
          break;
        case 2:
          sync_type = 4;
          break;
        case 3:
          sync_type = 2;
          break;
        case 4:
          sync_type = 6;
          break;
        default:
          break;
      }
    } else {
      if (syncinfo.sync_infos[i].creator.device_id == TableDevice::LOCAL_DEVICE_ID) {
        sync_type = 5;
      } else {
        sync_type = 1;
      }
    }

    StringAppendFormat(
        &sync_items,
        "{\"sync_uuid\":\"%s\","
        "\"sync_type\":\"%d\","
        "\"trees\":[%s]}",
        syncinfo.sync_infos[i].sync_uuid.c_str(), sync_type, tree_items.c_str());
  }

  std::string backup_items;
  for (size_t i = 0, j = 0; i < backupinfo.backups.size(); i++) {
    if (backupinfo.backups[i].src_tree.is_local == false) {
      continue;
    }
    std::string src_tree_items;
    int32_t file_count = 0;
    int64_t total_size = 0;
    {
      std::unique_ptr<ICursor2> cursor_count(resolver->Query(
              TableFile::GenUri(backupinfo.backups[i].src_tree.tree_uuid.c_str()),
              projection_count, 2, "%s = %d and %s = %d",
              TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL,
              TableFile::COLUMN_TYPE, OS_FILE_TYPE_REG));
      if (!cursor_count->MoveToNext()) {
        continue;
      }
      file_count = cursor_count->GetInt32(0);
      total_size = cursor_count->GetInt64(1);
    }

    std::string dest_tree_items;
    for (size_t k = 0; k < backupinfo.backups[i].target_trees.size(); k++) {
      std::string device_uuid; 
      resolver->QueryString(
          TableDevice::URI, TableDevice::COLUMN_UUID, &device_uuid,
          "%s = %d", TableDevice::COLUMN_ID,
          backupinfo.backups[i].target_trees[k].device.device_id);
      int sync_mode = resolver->QueryInt32(
          TableSyncMode::URI, TableSyncMode::COLUMN_SYNC_MODE, -1,
          "%s = %d and %s = %d",
          TableSyncMode::COLUMN_LOCAL_TREE_ID,
          backupinfo.backups[i].src_tree.tree_id,
          TableSyncMode::COLUMN_REMOTE_TREE_ID, 
          backupinfo.backups[i].target_trees[k].tree_id);
      sync_mode++;  // appear sync_mode number to STAT server data
      std::string tree_uuid;
      tree_uuid = backupinfo.backups[i].target_trees[k].tree_uuid;
      std::string sync_mode_str;

      if (k != 0) {
        dest_tree_items.append(",");
      }
      StringAppendFormat(&dest_tree_items,
                         "{\"device_uuid\":\"%s\","
                         "\"tree_uuid\":\"%s\","
                         "\"backup_mode\":\"%d\"}",
                         device_uuid.c_str(),
                         tree_uuid.c_str(),
                         sync_mode);
    }

    StringAppendFormat(
        &src_tree_items,
        "{\"tree_uuid\":\"%s\","
        "\"file_count\":\"%d\","
        "\"total_size\":\"%" PRId64 "\","
        "\"dest_trees\":[%s]}",
        backupinfo.backups[i].src_tree.tree_uuid.c_str(), file_count,
        total_size, dest_tree_items.c_str());

    int32_t sync_id;
    std::string sync_uuid;
    sync_id = resolver->QueryInt32(
        TableTree::URI, TableTree::COLUMN_SYNC_ID, -1,
        "%s = %d", TableTree::COLUMN_ID,
        backupinfo.backups[i].src_tree.tree_id);
    resolver->QueryString(
        TableSync::URI, TableSync::COLUMN_UUID, &sync_uuid,
        "%s = %d", TableSync::COLUMN_ID, sync_id);

    if (j != 0) {
      backup_items.append(",");
    }
    StringAppendFormat(
        &backup_items,
        "{\"sync_uuid\":\"%s\","
        "\"src_tree\":[%s]}",
        sync_uuid.c_str(), src_tree_items.c_str());
    j++;
  }

  int event_type = -1;
  if (rt_type == "start")
    event_type = 1;
  else if (rt_type == "period")
    event_type = 2;
  else if (rt_type == "stop")
    event_type = 3;

  StringFormat(json_data,
               "{\"device_uuid\":\"%s\","
               "\"mac_token\":\"%s\","
               "\"device_name\":\"%s\","
               "\"device_type\":\"%d\","
               "\"version\":\"%s\","
               "\"version_number\":\"%d\","
               "\"event\":\"%d\","
               "\"syncs\":[%s],"
               "\"backups\":[%s]}",
               device_uuid.c_str(), Config::mac_token().c_str(),
               Config::device_name().c_str(),
               device_type, zisync_version.c_str(),
               version_number, event_type, sync_items.c_str(),
               backup_items.c_str());

  return true;
}

err_t StatisticDataSource::GetVerifyData(
    const char *report_type, std::string *buffer) {
  return ZISYNC_SUCCESS;
}

}  // namespace zs

