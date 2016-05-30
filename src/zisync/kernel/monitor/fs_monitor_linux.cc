// Copyright 2014, zisync.com
#include <sys/inotify.h>
#include <errno.h>
#include <unistd.h>
#include <zmq.h>
#include <fts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <cstring>
#include <string>
#include <map>
#include <memory>
#include <set>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/monitor/fs_monitor.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/context.h"

namespace zs {

using std::string;
using std::map;
using std::shared_ptr;
using std::make_shared;
using std::set;

class MonitorWatchAddHandler : public MessageHandler {
 public:
  virtual ~MonitorWatchAddHandler() {}
  virtual google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgMonitorWatchAddRequest request_msg_;
};

class MonitorWatchDelHandler : public MessageHandler {
 public:
  virtual ~MonitorWatchDelHandler() {}
  virtual google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgMonitorWatchAddRequest request_msg_;
};


const int WATCH_MASK = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM |
IN_MOVED_TO | IN_ATTRIB | IN_MODIFY;


class FsMonitor : public IFsMonitor, public OsThread {
  friend class AddWatchesVisitor;
  friend class DelWatchesVisitor;
  friend class ModifyAndFindVisitor;
  friend class MonitorWatchAddHandler;
  friend class MonitorWatchDelHandler;
 public :
  FsMonitor():OsThread("FsMonitor"), inotify_fd_(-1), reporter_(NULL), 
    req_(NULL), exit_sub_(NULL) {
      msg_handers_[MC_MONITOR_WATCH_ADD_REQUEST] = new MonitorWatchAddHandler;
      msg_handers_[MC_MONITOR_WATCH_DEL_REQUEST] = new MonitorWatchDelHandler;
    }

  ~FsMonitor() {
    if (inotify_fd_ != -1) {
      close(inotify_fd_);
      inotify_fd_ = -1;
    }
    if (req_ != NULL) {
      delete req_;
      req_ = NULL;
    }
    if (exit_sub_ != NULL) {
      delete exit_sub_;
      exit_sub_ = NULL;
    }

    for (auto it = msg_handers_.begin();
         it != msg_handers_.end(); ++it) {
      delete it->second;
    }
    msg_handers_.clear();
  };
  virtual int Startup(IFsReporter* reporter);
  virtual int Run();
  virtual int Shutdown();
  virtual int AddWatchDir(const string& dir_path);
  virtual int DelWatchDir(const string& dir_path);

 private:
  FsMonitor(FsMonitor&);
  void operator=(FsMonitor&);

  class WatchDesc {
   public:
    WatchDesc(int wd, string path, int ref_count):wd(wd), path(path),
    ref_count(ref_count) {}
    int wd;
    const string path; // the path of the inotify dirs, end with "/"
    int ref_count;
  };

  int inotify_fd_;
  IFsReporter *reporter_;
  ZmqSocket *req_, *exit_sub_;
  map<string, shared_ptr<WatchDesc>> path_table; // the string is the path of inoitfy dirs , end with "/"
  map<int, shared_ptr<WatchDesc>> wd_table;
  set<string> watch_dirs; // string of the Watch dirs, which is the tree root, end with "/"
  map<MsgCode, MessageHandler*> msg_handers_;

  err_t AddInotifyWatch(const string &dir_path, bool inc_ref, int init_ref_count = 1);

  err_t DecInotifyWatchRefCount(const string &dir_path);
  err_t DelInotifyWatch(const string &path);
  err_t DelInotifyWatch(const map<string, shared_ptr<WatchDesc>>::iterator &iter);
  /* del due to the event of IN_IGNROED */
  void DelInotifyWatch(int wd);
  /*  recursive */
  err_t AddInotifyWatches(const string &dir_path);
  /*  recursive */
  err_t DelInotifyWatches(const string &dir_path);

