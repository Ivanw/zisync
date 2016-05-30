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

#ifndef ZISYNC_KERNEL_PLATFORM_PLATFORM_LINUX_H_
#define ZISYNC_KERNEL_PLATFORM_PLATFORM_LINUX_H_

#include <unistd.h>
#include <sys/cdefs.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#include <atomic>
#include <cinttypes>
#include <cassert>
#include <string>
#include <vector>

#include "zisync/kernel/platform/common.h"
#include "zisync/kernel/platform/os_file.h"

#define MAX_PATH PATH_MAX

namespace zs {

using std::string;

class OsCond;
class OsMutex {
  friend class OsCond;

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
  explicit OsThread(const char* thread_name):pid(-1) {}
  virtual ~OsThread() {}

  int Startup();   // return err_t
  int Shutdown();  // return err_t

  virtual int Run() = 0;   // return err_t

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
      bool auto_delete):pid(-1) {
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
//  void Wait(OsMutex *mutex, int timeout) {
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

  void SetSocket(int socket);
 protected:
  int fd_;
  std::string uri_;

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
  virtual ~OsUdpSocket() {
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

#ifdef __ANDROID__  
class AtomicInt64 {
 public:
	explicit AtomicInt64(int64_t init_value = 0) {
    value_ = init_value;
    mutex.Initialize();
  }

  ~AtomicInt64() {
    mutex.CleanUp();
  }

	int64_t value() {
    return value_;
  }
	void set_value(int64_t new_value) {
    value_ = new_value;
  }

	int64_t FetchAndInc(int64_t value) {
    mutex.AquireMutex(); 
    int64_t old_value = value_;
    value_ = value_ + value;
    mutex.ReleaseMutex();
    return old_value;
    // return __sync_fetch_and_add(&value_, value);
  }
  int64_t FetchAndSub(int64_t value) {
    mutex.AquireMutex(); 
    int64_t old_value = value_;
    value_ = value_ - value;
    mutex.ReleaseMutex();
    return old_value;
    // return __sync_fetch_and_sub(&value_, value);
  }

 private:
  int64_t value_;
  OsMutex mutex;
};

class AtomicInt32 {
 public:
	explicit AtomicInt32(int32_t init_value = 0) {
    value_ = init_value;
    mutex.Initialize();
  }

  ~AtomicInt32() {
    mutex.CleanUp();
  }

	int32_t value() {
    return value_;
  }
	void set_value(int32_t new_value) {
    value_ = new_value;
  }

	int32_t FetchAndInc(int32_t value) {
    mutex.AquireMutex(); 
    int32_t old_value = value_;
    value_ = value_ + value;
    mutex.ReleaseMutex();
    return old_value;
  }
  int32_t FetchAndSub(int32_t value) {
    mutex.AquireMutex(); 
    int32_t old_value = value_;
    value_ = value_ - value;
    mutex.ReleaseMutex();
    return old_value;
    // return __sync_fetch_and_sub(&value_, value);
  }

 private:
  int32_t value_;
  OsMutex mutex;
};
#else 
class AtomicInt64 {
 public:
	explicit AtomicInt64(int64_t init_value = 0) {
  std::atomic_store(&value_, init_value);

  }

	int64_t value() {
   return std::atomic_load(&value_);
  }

  void set_value(int64_t new_value) {
    std::atomic_store(&value_, new_value);
  }

  int64_t FetchAndInc(int64_t value) {
    return std::atomic_fetch_add(&value_, value);
  }
  int64_t FetchAndSub(int64_t value) {
    return std::atomic_fetch_sub(&value_, value);
  }

 private:
  std::atomic<int64_t> value_;
};

class AtomicInt32 {
 public:
	explicit AtomicInt32(int32_t init_value = 0) {
  std::atomic_store(&value_, init_value);

  }

	int32_t value() {
   return std::atomic_load(&value_);
  }

  void set_value(int32_t new_value) {
    std::atomic_store(&value_, new_value);
  }

  int32_t FetchAndInc(int32_t value) {
    return std::atomic_fetch_add(&value_, value);
  }
  int32_t FetchAndSub(int32_t value) {
    return std::atomic_fetch_sub(&value_, value);
  }

 private:
  std::atomic<int32_t> value_;
};

#endif

class IOnTimer {
 public:
  virtual ~IOnTimer() { /* virtual destructor */ }

  virtual void OnTimer() = 0;
};

class OsTimer {
 public:
  /**
   * @param interval_in_ms: time out interval in milisecond. thi first expire
   * will happen in interval_in_ms ms after Initialize
   * @param timer_func: timer_func->OnTimer() is called when timer out.
   *
   * Note: timer_func->OnTimer() is called in timer thread context, not
   * the thread context who initialize the timer.
   */

#if defined(__ANDROID__) 
  OsTimer(int interval_in_ms, IOnTimer* timer_func):
      interval_in_ms_(interval_in_ms), timer_func_(timer_func), 
      timer_id(-1){
        due_time_in_ms_ = interval_in_ms;
      }
  OsTimer(int due_time_in_ms, int interval_in_ms, IOnTimer* timer_func):
      due_time_in_ms_(due_time_in_ms),
      interval_in_ms_(interval_in_ms), timer_func_(timer_func), 
      timer_id(-1){}
#else
  OsTimer(int interval_in_ms, IOnTimer* timer_func):
      interval_in_ms_(interval_in_ms), timer_func_(timer_func), 
      timer_id(NULL){
        due_time_in_ms_ = interval_in_ms;
      }
  OsTimer(int due_time_in_ms, int interval_in_ms, IOnTimer* timer_func):
      due_time_in_ms_(due_time_in_ms),
      interval_in_ms_(interval_in_ms), timer_func_(timer_func), 
      timer_id(NULL){}
#endif
  ~OsTimer() {
#if defined(__ANDROID__) 
    if (timer_id != -1) {
#else
    if (timer_id != NULL) {
#endif
      timer_delete(timer_id);
    }
  }

  /**
   * return 0 if success
   */
  int Initialize();
  int CleanUp();

 private:
  int due_time_in_ms_;
  int interval_in_ms_;
  IOnTimer* timer_func_;
  timer_t timer_id;

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
  
#if defined (__ANDROID__)
  int traverse_helper(const char *dir);
#endif

  const string root;
  IFsVisitor *visitor;
};

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

int ListIpAddresses(
  std::vector<struct sockaddr_in>* ipv4,
  std::vector<struct sockaddr_in6>* ipv6);

inline Platform GetPlatform() {
#if defined(__ANDROID__) 
  return PLATFORM_ANDROID;
#else
  return PLATFORM_LINUX;
#endif
}

inline std::string GetPlatformWithString() {
#if defined (__ANDROID__)
  return "Android";
#else
  return "Linux";
#endif
}

inline int32_t GetPlatformWithNum() {
#if defined (__ANDROID__)
  return 2;
#else
  return 5;
#endif
}

}  // namespace zs

#endif  // ZISYNC_KERNEL_PLATFORM_PLATFORM_LINUX_H_
