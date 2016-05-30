// Copyright 2014, zisync.com
#include <string>
#include <algorithm>
#include <memory>
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/monitor/fs_monitor.h"
#ifndef _AFXDLL
#define _AFXDLL
#endif
#include "zisync/kernel/monitor/directory_changes_windows.h"
//#include "zisync/kernel/platform/platform_windows.h"
#include "zisync/kernel/utils/read_fs_task.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/file_stat.h"

namespace zs {

  int64_t GetFileMoveCookieNumber() {
    static int64_t cookie = 0;
    return ++cookie;
  }

class ZiDirChangeHandler :
      public CDirectoryChangeHandler
{

 public:
  ZiDirChangeHandler(std::string tree_root, IFsReporter* reporter)
    : tree_root_(tree_root), reporter_(reporter) {
      std::unique_ptr<Tree> tree(Tree::GetLocalTreeByRoot(tree_root));
      assert(tree);
      tree_id_ = tree->id();
    }
  virtual ~ZiDirChangeHandler(void) {}

  //
  // Implement CDirectoryChangeHandler
  //
  virtual void On_FileAdded(const CString& tpath) {
    std::string path = CT2A(tpath, CP_UTF8);
	  std::replace(path.begin(), path.end(), '\\', '/');
  
  if (!reporter_->IsIgnored(path)) {
      FsEvent event;
      event.type = FS_EVENT_TYPE_CREATE;
      event.path = path;
	  event.file_move_cookie = 0;
      reporter_->Report(tree_root_, event);

      zs::FsVisitor visitor(tree_root_, tree_id_);
      zs::OsFsTraverser traverser(path, &visitor);
      traverser.traverse();
      for(auto it = visitor.files()->begin(); it != visitor.files()->end(); ++it) {
        FsEvent event;
        event.type = FS_EVENT_TYPE_CREATE;
        event.path = tree_root_ + (*it)->path();
		event.file_move_cookie = 0;
        reporter_->Report(tree_root_, event);
      }
    }
  }
  
  virtual void On_FileRemoved(const CString& tpath) {
    std::string path = CT2A(tpath, CP_UTF8);
	  std::replace(path.begin(), path.end(), '\\', '/');

    if (!reporter_->IsIgnored(path)) {
      FsEvent event;
      event.type = FS_EVENT_TYPE_DELETE;
      event.path = path;
	  event.file_move_cookie = 0;
      reporter_->Report(tree_root_, event);
    }
  }
  virtual void On_FileNameChanged(const CString & tpath_old,
                                  const CString & tpath_new) {
    std::string path_old = CT2A(tpath_old, CP_UTF8);
    std::string path_new = CT2A(tpath_new, CP_UTF8);
    std::replace(path_old.begin(), path_old.end(), '\\', '/');
    std::replace(path_new.begin(), path_new.end(), '\\', '/');
    int64_t cookie = GetFileMoveCookieNumber();

    if (!reporter_->IsIgnored(path_old)) {
      FsEvent event;
      event.type = FS_EVENT_TYPE_MOVE_FROM;
      event.path = path_old;
      event.file_move_cookie = cookie;
      reporter_->Report(tree_root_, event);
    }
    if (!reporter_->IsIgnored(path_new)) {
      FsEvent event;
      event.type = FS_EVENT_TYPE_MOVE_TO;
      event.path = path_new;
      event.file_move_cookie = cookie;
      reporter_->Report(tree_root_, event);
    }
  }
  virtual void On_FileModified(const CString & tpath) {
    std::string path = CT2A(tpath, CP_UTF8);
	std::replace(path.begin(), path.end(), '\\', '/');

    if (!reporter_->IsIgnored(path)) {
      FsEvent event;
      event.type = FS_EVENT_TYPE_MODIFY;
      event.path = path;
	  event.file_move_cookie = 0;
      reporter_->Report(tree_root_, event);
    }
  }

  virtual void On_ReadDirectoryChangesError(DWORD error, const CString& dirname) { }
  virtual void On_WatchStarted(DWORD error, const CString& dirname) { }
  virtual void On_WatchStopped(const CString& dirname) { }

 protected:
  std::string  tree_root_;
  IFsReporter* reporter_;
  int32_t tree_id_;
};

class FsMonitor : public IFsMonitor {
 public:
  FsMonitor() : m_Watcher(false) { }
  virtual ~FsMonitor();
  /*  when FsMonitor found a new dir or file created, modified or deleted, call 
   *  reporter.Report(). */
  virtual int Startup(IFsReporter* reporter);
  virtual int Shutdown();
  virtual int AddWatchDir(const std::string& dir_path);
  virtual int DelWatchDir(const std::string& dir_path);
  virtual int Cancel() { return 0; }

 private:
  DWORD GetNotifyFlags();
  
  CDirectoryChangeWatcher m_Watcher;
  IFsReporter* reporter_;
};

// Singleton
static FsMonitor fs_monitor;

IFsMonitor* GetFsMonitor() {
  return &fs_monitor;
}

FsMonitor::~FsMonitor() {
}
int FsMonitor::Startup(IFsReporter* reporter) {
  reporter_ = reporter;
  return 0;
}
int FsMonitor::Shutdown() {
  m_Watcher.UnwatchAllDirectories();
  reporter_ = NULL;
  return 0;
}
int FsMonitor::AddWatchDir(const std::string& dir_path) {
  DWORD notify_flags = GetNotifyFlags();
  CString tdir_path = CA2T(dir_path.c_str(), CP_UTF8);
  tdir_path.Replace('/', '\\');

  //
  // pHandler is managed by refcount, will be free automatically
  // when UnwatchDirectory
  // 
  auto* pHandler = new ZiDirChangeHandler(dir_path, reporter_);

  ZSLOG_INFO("Watch %s", dir_path.c_str());
  // @TODO replace with .zstm with constant
  m_Watcher.WatchDirectory(
      tdir_path, notify_flags, pHandler, TRUE, NULL, _T(".zisync.meta;.zstm*"));

  return 0;
}

int FsMonitor::DelWatchDir(const std::string& dir_path) {
  CString tdir_path = CA2T(dir_path.c_str(), CP_UTF8);
  tdir_path.Replace('/', '\\');
  m_Watcher.UnwatchDirectory(tdir_path);
  ZSLOG_INFO("Unwatch %s", dir_path.c_str());
  return 0;
}

  
DWORD FsMonitor::GetNotifyFlags() {

  DWORD notify_flags;
  notify_flags =  FILE_NOTIFY_CHANGE_FILE_NAME;
  notify_flags |= FILE_NOTIFY_CHANGE_DIR_NAME;
  notify_flags |= FILE_NOTIFY_CHANGE_LAST_WRITE;
  notify_flags |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
  notify_flags |= FILE_NOTIFY_CHANGE_SIZE;
  notify_flags |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
  notify_flags |= FILE_NOTIFY_CHANGE_LAST_WRITE;
  // notify_flags |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
  notify_flags |= FILE_NOTIFY_CHANGE_CREATION;
  // notify_flags |= FILE_NOTIFY_CHANGE_SECURITY;

  return notify_flags;
}

}  // namespace zs
