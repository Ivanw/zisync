// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_PLATFORM_PLATFORM_H_
#define ZISYNC_KERNEL_PLATFORM_PLATFORM_H_

#include "zisync/kernel/platform/common.h"

#if defined(linux) || defined(__linux) || defined(__linux__) \
    || defined(__GNU__) || defined(__GLIBC__)
// linux, also other platforms (Hurd etc) that use GLIBC, should these
// really have their own config headers though?
#include "zisync/kernel/platform/platform_linux.h" // NOLINT
#include <string>

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
    || defined(__DragonFly__)
// BSD:
#error "platform unsupported by now"

#elif defined(sun) || defined(__sun)
// solaris:
#error "platform unsupported by now"

#elif defined(__sgi)
// SGI Irix:
#error "platform unsupported by now"

#elif defined(__hpux)
// hp unix:
#error "platform unsupported by now"

#elif defined(__CYGWIN__)
// cygwin is not win32:
#error "platform unsupported by now"

#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
// win32:
#include "zisync/kernel/platform/platform_windows.h"  // NOLINT

#elif defined(__BEOS__)
// BeOS
#error "platform unsupported by now"

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
// MacOS
#include "platform_mac.h"  // NOLINT

#elif defined(__IBMCPP__) || defined(_AIX)
// IBM
#error "platform unsupported by now"

#elif defined(__amigaos__)
// AmigaOS
#error "platform unsupported by now"

#elif defined(__QNXNTO__)
// QNX:
#error "platform unsupported by now"

#elif defined(__VXWORKS__)
// vxWorks:
#error "platform unsupported by now"

#elif defined(__SYMBIAN32__)
// Symbian:
#error "platform unsupported by now"

#elif defined(__VMS)
// VMS:
#error "platform unsupported by now"

#else
// Unknown
#error "Unkonwn platform "

#endif

namespace zs {

int OsSocketStartup();
int OsSocketShutdown();

class Cond;
class MutexAuto;
class Mutex {
  friend class Cond;
  friend class MutexAuto;

 public:
  Mutex() {
    mutex_.Initialize();
  }

  Mutex(int mutex_type) {
    mutex_.Initialize(mutex_type);
  }

  ~Mutex() {
    mutex_.CleanUp();
  }

  int Aquire() {
    return mutex_.AquireMutex();
  }

  bool TryAquire() {
    return mutex_.TryAquireMutex();
  }

  int Release() {
    return mutex_.ReleaseMutex();
  }

 private:
  Mutex(Mutex&);
  void operator=(Mutex&);

  OsMutex mutex_;
};

class MutexAuto {
 public:
  explicit MutexAuto(OsMutex* mutex)
      : mutex_(mutex) {
        mutex_->AquireMutex();
      }
  explicit MutexAuto(Mutex* mutex)
      : mutex_(&mutex->mutex_) {
        mutex_->AquireMutex();
      }

  ~MutexAuto() {
    mutex_->ReleaseMutex();
  }

 protected:
  OsMutex* mutex_;
};

class RwLock {
  friend class RwLockRdAuto;
  friend class RwLockWrAuto;

 public:
  RwLock() {
    rwlock_.Initialize();
  }

  ~RwLock() {
    rwlock_.CleanUp();
  }

  void AquireRdLock() {
    rwlock_.AquireRdLock();
  }
  
  void AquireWrLock() {
    rwlock_.AquireWrLock();
  }

  void ReleaseRdLock() {
    rwlock_.ReleaseRdLock();
  }

  void ReleaseWrLock() {
    rwlock_.ReleaseWrLock();
  }
 private:
  RwLock(RwLock&);
  void operator=(RwLock&);

  OsRwLock rwlock_;
};

class RwLockRdAuto {
 public:
  explicit RwLockRdAuto(OsRwLock* rwlock)
      : rwlock_(rwlock) {
        rwlock_->AquireRdLock();
      }
  explicit RwLockRdAuto(RwLock* rwlock)
      : rwlock_(&rwlock->rwlock_) {
        rwlock_->AquireRdLock();
      }

  ~RwLockRdAuto() {
    rwlock_->ReleaseRdLock();
  }

 private:
  RwLockRdAuto(RwLockRdAuto&);
  void operator=(RwLockRdAuto&);

  OsRwLock* rwlock_;
};

class RwLockWrAuto {
 public:
  explicit RwLockWrAuto(OsRwLock* rwlock)
      : rwlock_(rwlock) {
        rwlock_->AquireWrLock();
      }
  explicit RwLockWrAuto(RwLock* rwlock)
      : rwlock_(&rwlock->rwlock_) {
        rwlock_->AquireWrLock();
      }

  ~RwLockWrAuto() {
    rwlock_->ReleaseWrLock();
  }

 private:
  RwLockWrAuto(RwLockWrAuto&);
  void operator=(RwLockWrAuto&);

  OsRwLock* rwlock_;
};

//class Cond {
// public :
//  Cond() {
//    cond_.Initialize();
//  }
//  ~Cond() {
//    cond_.CleanUp();
//  }
//  void Signal() {
//    return cond_.Signal();
//  }
//  void Wait(Mutex *mutex) {
//    cond_.Wait(&mutex->mutex_);
//  }
//  void Wait(Mutex *mutex, int ms_timeout) {
//    cond_.Wait(&mutex->mutex_, ms_timeout);
//  }
// private:
//  Cond(Cond&);
//  void operator=(Cond&);
//  OsCond cond_;
//};

class Timer {
 public :
  Timer(int interval_in_ms, IOnTimer* timer_func):
      timer_(interval_in_ms, timer_func) {
        int ret = timer_.Initialize();
        assert(ret == 0);
      }
  ~Timer() {
    int ret = timer_.CleanUp();
    assert(ret == 0);
  }

 private:
  Timer(Timer&);
  void operator=(Timer&);

  OsTimer timer_;
};

class UrlParser {
 public:
  UrlParser(const std::string& url) : url_(url) {
    std::string schema;

    std::string::size_type found1 = url_.find_first_of("://");
    assert(found1 != std::string::npos);
    if (found1 != std::string::npos) {
      schema = url_.substr(0, found1);
      found1 += 3;
    } else {
      found1 = 0;
    }

    std::string::size_type found2 = url_.find_last_of(':');
    assert (found2 != std::string::npos);
    host_ = url_.substr(found1, found2 - found1);
    service_ = url_.substr(found2 + 1);
#ifdef _WIN32
    sscanf_s(service_.data(), "%d", &port_);
#else
    sscanf(service_.data(), "%d", &port_);
#endif
  }
  virtual ~UrlParser() {
      
  }

  const std::string& host() {
    return host_;
  }

  const std::string& service() {
    return service_;
  }

  int32_t port() {
    return port_;
  }

 private:
  std::string url_;
  std::string host_;
  std::string service_;
  int32_t port_;
};

FILE* OsFopen(const char* path, const char* mode);

int SockAddrToString(struct sockaddr_in* inaddr, std::string* host);
	
}  // namespace zs


#endif  // ZISYNC_KERNEL_PLATFORM_PLATFORM_H_
