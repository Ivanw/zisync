// Copyright 2014, zisync.com
#import <CoreFoundation/CFRunloop.h>
#import <Foundation/Foundation.h>
#import <CoreServices/CoreServices.h>

#include <errno.h>
#include <unistd.h>
#include <zmq.h>
#include <fts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <string>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/monitor/fs_monitor.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/platform/platform.h"

namespace zs {

using std::string;

  static CFStringRef CopyAddTrailingSlash(const string &dir_path) {
    string tmp(dir_path.length() + 2, '\0');
    tmp.assign(dir_path);
    if (tmp.at(tmp.length() - 1) != '/') {
      tmp.push_back('/');
    }
    return CFStringCreateWithBytes(NULL, (const uint8*)tmp.c_str(), tmp.length(), kCFStringEncodingUTF8, false);
  }
  
  static CFTypeRef ArrayContainString(CFArrayRef current_dirs, const string &dir_path) {
    if (!current_dirs) {
      return NULL;
    }
    CFTypeRef ret = NULL;
    current_dirs = (CFArrayRef)CFRetain(current_dirs);
    CFStringRef to_add = (CFStringRef)CFRetain(CopyAddTrailingSlash(dir_path));
    
    CFIndex n_dirs = CFArrayGetCount(current_dirs);
    CFStringRef obj = NULL;
    
    for (CFIndex i = 0; i < n_dirs; i++) {
      obj = (CFStringRef)CFArrayGetValueAtIndex(current_dirs, i);
      CFComparisonResult result = CFStringCompare(obj, to_add, 0);
      if (result == kCFCompareEqualTo) {
        ret = obj;
        break;
      }
    }
    
    CFRelease(to_add);
    CFRelease(current_dirs);
  
    return ret;
  }
  
  static CFComparisonResult pathCompare(
                                        const void *obj1, const void *obj2, void *cts) {
    CFStringRef s1 = (CFStringRef)obj1;
    CFStringRef s2 = (CFStringRef)obj2;
    return CFStringCompare(s1, s2, 0);
  }
  
  static CFArrayRef SortedCopiedArrayOfString(CFArrayRef orig) {
    if (!orig) {
      return NULL;
    }
    CFMutableArrayRef ret = CFArrayCreateMutableCopy(NULL, CFArrayGetCount(orig), orig);
    CFArraySortValues(ret, CFRangeMake(0, CFArrayGetCount(orig)), pathCompare, NULL);
    return ret;
  }

  static CFTypeRef ArrayContainRootForString(CFArrayRef current_dirs, const string &dir_path) {
    CFTypeRef ret = NULL;
    
    current_dirs = (CFArrayRef)CFRetain(current_dirs);
    CFStringRef to_add = CopyAddTrailingSlash(dir_path);
    
    CFIndex n_dirs = CFArrayGetCount(current_dirs);
    CFStringRef obj = NULL;
    
    for (CFIndex i = n_dirs - 1; i >= 0; i--) {
      obj = (CFStringRef)CFArrayGetValueAtIndex(current_dirs, i);
      CFComparisonResult result = CFStringCompare(obj, to_add, 0);
      if (result == kCFCompareGreaterThan) {
        continue;
      }else {
        CFRange range = CFStringFind(to_add, obj, 0);
        if (range.location == 0 && range.length == CFStringGetLength(obj)) {
          ret = obj;
          break;
        }
      }
    }
    
    CFRelease(to_add);
    CFRelease(current_dirs);
  
    return ret;
  }
  