  void HandleInotifyEvent();
  void ModifyInotifyWatchesAndFindInotifyEvents(
      const string &path, uint32_t mask);
  /* when a dir moved out of the watch dir , you need delete all subdir of the
   * dir */
  void DelInotifyWatchesOfDeletedDir(const string &dir_path);
  void Report(const FsEvent &fs_event);
};

static FsMonitor fs_monitor;
static const char fs_monitor_req_uri[] = "inproc://fs_monitor_req";

IFsMonitor* GetFsMonitor() {
  return &fs_monitor;
}

int FsMonitor::Startup(IFsReporter *reporter) {
  reporter_ = reporter;
  const ZmqContext &context = GetGlobalContext();
  err_t zisync_ret;

  if (inotify_fd_ == -1) {
    inotify_fd_ = inotify_init();
    if (inotify_fd_ == -1) {
      ZSLOG_ERROR("inotify_init() fail : %s", strerror(errno));
      return -1;
    }
  }

  if (req_ == NULL) {
    req_ = new ZmqSocket(context, ZMQ_ROUTER);
    zisync_ret = req_->Bind(fs_monitor_req_uri);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  if (exit_sub_ == NULL) {
    exit_sub_ = new ZmqSocket(context, ZMQ_SUB);
    zisync_ret = exit_sub_->Connect(zs::exit_uri);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  return OsThread::Startup();
}

int FsMonitor::Run() {
  while (1) {
    zmq_pollitem_t items[] = {
      { exit_sub_->socket(), -1, ZMQ_POLLIN, 0 },
      { NULL, inotify_fd_, ZMQ_POLLIN, 0 },
      { req_->socket(), -1, ZMQ_POLLIN, 0 },
    };

    int ret = zmq_poll (
        items, sizeof(items) / sizeof(zmq_pollitem_t), -1);
    if (ret == -1) {
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      break;
    }

    if (items[1].revents & ZMQ_POLLIN) {
      HandleInotifyEvent();
    }

    if (items[2].revents & ZMQ_POLLIN) {
      MessageContainer container(msg_handers_, true);
      container.RecvAndHandleSingleMessage(*req_, this);
    }
  }

  return 0;
}

int FsMonitor::Shutdown() {
  int ret = OsThread::Shutdown();
  if (ret != 0) {
    return ret;
  }
  if (inotify_fd_ != -1) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }
  if (exit_sub_ != NULL) {
    delete exit_sub_;
    exit_sub_ = NULL;
  }
  if (req_ != NULL) {
    delete req_;
    req_ = NULL;
  }
  path_table.clear();
  wd_table.clear();
  watch_dirs.clear();
  return 0;
}

int FsMonitor::AddWatchDir(const string& dir_path) {
  MonitorWatchAddRequest request;
  MonitorWatchAddResponse response;
  request.mutable_request()->set_path(dir_path);

  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  err_t zisync_ret = req.Connect(fs_monitor_req_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = response.RecvFrom(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Add Watch Dir(%s) fail : %s", dir_path.c_str(), 
                zisync_strerror(zisync_ret));
    return -1;
  }

  return 0;
}
int FsMonitor::DelWatchDir(const string& dir_path) {
  MonitorWatchDelRequest request;
  MonitorWatchDelResponse response;
  request.mutable_request()->set_path(dir_path);

  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  err_t zisync_ret = req.Connect(fs_monitor_req_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = response.RecvFrom(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Del Watch Dir(%s) fail : %s", dir_path.c_str(), 
                zisync_strerror(zisync_ret));
    return -1;
  }

  return 0;
}

class AddWatchesVisitor : public IFsVisitor {
 public:
  AddWatchesVisitor(FsMonitor *monitor):monitor_(monitor) {}
  virtual ~AddWatchesVisitor() {}

  virtual int Visit(const OsFileStat &stat);
  virtual bool IsIgnored(const std::string &path) const;

 private:
  FsMonitor *monitor_;
};

int AddWatchesVisitor::Visit(const OsFileStat &stat) {
  if (stat.type == OS_FILE_TYPE_DIR) {
    monitor_->AddInotifyWatch(stat.path, true);
  }

  return 0;
}

bool AddWatchesVisitor::IsIgnored(const std::string &path) const {
  return monitor_->reporter_->IsIgnored(path);
}

class DelWatchesVisitor : public IFsVisitor {
 public:
  DelWatchesVisitor(FsMonitor *monitor):monitor_(monitor) {}
  virtual ~DelWatchesVisitor() {}

  virtual int Visit(const OsFileStat &stat);
  virtual bool IsIgnored(const std::string &path) const;

 private:
  FsMonitor *monitor_;
};

int DelWatchesVisitor::Visit(const OsFileStat &stat) {
  if (stat.type == OS_FILE_TYPE_DIR) {
    monitor_->DecInotifyWatchRefCount(stat.path);
  }

  return 0;
}

bool DelWatchesVisitor::IsIgnored(const std::string &path) const {
  return monitor_->reporter_->IsIgnored(path);
}

err_t FsMonitor::AddInotifyWatch(
    const string &dir_path_, bool inc_ref, int init_ref_count /*  = 1 */) {
  string dir_path = dir_path_ + "/";
  auto find = path_table.find(dir_path);
  if (find != path_table.end()) {
    if (inc_ref) {
      find->second->ref_count ++;
      ZSLOG_INFO("Add Inotify Watch(%s)'s ref_count to (%d)", dir_path.c_str(),
                 find->second->ref_count);
    }
    return ZISYNC_SUCCESS;
  } else {
    int wd = inotify_add_watch(inotify_fd_, dir_path.c_str(), WATCH_MASK);
    if (wd == -1) {
      ZSLOG_ERROR("inotify_add_watch(%s) fail : %s", dir_path.c_str(),
                  strerror(errno));
      if (errno == ENOENT) {
        return ZISYNC_ERROR_ROOT_MOVED;
      }else {
        return ZISYNC_ERROR_GENERAL;
      }
    } else {
      ZSLOG_INFO("Add Inotify Watch(%s)", dir_path.c_str());
    }

    shared_ptr<WatchDesc> watch_desc = make_shared<WatchDesc>(
        wd, dir_path, init_ref_count);
    path_table.insert(std::make_pair(dir_path, watch_desc));
    wd_table.insert(std::make_pair(wd, watch_desc));
    return ZISYNC_SUCCESS;
  }
}

err_t FsMonitor::DecInotifyWatchRefCount(const string &dir_path_) {
  string dir_path = dir_path_ + "/";
  auto find = path_table.find(dir_path);
  if (find != path_table.end()) {
    if (find->second->ref_count == 1) {
      DelInotifyWatch(find);
    } else {
      find->second->ref_count --;
    }
  }
  return ZISYNC_SUCCESS;
}
err_t FsMonitor::DelInotifyWatch(const string &path) {
  auto find = path_table.find(path + "/");
  if (find != path_table.end()) {
    return DelInotifyWatch(find);
  }
  return ZISYNC_SUCCESS;
}
err_t FsMonitor::DelInotifyWatch(
    const map<string, shared_ptr<WatchDesc>>::iterator &iter) {
  int ret = inotify_rm_watch(inotify_fd_, iter->second->wd);
  if (ret == -1) {
    ZSLOG_ERROR("inotify_rm_watch(%s) fail : %s", 
                iter->second->path.c_str(), strerror(errno));
    return ZISYNC_ERROR_GENERAL;
  } else {
    ZSLOG_INFO("Del Inotify Watch(%s)", iter->second->path.c_str());
  }
  wd_table.erase(iter->second->wd);
  path_table.erase(iter);
  return ZISYNC_SUCCESS;
}

/* del due to the event of IN_IGNROED */
void FsMonitor::DelInotifyWatch(int wd) {
  auto find = wd_table.find(wd);
  if (find != wd_table.end()) {
    ZSLOG_INFO("DelInotifyWatch(%s) due to IN_IGNORED", 
               find->second->path.c_str());
    path_table.erase(find->second->path);
    wd_table.erase(find);
  }
}

err_t FsMonitor::AddInotifyWatches(const string &path) {
  err_t zisync_ret = AddInotifyWatch(path, true);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_WARNING("AddInotifyWatch(%s) fail.", path.c_str());
  }
  AddWatchesVisitor visitor(this);
  OsFsTraverser traverser(path, &visitor);
  int ret = traverser.traverse();
  if (ret != 0) {
    ZSLOG_ERROR("Traverser traverse fail.");
    return ZISYNC_ERROR_GENERAL;
  }
  return ZISYNC_SUCCESS;
}

static inline bool IsOverflowEvent(const struct inotify_event &event) {
  return (event.wd == -1 && (event.mask & IN_Q_OVERFLOW));
}

err_t FsMonitor::DelInotifyWatches(const string &path) {
  err_t zisync_ret = DecInotifyWatchRefCount(path);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_WARNING("DelInotifyWatch(%s) fail.", path.c_str());
  }
  DelWatchesVisitor visitor(this);
  OsFsTraverser traverser(path, &visitor);
  int ret = traverser.traverse();
  if (ret != 0) {
    ZSLOG_ERROR("Traverser traverse fail.");
    return ZISYNC_ERROR_GENERAL;
  }
  return ZISYNC_SUCCESS;
}

