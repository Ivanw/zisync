
// Copyright 2014, zisync.com

#include <cassert>
#include <memory>

#include "zisync/kernel/monitor/fs_monitor.h"
#include "zisync/kernel/monitor/monitor.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/ignore.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/utils.h"

namespace zs {

const int64_t REPORT_EVENTS_TO_WORKER_INTERVAL_IN_MS = 2000;
const int64_t CREATE_OR_MODIFY_WAIT_TIME_IN_MS = 100;
const int64_t FILE_MOVE_WAIT_TIME_IN_MS = 100;
const char monitor_listener_uri[] = "inproc://monitor_listener";

using std::unique_ptr;

Reporter::~Reporter() {
  if (reporter != NULL) {
    delete reporter;
    reporter = NULL;
  }
}

err_t Reporter::Initialize() {
  assert(reporter == NULL);
  reporter = new ZmqSocket(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = reporter->Connect(monitor_listener_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

static void MsgReportEventToFsEvent(const MsgReportEvent &msg_event,
                                    FsEvent *fs_event) {
  if (fs_event) {
    fs_event->path = msg_event.path();
    fs_event->file_move_cookie = msg_event.file_move_cookie();
    fs_event->type = ToFsEventType(msg_event.type());
  }
}
  
  void Monitor::ReportFailBack(const string &tree_root, const zs::MsgReportEvent &evt) {
    FsEvent fs_evt;
    MsgReportEventToFsEvent(evt, &fs_evt);
    ReportFailBack(tree_root, fs_evt);
  }
  
  void Monitor::ReportFailBack(const string &tree_root, const FsEvent& evt) {
	  if (reporter_) {
		  reporter_->Report(tree_root, evt);
	  }
  }

int Reporter::Report(const string &watch_dir, const FsEvent &fs_event) {
#ifdef ZS_TEST
  if (!Config::is_monitor_enabled()) {
    return 0;
  }
#endif
  int watch_dir_length = watch_dir.length();
  if (zs::IsSystemRootDir(watch_dir)) {
	  watch_dir_length --;
  }
  if (IsInIgnoreDir((&(*fs_event.path.begin()) + watch_dir_length))) {
//      ZSLOG_INFO("TempDir(%s)", fs_event.path.c_str());
      return 0;
  }
  MonitorReportEventRequest request;
  request.mutable_request()->set_tree_root(watch_dir);
  MsgReportEvent *event = request.mutable_request()->add_events();
  event->set_path(fs_event.path);
  event->set_file_move_cookie(fs_event.file_move_cookie);
  switch (fs_event.type) {
    case FS_EVENT_TYPE_CREATE:
      event->set_type(ET_CREATE);
      break;
    case FS_EVENT_TYPE_MODIFY:
      event->set_type(ET_MODIFY);
      break;
    case FS_EVENT_TYPE_DELETE:
      event->set_type(ET_DELETE);
      break;
    case FS_EVENT_TYPE_ATTRIB:
      event->set_type(ET_ATTRIB);
      break;
    case FS_EVENT_TYPE_MOVE_FROM:
      event->set_type(ET_MOVE_FROM);
      break;
    case FS_EVENT_TYPE_MOVE_TO:
      event->set_type(ET_MOVE_TO);
      break;
    default:
      assert(0);
  }

  err_t zisync_ret = request.SendTo(*reporter);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return 0;
}

  FsEventType ToFsEventType(MsgReportEventType type) {
    switch (type) {
      case ET_CREATE:
        return FS_EVENT_TYPE_CREATE;
      case ET_MODIFY:
        return FS_EVENT_TYPE_MODIFY;
      case ET_DELETE:
        return FS_EVENT_TYPE_DELETE;
      case ET_ATTRIB:
        return FS_EVENT_TYPE_ATTRIB;
      case ET_MOVE_FROM:
        return FS_EVENT_TYPE_MOVE_FROM;
      case ET_MOVE_TO:
        return FS_EVENT_TYPE_MOVE_TO;
      default:
        assert(false);
        return FS_EVENT_TYPE_ATTRIB;
    }
  }

  MsgReportEventType ToMsgReportEventType(FsEventType type) {
    switch (type) {
      case FS_EVENT_TYPE_CREATE:
        return ET_CREATE;
      case FS_EVENT_TYPE_MODIFY:
        return ET_MODIFY;
      case FS_EVENT_TYPE_DELETE:
        return ET_DELETE;
      case FS_EVENT_TYPE_ATTRIB:
        return ET_ATTRIB;
      case FS_EVENT_TYPE_MOVE_FROM:
        return ET_MOVE_FROM;
      case FS_EVENT_TYPE_MOVE_TO:
        return ET_MOVE_TO;
      default:
        assert(false);
        return ET_ATTRIB;
    }
  }

bool Reporter::IsIgnored(const string &path) {
  return IsIgnoreDir(path) || IsIgnoreFile(path);
}

int Reporter::ReportMiss() {
  //if (last_refresh_time == -1 || 
  //    (OsTime() - last_refresh_time) > MISS_REFRESH_INTERVAL) {
  //  
  //} else {

  //}
  IssueAllRefresh();
  return 0;
}

int Reporter::ReportMiss(const string &watch_dir) {
  //if (last_refresh_time == -1 || 
  //    (OsTime() - last_refresh_time) > MISS_REFRESH_INTERVAL) {
  //  
  //} else {

  //}
  unique_ptr<Tree> tree(Tree::GetBy(
          "%s = '%s' AND %s = %d",
          TableTree::COLUMN_ROOT, GenFixedStringForDatabase(watch_dir).c_str(), 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));

  return 0;
}

Monitor Monitor::monitor;

Monitor::Monitor():
    OsThread("Monitor"), listener(NULL), reporter(NULL), exit_sub(NULL),
    fs_monitor(GetFsMonitor()), has_startup(false) {}

err_t Monitor::Startup() {
  if (has_startup) {
    return ZISYNC_SUCCESS;
  }
  const ZmqContext &context = GetGlobalContext();

  err_t zisync_ret;
  if (!listener) {
    listener = new ZmqSocket(context, ZMQ_PULL);
    zisync_ret = listener->Bind(monitor_listener_uri);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  if (!exit_sub) {
    exit_sub = new ZmqSocket(context, ZMQ_SUB);
    zisync_ret = exit_sub->Connect(zs::exit_uri);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  if (!reporter) {
    reporter = new ZmqSocket(context, ZMQ_PUSH);
    zisync_ret = reporter->Connect(zs::router_inner_pull_fronter_uri);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  if (!reporter_) {
    reporter_.reset(new Reporter());
    reporter_->Initialize();
  }

  int ret = fs_monitor->Startup(reporter_.get());
  if (ret != 0) {
    ZSLOG_ERROR("Startup fs_monitor fail.");
    return ZISYNC_ERROR_OS_THREAD;
  }

  if (OsThread::Startup() != 0) {
    ZSLOG_ERROR("Startup thread fail.");
    return ZISYNC_ERROR_OS_THREAD;
  }

  has_startup = true;
  return ZISYNC_SUCCESS;
}
  
err_t Monitor::AddWatchDir(const string &path) {
  if (!has_startup) {
    ZSLOG_ERROR("Monitor has not startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  int ret = fs_monitor->AddWatchDir(path);
  if (ret != 0) {
    return ZISYNC_ERROR_MONITOR;
  }
  return ZISYNC_SUCCESS;
}

err_t Monitor::DelWatchDir(const string &path) {
  if (!has_startup) {
    ZSLOG_INFO("Monitor has not startup.");
    return ZISYNC_ERROR_NOT_STARTUP;
  }
  int ret = fs_monitor->DelWatchDir(path);
  if (ret != 0) {
    return ZISYNC_ERROR_MONITOR;
  }
  return ZISYNC_SUCCESS;
}

void Monitor::HandleReportEventRequest() {
  MonitorReportEventRequest request;
  err_t zisync_ret = request.RecvFrom(*listener);
  assert(zisync_ret == ZISYNC_SUCCESS);
  int64_t cookie;

  for (int i = 0; i < request.request().events_size(); i ++) {

    const MsgReportEvent &event = request.request().events(i);
    assert(event.has_file_move_cookie());
    cookie = event.file_move_cookie();
    ListenerEvent &listener_event = 
        report_events[request.request().tree_root()][cookie][event.path()];
    listener_event.type = event.type();
    if (listener_event.type == ET_CREATE
        || listener_event.type == ET_MODIFY
        || listener_event.type == ET_MOVE_FROM
        || listener_event.type == ET_MOVE_TO) {
      listener_event.last_report_time_in_ms = OsTimeInMs();
    }
  }
}

void Monitor::ReportEventToWorker(bool *has_wait_event) {
  *has_wait_event = false;
  int64_t cookie;
  for (auto tree_root_iter = report_events.begin(); 
      tree_root_iter != report_events.end(); ) {
    MonitorReportEventRequest request;
    request.mutable_request()->set_tree_root(tree_root_iter->first);

    for(auto cookie_iter = tree_root_iter->second.begin();
        cookie_iter != tree_root_iter->second.end(); ) {
      cookie = cookie_iter->first;
      map<string, ListenerEvent> &events_map= cookie_iter->second;
      if (cookie != 0) {
        ReportFileMoveEvents(request, cookie, events_map, has_wait_event);
      }else {
        ReportOtherEvents(request, events_map, has_wait_event);
      }
      if (events_map.empty()) {
        cookie_iter = tree_root_iter->second.erase(cookie_iter);
      }else {
        ++cookie_iter;
      }
    }
    if (tree_root_iter->second.empty()) {
      tree_root_iter = report_events.erase(tree_root_iter);
    }else {
      ++tree_root_iter;
    }

    if (request.request().events_size() != 0) {
      err_t zisync_ret = request.SendTo(*reporter);
      assert(zisync_ret == ZISYNC_SUCCESS);
    }

  }

}

void Monitor::ReportFileMoveEvents(MonitorReportEventRequest &request,
    int64_t cookie, map<string, ListenerEvent> &events_map, bool *has_wait_event) {

  for (auto event_iter = events_map.begin(); 
      event_iter != events_map.end();) {
    auto temp_iter = event_iter;
    event_iter ++;
    if (event_iter == events_map.end()) {
      if (temp_iter->second.last_report_time_in_ms + FILE_MOVE_WAIT_TIME_IN_MS
          < OsTimeInMs()) {
        AddEvent(request, temp_iter->second, cookie, temp_iter->first);
        events_map.erase(temp_iter);
      }else {
        *has_wait_event = true;
      }
    }else {
      if (temp_iter->second.type != event_iter->second.type) {
        AddEvent(request, temp_iter->second, cookie, temp_iter->first);
        AddEvent(request, event_iter->second, cookie, event_iter->first);
        events_map.erase(temp_iter);
        event_iter = events_map.erase(event_iter);
      }else {
        if (temp_iter->second.last_report_time_in_ms + FILE_MOVE_WAIT_TIME_IN_MS
            < OsTimeInMs()) {
          AddEvent(request, temp_iter->second, cookie, temp_iter->first);
          events_map.erase(temp_iter);
        }else {
          *has_wait_event = true;
        }
      }
    }
  }

}

void Monitor::ReportOtherEvents(MonitorReportEventRequest &request,
    map<string, ListenerEvent> &events_map, bool *has_wait_event) {

  for (auto event_iter = events_map.begin(); 
      event_iter != events_map.end();) {
    auto temp_iter = event_iter;
    event_iter ++;
    ListenerEvent &listener_event = temp_iter->second;
    if ((listener_event.type == ET_CREATE || 
          listener_event.type == ET_MODIFY) &&
        ((OsTimeInMs() - listener_event.last_report_time_in_ms) <= 
         CREATE_OR_MODIFY_WAIT_TIME_IN_MS)) {
      *has_wait_event = true;
      continue;
    }
    AddEvent(request, listener_event, 0, temp_iter->first);
    events_map.erase(temp_iter);
  }
}

void Monitor::AddEvent(MonitorReportEventRequest &request,
    const ListenerEvent &event, int64_t cookie, const string &path) {
  MsgReportEvent *report_event = request.mutable_request()->add_events();
  report_event->set_path(path);
  report_event->set_file_move_cookie(cookie);
  report_event->set_type(event.type);
}

int Monitor::Run() {
  int64_t report_time_in_ms = -1, timeout_in_ms;

  while (1) {
    zmq_pollitem_t items[] = {
      { exit_sub->socket(), 0, ZMQ_POLLIN, 0 },
      { listener->socket(), 0, ZMQ_POLLIN, 0 },
    };
    if (report_time_in_ms == -1) {
      timeout_in_ms = -1;
    } else {
      timeout_in_ms = report_time_in_ms - OsTimeInMs(); 
      timeout_in_ms = timeout_in_ms < 0 ? 0 : timeout_in_ms;
    }
    int ret = zmq_poll(items, ARRAY_SIZE(items),
                       static_cast<int>(timeout_in_ms));
    if (ret == -1) {
      continue;
    }
    if (items[0].revents & ZMQ_POLLIN) {
      return 0;
    }
    if (items[1].revents & ZMQ_POLLIN) {
      HandleReportEventRequest();
      if (report_time_in_ms == -1) {
        report_time_in_ms = OsTimeInMs() + 
            REPORT_EVENTS_TO_WORKER_INTERVAL_IN_MS;
      }
    }

    if (report_time_in_ms < OsTimeInMs()) {
      bool has_wait_event;
      ReportEventToWorker(&has_wait_event);
      if (!has_wait_event) {
        report_time_in_ms = -1;
      } else {
        report_time_in_ms = OsTimeInMs() + 
            REPORT_EVENTS_TO_WORKER_INTERVAL_IN_MS;
      }
    }
  }
  return 0;
}

void Monitor::Shutdown() {
  if (!has_startup) {
    return;
  }
  fs_monitor->Shutdown();
  OsThread::Shutdown();
  if (listener != NULL) {
    delete listener;
    listener = NULL;
  }
  if (exit_sub != NULL) {
    delete exit_sub;
    exit_sub = NULL;
  }
  if (reporter != NULL) {
    delete reporter;
    reporter = NULL;
  }
  reporter_.reset(NULL);
  has_startup = false;
}

}  // namespace zs
