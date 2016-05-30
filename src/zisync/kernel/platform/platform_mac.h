/**
 * @file platform_linux.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Linux platfom implment.
 *
 * Copyright (C) 2014 Likun Liu <liulikun@gmail.com>
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

#ifndef ZISYNC_KERNEL_PLATFORM_PLATFORM_MAC_H_
#define ZISYNC_KERNEL_PLATFORM_PLATFORM_MAC_H_

#include <sys/cdefs.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <cassert>
#include <string>
#include <atomic>
#include <utility>
#include <netdb.h>
#include <netinet/in.h>
#include "zisync/kernel/platform/common.h"

#include <sys/time.h>
#include <iostream>
#include <vector>
#include <functional>

#include <CoreFoundation/CFRunloop.h>
#include <zisync/kernel/zslog.h>

#include <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE

#include <AssetsLibrary/AssetsLibrary.h>
#import "ZiAsset.h"
#import "ZiGroup.h"
#import "ZiAssetsLibrary.h"

#define AssetLibrarySchemeGroup "assets-library://group"
#define AssetLibrarySchemeAsset "assets-library://asset"
#endif

#define MAX_PATH PATH_MAX
#define MSG_NOSIGNAL 0

#define IsEqualFloats(fa, fb) ( fabs((fa) - (fb)) < 10 * FLT_EPSILON *fabs((fa) + (fb)) )
#define IsEqualBOOL(a, b) ((a) && (b) || !(a) && !(b))

namespace zs {

using std::string;
using std::vector;
class Mutex;

int64_t OsTime();  // in second

class OsCond;
class OsMutex {
  friend OsCond;

 public:
  OsMutex() {}
  ~OsMutex() {}
  int Initialize(int type = PTHREAD_MUTEX_DEFAULT) {
    int ret;

    pthread_mutexattr_t attr;
    pthread_mutexattr_t *pattr = NULL;
    if (type != PTHREAD_MUTEX_DEFAULT) {
      ret = pthread_mutexattr_init(&attr);
      assert(ret == 0);
      ret = pthread_mutexattr_settype(&attr, type);
      assert(ret == 0);
      pattr = &attr;
    }

    ret = pthread_mutex_init(&mutex, pattr);
    assert(ret == 0);

    if (pattr) {
      ret = pthread_mutexattr_destroy(pattr);
      assert(ret == 0);
    }
    return ret;
  }

  int CleanUp() {
    int ret = pthread_mutex_destroy(&mutex);
    assert(ret == 0);
    return ret;
  }
  int AquireMutex() {
    int ret = pthread_mutex_lock(&mutex);
    assert(ret == 0 || ret == EDEADLK);
    return ret;
  }

  bool TryAquireMutex() {
    return pthread_mutex_trylock(&mutex) == 0;
  }
  
  int ReleaseMutex() {
    int ret = pthread_mutex_unlock(&mutex);
    assert(ret == 0 || ret == EPERM);
    return ret;
  }

 private:
  OsMutex(OsMutex&);
  void operator=(OsMutex&);

  pthread_mutex_t mutex;
};

class OsEvent {
 public:
  int Initialize(bool signaled);
  int CleanUp();

  int Reset();
  int Signal();
  int Wait();

 private:
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
  bool signaled_;
};


class OsRwLock {
 public:
  OsRwLock() {}
  ~OsRwLock() {}
  int Initialize() {
    int ret = pthread_rwlock_init(&rwlock_, NULL);
    assert(ret == 0);
    return ret;
  }
  int CleanUp() {
    int ret = pthread_rwlock_destroy(&rwlock_);
    assert(ret == 0);
    return ret;
  }
  int AquireRdLock() {
    int ret = pthread_rwlock_rdlock(&rwlock_);
    assert(ret == 0);
    return ret;
  }
  int AquireWrLock() {
    int ret = pthread_rwlock_wrlock(&rwlock_);
    assert(ret == 0);
    return ret;
  }
  int ReleaseRdLock() {
    int ret = pthread_rwlock_unlock(&rwlock_);
    assert(ret == 0);
    return ret;
  }
  int ReleaseWrLock() {
    int ret = pthread_rwlock_unlock(&rwlock_);
    assert(ret == 0);
    return ret;
  }

  private:
  pthread_rwlock_t rwlock_;
};

class OsThread {
 public:
  // thread name, most of time used by debugger
  explicit OsThread(const char* thread_name):pid(NULL) {}
  virtual ~OsThread() {}

  int Startup();   // return error_t
  int Shutdown();  // return error_t

  virtual int Run() = 0;   // return error_t

 private:
  OsThread(OsThread&);
  void operator=(OsThread&);

  pthread_t pid;
};

class OsWorkerThread {
 public:
  // thread name, most of time used by debugger
  explicit OsWorkerThread(
      const char* thread_name,
      IRunnable* runnable,
      bool auto_delete):pid(NULL) {
    started_ = false;
    runnable_ = runnable;
    auto_delete_ = auto_delete;
  }
  ~OsWorkerThread() {}

  int Startup();   // return err_t
  int Shutdown();  // return err_t

  static void* ThreadFunc(void* args);

 private:
  OsWorkerThread(OsWorkerThread&);
  void operator=(OsWorkerThread&);

  pthread_t pid;
  IRunnable* runnable_;
  bool auto_delete_;
  bool started_;
};


//class OsCond {
// public:
//  OsCond() {}
//  ~OsCond() {}
//
//  void Initialize() {
//    int ret = pthread_cond_init(&cond, NULL);
//    assert(ret == 0);
//  }
//  void CleanUp() {
//    int ret = pthread_cond_destroy(&cond);
//    assert(ret == 0);
//  }
//  void Signal() {
//    int ret = pthread_cond_signal(&cond);
//    assert(ret == 0);
//  }
//  void Wait(OsMutex* mutex) {
//    int ret = pthread_cond_wait(&cond, &mutex->mutex);
//    assert(ret == 0);
//  }
//  // in ms
//  void Wait(OsMutex* mutex, int timeout) {
//    struct timespec timeout_;
//    timeout_.tv_sec = timeout / 1000;
//    timeout_.tv_nsec = (timeout - timeout_.tv_sec) * 1000;
//    int ret = pthread_cond_timedwait(&cond, &mutex->mutex, &timeout_);
//    assert(ret == 0);
//  }
//
// private:
//  OsCond(OsCond&);
//  void operator=(OsCond&);
//
//  pthread_cond_t cond;
//};

class OsSocketAddress {
public:
  OsSocketAddress(const std::string& uri);

  struct sockaddr* NextSockAddr();

private:
  const std::string uri_;

  struct addrinfo* result_;
  struct addrinfo* ptr_;
};

class OsTcpSocket {
 public:
  explicit OsTcpSocket(const char* uri);
  explicit OsTcpSocket(const std::string& uri);
  virtual ~OsTcpSocket();

  virtual int Listen(int backlog);
  virtual int Accept(OsTcpSocket **accepted_socket);
  /* @return : 0 if success, EADDRINUSE if addr in use, others -1*/
  virtual int Bind();
  virtual int Connect();
  virtual int GetSockOpt(
      int level, int optname, void* optval, socklen_t* optlen);
  virtual int SetSockOpt(
      int level, int optname, void* optval, socklen_t optlen);

  /**
   * @param how: how to shutdown, "r", "w", and "rw"
   */
  virtual int Shutdown(const char* how);

  virtual int Send(const char *buffer, int length, int flags);
  virtual int Send(const string &buffer, int flags);
  virtual int Recv(char *buffer, int length, int flags);
  virtual int Recv(string *buffer, int flags);

  void Swap(OsTcpSocket* that) {
    std::swap(fd_, that->fd_);
    uri_.swap(that->uri_);
  }

  int fd() {
    return fd_;
  }

  const std::string& uri() {
    return uri_;
  }

 protected:
  int fd_;
  std::string uri_;
  void SetSocket(int socket);

 private:
  OsTcpSocket(const OsTcpSocket&);
  void operator=(const OsTcpSocket&);
};