void FsMonitor::HandleInotifyEvent() {
  int buf_len;

  int ret = ioctl(inotify_fd_, FIONREAD, &buf_len);
  if (ret != 0) {
    ZSLOG_ERROR("Get Inotify read size fail : %s", strerror(errno));
    return;
  }

  string buf(buf_len, '\0');
  int len = read(inotify_fd_, const_cast<char*>(buf.data()), buf_len);
  if (len == -1) {
    ZSLOG_ERROR("read inotify event fail : %s", strerror(errno));
  }

  int left_buf_len = buf_len;
  const char *buf_iter = buf.data();
  while (left_buf_len > 0) {
    const struct inotify_event *event = 
        reinterpret_cast<const struct inotify_event*>(buf_iter);
    if (IsOverflowEvent(*event)) {
      reporter_->ReportMiss();
    } else {
      if (event->mask & IN_IGNORED) {
        DelInotifyWatch(event->wd);
      } else {
        auto find = wd_table.find(event->wd);
        if (find != wd_table.end()) {
          FsEvent fs_event;
          fs_event.path = find->second->path + event->name;
          fs_event.file_move_cookie = event->cookie;
          if (!reporter_->IsIgnored(fs_event.path)) {
            if (event->mask & IN_CREATE) {
              fs_event.type = FS_EVENT_TYPE_CREATE;
            } else if (event->mask & IN_DELETE) {
              fs_event.type = FS_EVENT_TYPE_DELETE;
            } else if (event->mask & IN_CLOSE_WRITE) {
              fs_event.type = FS_EVENT_TYPE_MODIFY;
            } else if (event->mask & IN_MODIFY) {
              fs_event.type = FS_EVENT_TYPE_MODIFY;
            } else if (event->mask & IN_MOVED_FROM) {
              fs_event.type = FS_EVENT_TYPE_MOVE_FROM;
            } else if (event->mask & IN_MOVED_TO) {
              fs_event.type = FS_EVENT_TYPE_CREATE;
            } else if (event->mask & IN_ATTRIB) {
              fs_event.type = FS_EVENT_TYPE_ATTRIB;
            } else {
              assert(false);
            }
            Report(fs_event);
            ModifyInotifyWatchesAndFindInotifyEvents(
                fs_event.path, event->mask);
          }
        }
      }
    }
    buf_iter += sizeof(struct inotify_event) + event->len;
    left_buf_len -= sizeof(struct inotify_event) + event->len;
    assert(left_buf_len >= static_cast<int>(sizeof(struct inotify_event)) ||
           left_buf_len == 0);
  }
}

