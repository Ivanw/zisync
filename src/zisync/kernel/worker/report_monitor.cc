// Copyright 2014 zisync.com

#include <iostream>

#include "zisync/kernel/worker/report_monitor.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/request.h"

namespace zs {

const int kreport_ui_moniter_interval_in_ms = 3600000;

ReportUIMonitor g_ReportUIMonitor;

ReportMonitor* GetUIEventMonitor() {
  return &g_ReportUIMonitor; 
}

ReportUIMonitor::ReportUIMonitor()
: report_timer_(kreport_ui_moniter_interval_in_ms, &report_timer_handler_) {
}

ReportUIMonitor::~ReportUIMonitor() {
}

err_t ReportUIMonitor::Initialize() {
  report_cache_mutex_.Initialize();

  if (report_timer_.Initialize() != 0) {
    ZSLOG_ERROR("Initialize report_timer fail : %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_TIMER;
  }
  return ZISYNC_SUCCESS;
}

err_t ReportUIMonitor::CleanUp() {
  if (report_timer_.CleanUp() != 0) {
    ZSLOG_ERROR("report_timer_ CleanUp() failed.");
  }

  report_cache_mutex_.CleanUp();
  return ZISYNC_SUCCESS;
}

bool ReportUIMonitor::Report(std::string actionname) {
  MutexAuto mutex(&report_cache_mutex_);
  report_cache_.push_back(UIActionInfo(actionname, OsTimeInS()));
  return true;
}

void ReportTimerHandler::OnTimer() {
  err_t zisync_ret = ZISYNC_SUCCESS;
  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  zisync_ret = req.Connect(router_inner_pull_fronter_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connet to InnerWorker fail: %s",
                zisync_strerror(zisync_ret));
    return;
  }

  ReportUiMonitorRequest request;
  zisync_ret = request.SendTo(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send ReportUiMonitorRequset fail : %s",
                zisync_strerror(zisync_ret));
  }
}

bool ReportUIMonitor::SendData() {
  if (report_cache_.size() == 0) return true;
  std::vector<UIActionInfo> tmp;
  {
    MutexAuto mutex(&report_cache_mutex_);
    tmp.swap(report_cache_);
  }
  std::string actions;
  actions.append("[");
  MutexAuto mutex(&report_cache_mutex_);
  for (size_t i = 0; i < tmp.size(); i++) {
    if (i != 0) {
      StringAppendFormat(&actions, ", ");
    }
    StringAppendFormat(&actions, "{\"event\":\"%s\", \"time\":\"%" PRId64 "\"}",
                       tmp[i].name_.data(),
                       tmp[i].time_);
  }
  actions.append("]");

  std::string report_host;
  std::string report_uri = kReportUiEventUri;
  size_t first_pos = report_uri.find(':') + 3;
  assert(first_pos != std::string::npos);
  size_t last_pos = report_uri.rfind(':');
  assert(last_pos != std::string::npos);

  report_host = report_uri.substr(first_pos, last_pos - first_pos);

  std::string buffer;
  StringFormat(&buffer, "POST /v1/events/%s/%s/%s HTTP/1.1\r\n"
               "Host: %s\r\n" "Content-Type: text/json\r\n" 
               "Content-Length: %d\r\n" "\r\n" "%s",
               Config::device_uuid().data(), GetPlatformWithString().data(),
               zisync_version.c_str(), report_host.c_str(), static_cast<int>(actions.size()),
               actions.data());
  
  OsTcpSocket client_socket(kReportUiEventUri);
  if (client_socket.Connect() == -1) {
    ZSLOG_ERROR("OsTcpSocket Connect failed: %s",
                client_socket.uri().data());
    return false;
  }
  if (client_socket.Send(buffer, 0) < 0) {
    ZSLOG_ERROR("OsTcpSocket Send failed!");
    return false;
  }
  client_socket.Shutdown("w");

  fdbuf fd_buf(&client_socket);
  std::istream in(&fd_buf);

  int code = -1;
  std::string line;
  std::getline(in, line, '\n');
  if (!line.empty()) {
    int ret = sscanf(line.c_str(), "HTTP/%*s %d", &code);
    assert(ret > 0);
  }

  if (code != 200) {
    ZSLOG_ERROR("response status not OK.");
    return false;
  }

  // skip headers
  while (!in.fail()) {
    std::getline(in, line, '\n');
    if (line == "\r") {
      break;
    }
  }
  bool is_success = false;
  while (!in.fail()) {
    getline(in, line, '\n');
    if (!line.empty() && line.at(line.size() - 1) == '\r') {
      line.erase(line.size() - 1);
    }
    if (line == "success") {
      is_success = true;
    }
  }
  if (is_success != true) {
    MutexAuto mutex(&report_cache_mutex_);
    report_cache_.swap(tmp);
    copy(tmp.begin(), tmp.end(), back_inserter(report_cache_));
  }

  return true;
}

}  // namespace zs