class OsTcpSocketFactory {
 public:
  // @param uri should be: "tcp://host:port" or "file:///path/to/file"
  static OsTcpSocket* Create(const std::string& uri, void* arg = NULL);
};

class OsUdpSocket {
 public:
  explicit OsUdpSocket(const char* uri) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd_ != -1);
    uri_ = uri;
  }
  explicit OsUdpSocket(const std::string& uri) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd_ != -1);
    uri_ = uri;
  }
  ~OsUdpSocket() {
    close(fd_);
  }

  void Swap(OsUdpSocket* that) {
    std::swap(fd_, that->fd_);
    uri_.swap(that->uri_);
  }

  /* @return : 0 if success, EADDRINUSE if addr in use, others -1*/
  int Bind();
  /**
   * @param how: how to shutdown, "r", "w", and "rw"
   */
  int Shutdown(const char* how);

  int SendTo(const char *buffer, int length, int flags,
             const char *dst_addr);
  int SendTo(const string &buffer, int flags,
             const string &dst_addr);
  int RecvFrom(string *buffer, int flags, string *src_addr);
  int EnableBroadcast();
int EnableMulticast(const std::string& multicast_addr);
  int fd() { return fd_; }

 protected:
  int fd_;
  std::string uri_;

 private:
  OsUdpSocket(OsUdpSocket&);
  void operator=(OsUdpSocket&);
};