class ModifyAndFindVisitor : public IFsVisitor {
 public:
  ModifyAndFindVisitor(FsMonitor *monitor, FsEventType event_type):
      find_event_type_(event_type), monitor_(monitor) {}
  virtual int Visit(const OsFileStat &stat);
  virtual bool IsIgnored(const std::string &path) const;

 private:
  FsEventType find_event_type_;
  FsMonitor *monitor_;
};

int ModifyAndFindVisitor::Visit(const OsFileStat &stat) {
  if (stat.type == OS_FILE_TYPE_DIR) {
    monitor_->AddInotifyWatch(stat.path, false);
  }
  FsEvent fs_event;
  fs_event.type = find_event_type_;
  fs_event.path = stat.path;
  monitor_->Report(fs_event);
  ZSLOG_ERROR("finish visit");
  return 0;
}

bool ModifyAndFindVisitor::IsIgnored(const std::string &path) const {
  return monitor_->reporter_->IsIgnored(path);
}

void FsMonitor::DelInotifyWatchesOfDeletedDir(const string &dir_path) {
  /* @TODO tested it */
  const string &prefix = dir_path;
  string next_prefix = prefix;
  next_prefix[prefix.length() - 1] ++;
  auto begin = path_table.lower_bound(prefix);
  auto end = path_table.lower_bound(next_prefix);
  for (auto iter = begin; iter != end;) {
    auto pre_iter = iter;
    iter ++;
    DelInotifyWatch(pre_iter);
  }
}