  static void myCallback(
                         ConstFSEventStreamRef streamRef,
                         void *clientCallBackInfo,
                         size_t numEvents,
                         void *eventPaths,
                         const FSEventStreamEventFlags eventFlags[],
                         const FSEventStreamEventId eventIds[]
                         ){
    // protect this queue from multithread AddWatchDir(..)
    const char **evt_paths_ = (const char**)eventPaths;
    
  OsFileStat os_stat;
    for(int i = 0; i < (int)numEvents; i++){
      FsEvent one_evt;
      one_evt.file_move_cookie = 0;
      one_evt.path = evt_paths_[i];
      
      //printf("%s changed, type: ", one_evt.path.c_str());
      if(eventFlags[i] & kFSEventStreamEventFlagItemCreated){
        one_evt.type = FS_EVENT_TYPE_CREATE; 
        //printf("create, ");
      }else if(eventFlags[i] & kFSEventStreamEventFlagItemRemoved){
        one_evt.type = FS_EVENT_TYPE_DELETE;
        //printf("deleted, ");
      }else if(eventFlags[i] & kFSEventStreamEventFlagItemRenamed){
        //on mac, move_from and move_to are the same evnet type, i.e. renamed
        int stat_ret = zs::OsStat(evt_paths_[i], string(), &os_stat);
        if (stat_ret == 0) {
          one_evt.type = FS_EVENT_TYPE_MOVE_TO;
        }else {
          one_evt.type = FS_EVENT_TYPE_MOVE_FROM;
        }
        //printf("renamed, ");
      }else if(eventFlags[i] & kFSEventStreamEventFlagItemModified){
        one_evt.type = FS_EVENT_TYPE_MODIFY;
        //printf("modified, ");
      }else if(eventFlags[i] & (kFSEventStreamEventFlagItemXattrMod | kFSEventStreamEventFlagItemInodeMetaMod)){
        one_evt.type = FS_EVENT_TYPE_ATTRIB;
        //printf("attibute changed, ");
      }else {
        one_evt.type = FS_EVENT_TYPE_MODIFY;
      }
      CFArrayRef current_dirs = (CFArrayRef)CFAutorelease(FSEventStreamCopyPathsBeingWatched(streamRef));
      CFStringRef tree_root = (CFStringRef)ArrayContainRootForString(
                                                                     current_dirs, evt_paths_[i]);
      const char *root = CFStringGetCStringPtr(tree_root, kCFStringEncodingUTF8);
      char *buf = NULL;
      if (!root) {
        
        CFIndex length = CFStringGetLength(tree_root);
        CFIndex maxSize =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
        
        buf = new char[maxSize];
        if (!buf) {
          return;
        }
        
        if (!CFStringGetCString(tree_root, buf, maxSize, kCFStringEncodingUTF8)) {
          delete [] buf;
          return;
        }else {
          root = buf;
        }
      }
      
      if (strcmp(root, evt_paths_[i]) != 0) {
        ((IFsReporter*)clientCallBackInfo)->Report(root, one_evt);
      }
      
      if (buf) {
        delete [] buf;
      }
      
    }
    
  }
  
class FsMonitor : public IFsMonitor, public OsThread {
 public :
  FsMonitor():OsThread("FsMonitor"), fs_event_stream_(NULL), reporter_(NULL),
  runloop_(NULL) {
    runloop_ok_ = [[NSCondition alloc] init];
    runloop_source_ = NULL;
    }

  ~FsMonitor() {
  };
  virtual int Startup(IFsReporter* reporter);
  virtual int Run();
  virtual int Shutdown();
  virtual int AddWatchDir(const string& dir_path);
  virtual int DelWatchDir(const string& dir_path);
  
 private:
  FsMonitor(FsMonitor&);
  void operator=(FsMonitor&);

  FSEventStreamRef fs_event_stream_;
  IFsReporter *reporter_;
  Mutex mutex_;
  CFRunLoopRef runloop_;
  NSCondition *runloop_ok_;
  
  RunLoopSource *runloop_source_;
  
  err_t ChangeMonitoredDirs(CFArrayRef dirs);
  err_t StartupFsEventStream(CFArrayRef dirsToMonitor);
  void StopFsEventStreamIntern(void*);
  err_t StopFsEventStream();
  err_t StartRunloopSource();
  