class AtomicInt64 {
 public:
	explicit AtomicInt64(int64_t init_value = 0);

	int64_t value();
	void set_value(int64_t new_value);

	int64_t FetchAndInc(int64_t value);
  int64_t FetchAndSub(int64_t value);

 private:
  std::atomic<int64_t> value_;
};

class AtomicInt32 {
 public:
	explicit AtomicInt32(int32_t init_value = 0);

	int32_t value();
	void set_value(int32_t new_value);

	int32_t FetchAndInc(int32_t value);
  int32_t FetchAndSub(int32_t value);

 private:
  std::atomic<int32_t> value_;
};


class IOnTimer {
 public:
  virtual ~IOnTimer() { /* virtual destructor */ }

  virtual void OnTimer() = 0;
};
extern int ZsTimerSchedulerThreadInstantCnt;
class ZsTimerSchedulerThread : public OsThread, public IOnTimer {
 public:
  // thread name, most of time used by debugger
  explicit ZsTimerSchedulerThread(const char* thread_name): OsThread(thread_name), numberOfTimers(0) {
      assert(0 == mutx.Initialize());
         mutx.AquireMutex();
         ZsTimerSchedulerThreadInstantCnt++;
         mutx.ReleaseMutex();
         printf("ZsTimerSchedulerThread instance serial: %d\n", ZsTimerSchedulerThreadInstantCnt);
      initIdleTimer();
      Startup();
  }
  virtual int Run();
  bool ScheduleTimer(CFRunLoopTimerRef timer);
  bool ScheduleTimerUnsafe(CFRunLoopTimerRef timer);
  void RemoveTimer(CFRunLoopTimerRef);
  
  void OnTimer();

  vector<CFRunLoopTimerRef> dump();
  void import(const vector<CFRunLoopTimerRef> &);
 private:
  void initIdleTimer();
  bool runloopValid();
  bool runloopWaiting();
  bool isTimerScheduled(CFRunLoopTimerRef );
  void RemoveIdleTimer();
  ZsTimerSchedulerThread(ZsTimerSchedulerThread&);
  void operator=(ZsTimerSchedulerThread&);
  CFRunLoopTimerRef idleTimer_;
  CFRunLoopRef runloop;
  int numberOfTimers;
  OsMutex mutx;
  vector<CFRunLoopTimerRef> todo;
};

class OsTimer {
 public:
  /**
   * @param interval_in_ms: time out interval in milisecond.
   * @param timer_func: timer_func->OnTimer() is called when timer out.
   *
   * Note: timer_func->OnTimer() is called in timer thread context, not
   * the thread context who initialize the timer.
   */
  OsTimer(int , IOnTimer*);
  OsTimer(int, int , IOnTimer*);
  ~OsTimer() {
  }

  /**
   * return 0 if success
   */
  int Initialize();
  int CleanUp();

  static ZsTimerSchedulerThread *timer_scheduler_;
 private:
  string threadName();
  int interval_in_ms_;
  int due_time_in_ms_;
  IOnTimer* timer_func_;
  CFRunLoopTimerRef timer_;
  static Mutex mutex;

  OsTimer(const OsTimer&);
  void operator=(const OsTimer&);
};

class OsTimeOut : public OsTimer {
 public:
  OsTimeOut(int timeout_in_ms, IOnTimer* timeout_func):
    OsTimer(timeout_in_ms, 0, timeout_func){}
};