void FsMonitor::ModifyInotifyWatchesAndFindInotifyEvents(
    const string &path, uint32_t event_mask) {
  if (event_mask & IN_ISDIR) {
    if (event_mask & IN_CREATE) {
      AddInotifyWatch(path, false);
      ModifyAndFindVisitor visitor(this, FS_EVENT_TYPE_CREATE);
      OsFsTraverser traverser(path, &visitor);
      traverser.traverse();
    } else if (event_mask & IN_DELETE) {
      DelInotifyWatch(path);
    } else if (event_mask & IN_MOVED_FROM) {
      DelInotifyWatchesOfDeletedDir(path);
    } else if (event_mask & IN_MOVED_TO) {
      AddInotifyWatch(path, false);
      ModifyAndFindVisitor visitor(this, FS_EVENT_TYPE_CREATE);
      OsFsTraverser traverser(path, &visitor);
      traverser.traverse();
    }
  } 
}

void FsMonitor::Report(const FsEvent &fs_event) {
  auto begin = watch_dirs.upper_bound(fs_event.path);
  while (begin != watch_dirs.begin()) {
    begin --;
    const string &watch_dir = *begin;
    if (fs_event.path.compare(0, watch_dir.length(), watch_dir) == 0) {
      const string &watch_dir_path = watch_dir.substr(0, watch_dir.size() - 1);
      reporter_->Report(watch_dir_path, fs_event);
      ZSLOG_INFO("Report (%s) in WatchDir(%s)", fs_event.path.c_str(),
                 watch_dir_path.c_str());
    }
  }
}

err_t MonitorWatchAddHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void *userdata) {
  FsMonitor *monitor = reinterpret_cast<FsMonitor*>(userdata);
  err_t zisync_ret = monitor->AddInotifyWatches(request_msg_.path());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
    
  monitor->watch_dirs.insert(request_msg_.path() + "/");
  MonitorWatchAddResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  
  return zisync_ret;
}

err_t MonitorWatchDelHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void *userdata) {
  FsMonitor *monitor = reinterpret_cast<FsMonitor*>(userdata);
  err_t zisync_ret = monitor->DelInotifyWatches(request_msg_.path());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  monitor->watch_dirs.erase(request_msg_.path() + "/");
  MonitorWatchDelResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return zisync_ret;
}

}  // namespace zs