  void  AddWatchDirIntern(void *);
  void DelWatchDirIntern(void *);
};

static FsMonitor fs_monitor;
  
//  static CFStringRef TrimTrailingSlashes(const string &dir_path) {
//    long dir_len = dir_path.size();
//    assert(dir_len > 0);
//    while (dir_len > 0 && dir_path.at(dir_len - 1) == '/') {
//      dir_len--;
//    }
//    if (dir_len <= 0) {
//      assert(false);
//      return NULL;
//    }
//    CFStringRef ret = CFStringCreateWithBytes(NULL, (const uint8*)dir_path.c_str(), dir_len, kCFStringEncodingUTF8, false);
//    return (CFStringRef)CFAutorelease(ret);
//  }
  
IFsMonitor* GetFsMonitor() {
  return &fs_monitor;
}
  
  void FsMonitor::StopFsEventStreamIntern(void*) {
    if (fs_event_stream_) {
      FSEventStreamStop(fs_event_stream_);
      FSEventStreamInvalidate(fs_event_stream_);
      //FSEventStreamUnscheduleFromRunLoop(fs_event_stream_, runloop_, kCFRunLoopCommonModes);
      FSEventStreamRelease(fs_event_stream_);
      fs_event_stream_ = NULL;
    }
  }
  
  err_t FsMonitor::StopFsEventStream() {
    runloop_source_->SignalSourceWithFunctorAndData(std::bind(&FsMonitor::StopFsEventStreamIntern
                                                              , this
                                                              , std::placeholders::_1),
                                                    NULL);
    return ZISYNC_SUCCESS;
  
  }

err_t FsMonitor::StartupFsEventStream(CFArrayRef dirsToMonitor) {
  assert(fs_event_stream_ == NULL);
  
  if (dirsToMonitor) {
    dirsToMonitor = (CFArrayRef)CFRetain(dirsToMonitor);
  }
  
  if (dirsToMonitor) {
    FSEventStreamContext context = {0, reporter_, NULL, NULL, NULL};
    CFTimeInterval latency = 1.0f;
    FSEventStreamCreateFlags createFlags = kFSEventStreamCreateFlagFileEvents
    | kFSEventStreamCreateFlagNoDefer;
    fs_event_stream_ = FSEventStreamCreate(
                                           NULL, &myCallback, &context, dirsToMonitor, FSEventsGetCurrentEventId(), latency, createFlags);
    FSEventStreamScheduleWithRunLoop(
                                     fs_event_stream_, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    FSEventStreamStart(fs_event_stream_);
    
  }
  
  if (dirsToMonitor) {
    CFRelease(dirsToMonitor);
  }
  return ZISYNC_SUCCESS;
}
  
  err_t FsMonitor::StartRunloopSource() {
    
    if (!runloop_source_) {
      runloop_source_ = new RunLoopSource;
      runloop_source_->Initialize();
      runloop_source_->AddToRunloop();
    }
    
    return ZISYNC_SUCCESS;
  }

int FsMonitor::Startup(IFsReporter *reporter) {
  reporter_ = reporter;
  assert(!fs_event_stream_);
  int ret =  OsThread::Startup();
  [runloop_ok_ lock];
  while (!runloop_) {
    [runloop_ok_ wait];
  }
  [runloop_ok_ unlock];
  return ret;
}

int FsMonitor::Run() {
  
  StartRunloopSource();
  
  err_t zisync_ret = ZISYNC_SUCCESS;
  zisync_ret = StartupFsEventStream(NULL);
  assert(zisync_ret == ZISYNC_SUCCESS);
  
  if (!runloop_) {
    MutexAuto mutex_auto(&mutex_);
    runloop_ = (CFRunLoopRef)CFRetain(CFRunLoopGetCurrent());
    [runloop_ok_ lock];
    [runloop_ok_ signal];
    [runloop_ok_ unlock];
  }

  CFRunLoopRun();
  
  assert(runloop_);
  CFRunLoopStop(runloop_);
  CFRelease(runloop_);
  runloop_ = NULL;
  
  if (runloop_source_) {
    MutexAuto mutex_auto(&mutex_);
    delete runloop_source_;
    runloop_source_ = NULL;
  }
  
  return 0;
}

int FsMonitor::Shutdown() {
  StopFsEventStream();
  runloop_source_->Invalidate();
  
  int ret = OsThread::Shutdown();
  if (ret != 0) {
    return ret;
  }
  return 0;
}
  
  int FsMonitor::AddWatchDir(const string &dir_path) {
    std::function<RunLoopCallbackFunctor_t> functor =
    std::bind(&FsMonitor::AddWatchDirIntern, this, std::placeholders::_1);
    assert(runloop_);
    assert(runloop_source_);
    runloop_source_->SignalSourceWithFunctorAndData(functor, new string(dir_path));
    return 0;
  }

void FsMonitor::AddWatchDirIntern(void *info) {
  string *ppath = (string*)info;
  const string &dir_path = *ppath;
  
  CFArrayRef current_dirs = NULL;
  if (fs_event_stream_) {
    current_dirs = FSEventStreamCopyPathsBeingWatched(fs_event_stream_);
  }
  
  CFStringRef to_add = CopyAddTrailingSlash(dir_path);
  
  if (!ArrayContainString(current_dirs, dir_path)) {
    CFMutableArrayRef updatedDirsToMonitor
      = CFArrayCreateMutable(NULL
                             , (current_dirs ? CFArrayGetCount(current_dirs) : 0 )+ 1
                             , &kCFTypeArrayCallBacks);
    if (current_dirs) {
      CFArrayAppendArray(updatedDirsToMonitor, current_dirs, CFRangeMake(0, CFArrayGetCount(current_dirs)));
    }
    
    CFArrayAppendValue(updatedDirsToMonitor, to_add);
    ChangeMonitoredDirs(updatedDirsToMonitor);
  }
  
  if (current_dirs) {
    CFRelease(current_dirs);
  }
  CFRelease(to_add);
  delete ppath;
}
  
  err_t FsMonitor::ChangeMonitoredDirs(CFArrayRef dirs) {
    dirs = (CFArrayRef)CFRetain(dirs);
    
    if (fs_event_stream_) {
      StopFsEventStreamIntern(NULL);
    }
    
    err_t ret = StartupFsEventStream(dirs);
    assert(ret == ZISYNC_SUCCESS);
    
    CFRelease(dirs);
    return ret;
  }

void FsMonitor::DelWatchDirIntern(void *info) {
  string *ppath = (string*)info;
  const string &dir_path = *ppath;
  
  CFArrayRef current_dirs = NULL;
  if (fs_event_stream_) {
   current_dirs = FSEventStreamCopyPathsBeingWatched(fs_event_stream_);
  }
  CFStringRef to_del = CFStringCreateWithBytes(NULL, (const uint8*)dir_path.c_str(), dir_path.length(), kCFStringEncodingUTF8, false);
  
  CFTypeRef obj = ArrayContainString(current_dirs, dir_path);
  
  if (obj) {
    CFMutableArrayRef updatedDirsToMonitor = NULL;
    CFIndex n_dirs = CFArrayGetCount(current_dirs);
    if (n_dirs > 1) {
      updatedDirsToMonitor = CFArrayCreateMutable(NULL
                                                  , n_dirs - 1
                                                  , &kCFTypeArrayCallBacks);
      for (CFIndex i = 0; i < n_dirs; i++) {
        CFStringRef item =  (CFStringRef)CFArrayGetValueAtIndex(current_dirs, i);
        if (CFStringCompare(item, to_del, 0) != kCFCompareEqualTo) {
          CFArrayAppendValue(updatedDirsToMonitor, item);
        }
      }
    }
    ChangeMonitoredDirs(updatedDirsToMonitor);
  }
  
  CFRelease(current_dirs);
  CFRelease(to_del);
  delete ppath;
}

  int FsMonitor::DelWatchDir(const string &dir_path) {
    std::function<RunLoopCallbackFunctor_t> functor =
    std::bind(&FsMonitor::DelWatchDirIntern, this, std::placeholders::_1);
    assert(runloop_);
    assert(runloop_source_);
    runloop_source_->SignalSourceWithFunctorAndData(functor, new string(dir_path));
    return 0;
  }


}  // namespace zs