class OsFsTraverser {
 public:
  OsFsTraverser(const string& root, IFsVisitor *visitor):root(root), 
    visitor(visitor) {}
  /* traverse the directory, for each file and dir, call
   * visitor->Visit(file_stat). In this function, you can also use
   * visitor->IsIgnore te determin the file or (dir and its sub files should be
   * ignored*/
  int traverse();

 private:
  OsFsTraverser(OsFsTraverser&);
  void operator=(OsFsTraverser&);

  const string root;
  IFsVisitor *visitor;
};

//class OsFsTraverser {
// public:
//    OsFsTraverser(const string& root, IFsVisitor *visitor);
//  /* traverse the directory, for each file and dir, call
//   * visitor->Visit(file_stat). In this function, you can also use
//   * visitor->IsIgnore te determin the file or (dir and its sub files should be
//   * ignored*/
//  int traverse();
//
// private:
//  OsFsTraverser(OsFsTraverser&);
//  void operator=(OsFsTraverser&);
//
//  const string root;
//  IFsVisitor *visitor;
//  
//    int traverseFileSystem();
//#if TARGET_OS_IPONE
//    bool m_is_asset;
//    void enumerateGroup(ZiGroup *group);
//    int traverseAsset();
//    ZiGroup *m_group;
//    NSCondition *m_condition_assets;
//    NSCondition *m_condition_group;
//    NSMutableDictionary *m_asset_groups;
//#endif
//};
//
inline Platform GetPlatform() {
#if TARGET_OS_IPHONE
  return PLATFORM_IOS;
#else
  return PLATFORM_MAC;
#endif
}

inline std::string GetPlatformWithString() {
  return "iOS";
} 

inline int32_t GetPlatformWithNum() {
  return 3;
}

int ListIpAddresses(
  std::vector<struct sockaddr_in>* ipv4,
  std::vector<struct sockaddr_in6>* ipv6);

#if TARGET_OS_IPHONE
std::string iOSVersion() {
    return [[UIDevice currentDevice] systemVersion].UTF8String;
}
#endif
  
#if TARGET_OS_IPHONE
static void handleAssetsLibraryChange(CFNotificationCenterRef center,
                                      void *observer,
                                      CFStringRef name,
                                      const void *object,
                                      CFDictionaryRef userInfo);
#endif

//class AssetUrlParser {
//public:
//    static const char *SeparatorString;
//    static const char *AssetsRoot;
//    AssetUrlParser(){}
//    AssetUrlParser(const string &url);
//    void SetUrl(const string &url);
//    static string composeUrlWith(const string &assetUrl,
//                          const string &groupUrl);
//    string AssetUrl();
//    string GroupUrl();
//    string GroupName();
//    string AssetName();
//    string AssetType();
//    bool IsAsset(){return m_is_asset;};
//private:
//    bool m_is_asset;
//    string m_group_url;
//    string m_asset_url;
//    string m_group_name;
//    string m_asset_name;
//    string m_asset_type;
//    
//    string GetSegment(int index, const string &str);
//};
//    
class OsFile {
public:
	OsFile() : fp_(NULL) {}
	~OsFile() {
		if (fp_ != NULL) {
			fclose(fp_);
		}
	}

	int Open(const std::string& path, const std::string &alias,
		const char* mode);
	/* read file data, size = buffet->size(), offset = SEEK_CUR */
	/* @return : the byte of read data, resize the buffer with read size */
	size_t Read(std::string* buffer);
	size_t Read(char *buf, size_t length);
	/* write file data, size = buffet.size(), offset = SEEK_CUR */
	size_t Write(const std::string &buffer);
	size_t Write(const char *data, size_t length);

	// Do not use this function to read large file.
	size_t ReadWholeFile(std::string* buffer);
	/* if reach the end of file, return 1, else return 0*/
	int eof() {
		return feof(fp_);
	}
	void Close() {
		if (fp_ != NULL) {
			fclose(fp_);
			fp_ = NULL;
		}
	}

private:
	FILE* fp_;
};

//class OsFile {
//public:
//    OsFile();
//    ~OsFile(){reset();};
//#if TARGET_OS_IPHONE
//    friend void handleAssetsLibraryChange(CFNotificationCenterRef center,
//                                          void *observer,
//                                          CFStringRef name,
//                                          const void *object,
//                                          CFDictionaryRef userInfo);
//#endif
//    static const char *ModeRead;
//    static const char *ModeWrite;
//    
//    int Open(const std::string& path, const std::string& alias, 
//             const char* mode);
//    /* read file data, size = buffet->size(), offset = SEEK_CUR */
//    size_t Read(std::string* buffer);
//    size_t Read(char *buf, size_t length);
//    /* write file data, size = buffet.size(), offset = SEEK_CUR */
//    size_t Write(const std::string &buffer);
//    size_t Write(const char *data, size_t length);
//    
//    // Do not use this function to read large file.
//    size_t ReadWholeFile(std::string* buffer);
//    int Close();//check asset type, and import into photolibrary
//    int eof();
//    static bool isMetaFile(const string &meta_path);
//    static std::string pathForMetaFile(const string &meta_path);
//    
//private:
//    void reset();
//    AssetUrlParser m_url_parser;
//    int appendToFile(const char *buffer, size_t length);
//#if TARGET_OS_IPHONE
//    ZiAsset *m_asset;
//    ZiGroup *m_group;
//    bool m_failed;
//    NSCondition *m_condition;
//    std::string m_mode;
//  
//    void CreateGroup();
//    void LoadGroup();
//    
//    long long m_rd_offset;
//    std::string m_video_cache_path;
//    std::string m_write_buf;
//    
//    std::string assetType();
//    int writeIntoAssetLibraryComplete(dispatch_block_t complete);
//    
//    void logError(NSString *info, NSString *action, NSError *error);
//  
//#endif
//    std::ifstream *ifile;
//    std::ofstream *ofile;
//};
//
  
  typedef void RunLoopCallbackFunctor_t(void*);
  
  typedef void (*RunLoopDidScheduleCallbackPtr_t) (void *info
  , CFRunLoopRef runloop, CFStringRef mode);
  
  typedef void (*RunLoopDidRemoveCallbackPtr_t) (void *info
  , CFRunLoopRef runloop, CFStringRef mode);
  
  
  typedef std::function<RunLoopCallbackFunctor_t> RunLoopSourceFunctor;
  typedef std::tuple<RunLoopSourceFunctor, void*, OsEvent*> RunLoopSourceRequest;
  
  class RunLoopSourceContext {
  public:
    CFRunLoopRef runloop_;
    CFRunLoopSourceRef runloop_source_;
  };
  
  static void RunLoopSourceFiredCallback(void*);
  static void RunLoopDidScheduleCallback(void*, CFRunLoopRef, CFStringRef);
  static void RunLoopDidRemoveCallback(void*, CFRunLoopRef, CFStringRef);
  
  class RunLoopSource {
  public:
    
    friend void RunLoopSourceFiredCallback(void*);
    friend void RunLoopDidScheduleCallback(void*, CFRunLoopRef, CFStringRef);
    friend void RunLoopDidRemoveCallback(void*, CFRunLoopRef, CFStringRef);
    
    RunLoopSource() {
      is_valid_ = false;
      mutex_sync_.Initialize();
    }
    ~RunLoopSource() {
      is_valid_ = false;
      mutex_sync_.CleanUp();
      CFRelease(runloop_source_);
      runloop_source_ = NULL;
    }
    
    //create runloop_source, callback used to pass back runloop
    void Initialize();
    //must call in destination thread
    void AddToRunloop();
    
    void Invalidate();
    //todo: may miss request, should queue up requests
    void SignalSourceWithFunctorAndData(std::function<RunLoopCallbackFunctor_t> functor, void *data);
    
    bool IsValid(){return is_valid_;}
    CFRunLoopRef RunLoop() {return runloop_;};
    
  private:
    OsMutex mutex_sync_;
    std::vector<RunLoopSourceRequest> requests_;
    
    bool is_valid_;
    
    CFRunLoopSourceRef runloop_source_;
    CFRunLoopRef runloop_;
    
    void InvalidateIntern(void*);
    
    RunLoopSourceRequest MakeRequest(RunLoopSourceFunctor functor, void *data, OsEvent *wait) {
      return std::make_tuple(functor, data, wait);
    }
    
    void HandleRequests();
  };
  
}  // namespace zs
#endif  // ZISYNC_KERNEL_PLATFORM_PLATFORM_MAC_H_
