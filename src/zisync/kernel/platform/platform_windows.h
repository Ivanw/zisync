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

#ifndef ZISYNC_KERNEL_PLATFORM_PLATFORM_WINDOWS_H_
#define ZISYNC_KERNEL_PLATFORM_PLATFORM_WINDOWS_H_


#include <Winsock2.h>
#include <Windows.h>
#include <ws2ipdef.h>
#include <WS2tcpip.h>

#include <stdint.h>
#include <cassert>
#include <string>
#include <vector>

#ifndef va_copy
# ifdef __va_copy
# define va_copy(DEST, SRC) __va_copy((DEST), (SRC))
# else
//  safe on vc
# define va_copy(DEST, SRC) memcpy((&DEST), (&SRC), sizeof(va_list))
# endif
#endif

#define PRId64 "I64d"
#define PRId16 "hd"
#define PRId32 "d"
#define PRIo64 "I64o"
#define strdup _strdup

#define PATH_MAX MAX_PATH
#define MSG_NOSIGNAL 0

static inline unsigned int sleep(unsigned int seconds) {
  Sleep(seconds * 1000);
  return 0;
}

namespace zs {

int OsPathAppend(char* path1, int path1_capacity, const char* path2);
int OsPathAppend(std::string* path1, const std::string& path2);

int64_t OsTimeInS();  
int64_t OsTimeInMs();  
int OsLstat(const char *path, struct stat *buf);

class OsMutex {
 public:
  int Initialize();
  int Initialize(int);
  int CleanUp();

  int AquireMutex();
  bool TryAquireMutex();
  int ReleaseMutex();

 protected:
  CRITICAL_SECTION critical_section_;
};

class OsEvent {
 public:
  int Initialize(bool signaled) {
    BOOL init_value = signaled ? TRUE : FALSE;
    event_ = CreateEvent(NULL, TRUE, init_value, NULL);
    return (event_ != NULL) ? 0 : -1;
  }
  int CleanUp() {
    CloseHandle(event_);
    return 0;
  }

  int Reset() {
    return ResetEvent(event_) ? 0 : -1;
  }
  int Signal() {
    return SetEvent(event_) ? 0 : -1;
  }

  int Wait() {
    DWORD result = WaitForSingleObject(event_, INFINITE);
    return result == WAIT_OBJECT_0 ? 0 : -1;
  }

 private:
  HANDLE event_;
};


typedef VOID (WINAPI *InitializeSRWLockPtr)(__out  PVOID *SRWLock);
typedef VOID (WINAPI *ReleaseSRWLockExclusivePtr)(__inout  PVOID *SRWLock);
typedef VOID (WINAPI *ReleaseSRWLockSharedPtr)(__inout  PVOID *SRWLock);
typedef VOID (WINAPI *AcquireSRWLockExclusivePtr)(__inout  PVOID *SRWLock);
typedef VOID (WINAPI *AcquireSRWLockSharedPtr)(__inout  PVOID *SRWLock);


class OsRwLock {
 public:
  OsRwLock() : srw_lock_(NULL) {}
  ~OsRwLock() {}
  int Initialize();
  int CleanUp();
  int AquireRdLock();
  int AquireWrLock();
  int ReleaseRdLock();
  int ReleaseWrLock();

  private:
  // implement if >=vista
  static int s_has_srw_;
  static InitializeSRWLockPtr SRWInit;
  static ReleaseSRWLockExclusivePtr SRWEndWrite;
  static ReleaseSRWLockSharedPtr SRWEndRead;
  static AcquireSRWLockExclusivePtr SRWStartWrite;
  static AcquireSRWLockSharedPtr SRWStartRead;

  PVOID srw_lock_;

  // Implement if xp
  CRITICAL_SECTION m_csWrite;
  CRITICAL_SECTION m_csReaderCount;
  long m_cReaders;
  HANDLE m_hevReadersCleared;
};

class OsThread {
 public:
  // thread name, most of time used by debugger
  explicit OsThread(const char* thread_name);
  ~OsThread();

  int Startup();   // return err_t
  int Shutdown();  // return err_t

  virtual int Run() = 0;   // return err_t

 protected:
  static UINT WINAPI ThreadEntry(LPVOID lpParam);

 private:
  OsThread(OsThread&);
  void operator=(OsThread&);

  bool     started_;
  unsigned thread_id_;
  HANDLE   thread_handle_;
  std::string thread_name_;
};

class OsWorkerThread {
public:
	// thread name, most of time used by debugger
	explicit OsWorkerThread(
		const char* thread_name,
		IRunnable* runnable,
		bool auto_delete);
	~OsWorkerThread();

	int Startup();   // return err_t
	int Shutdown();  // return err_t

	static UINT WINAPI ThreadFunc(LPVOID lpParam);

private:
	OsWorkerThread(OsWorkerThread&);
	void operator=(OsWorkerThread&);

	IRunnable* runnable_;
	bool auto_delete_;
	bool started_;
	unsigned thread_id_;
	HANDLE   thread_handle_;
	std::string thread_name_;

};

//class OsCond {
//public:
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
//private:
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
      int level, int optname, void* optval, int* optlen);
  virtual int SetSockOpt(
      int level, int optname, const void* optval, int optlen);
  /**
   * @param how: how to shutdown, "r", "w", and "rw"
   */
  virtual int Shutdown(const char* how);

  virtual int Send(const char *buffer, int length, int flags);
  virtual int Send(const std::string &buffer, int flags);
  virtual int Recv(char *buffer, int length, int flags);
  virtual int Recv(std::string *buffer, int flags);

  int fd() {
    return static_cast<int>(fd_);  // safe cast
  }

  const std::string& uri() {
    return uri_;
  }

  void Swap(OsTcpSocket* that) {
	  std::swap(fd_, that->fd_);
	  uri_.swap(that->uri_);
  }

 protected:
  SOCKET fd_;
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
  explicit OsUdpSocket(const char* uri);
  explicit OsUdpSocket(const std::string& uri);
  ~OsUdpSocket();

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
  int SendTo(const std::string &buffer, int flags,
             const std::string &dst_addr);
  int RecvFrom(std::string *buffer, int flags, std::string *src_addr);
  int EnableBroadcast();
  int EnableMulticast(const std::string& multicast_addr);
  int fd() { return static_cast<int>(fd_); }

 protected:
  SOCKET fd_;
  std::string uri_;

 private:
  OsUdpSocket(OsUdpSocket&);
  void operator=(OsUdpSocket&);
};

class AtomicInt64 {
public:
	explicit AtomicInt64(int64_t init_value = 0) {
    spin_lock = 0;
		value_ = init_value;
	}

	// ~AtomicInt64();

	int64_t value() { 
	  return value_; 
	}
	void set_value(int64_t new_value) {
    LONG ret;
    do {
      ret = InterlockedCompareExchange(&spin_lock, 1, 0);
      if (ret != 0) OutputDebugStringA("spin lock try again\n");
    } while (ret != 0) ; // loop until 0

    *((volatile int64_t*)&value_) = new_value;
    
    ret = InterlockedCompareExchange(&spin_lock, 0, 1);
    assert(ret == 1);
	}

	int64_t FetchAndInc(int64_t count) {
    LONG ret;
    int64_t old_value;
    
    do {
      ret = InterlockedCompareExchange(&spin_lock, 1, 0);
    } while (ret != 0) ; // loop until 0

	  old_value = value_;
    value_ += count;

    ret = InterlockedCompareExchange(&spin_lock, 0, 1);
    assert(ret == 1);

    return old_value;
	}
  int64_t FetchAndSub(int64_t count) {
    LONG ret;
    int64_t old_value;

    do {
      ret = InterlockedCompareExchange(&spin_lock, 1, 0);
    } while (ret != 0) ; // loop until 0

    old_value = value_;
    value_ -= count;

    ret = InterlockedCompareExchange(&spin_lock, 0, 1);
    assert(ret == 1);

    return old_value;
	}

private:
	// CRITICAL_SECTION mutex_;
  volatile LONG spin_lock;
	volatile int64_t value_;
};

class AtomicInt32 {
public:
	explicit AtomicInt32(int32_t init_value = 0) {
	  *((volatile int32_t*)&value_) = init_value;
	}

	// ~AtomicInt32();

	int32_t value() {
	  return *((volatile int32_t*)&value_);
	}
	void set_value(int32_t new_value) {
	  *((volatile int32_t*)&value_) = new_value;
	}

	int32_t FetchAndInc(int32_t count) {
    return InterlockedExchangeAdd(&value_, count);
  }
  
  int32_t FetchAndSub(int32_t count) {
    return InterlockedExchangeAdd(&value_, -count);
  }

private:
	// CRITICAL_SECTION mutex_;
	volatile LONG value_;
};


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
  OsTimer(int interval_in_ms, IOnTimer* timer_func):
      interval_in_ms_(interval_in_ms), timer_func_(timer_func),
      timer_(INVALID_HANDLE_VALUE) {
        due_time_in_ms_ = interval_in_ms;
      }
  OsTimer(int due_time_in_ms, int interval_in_ms, IOnTimer* timer_func):
      due_time_in_ms_(due_time_in_ms),
      interval_in_ms_(interval_in_ms), timer_func_(timer_func),
      timer_(INVALID_HANDLE_VALUE) {}
  ~OsTimer() {}

  /**
   * return 0 if success
   */
  int Initialize();
  int CleanUp();

  static VOID CALLBACK  TimerRoute(PVOID lpParam, BOOLEAN TimerOrWaitFired);

 protected:
  int due_time_in_ms_;
  int interval_in_ms_;
  IOnTimer* timer_func_;
  HANDLE  timer_;

  OsTimer(const OsTimer&);
  void operator=(const OsTimer&);
};

class OsTimeOut : public OsTimer{
 public:
  OsTimeOut(int timeout_in_ms, IOnTimer* timeout_func):
    OsTimer(timeout_in_ms, 0, timeout_func){}

  int CleanUp();
};

class OsFsTraverser {
 public:
  OsFsTraverser(const std::string& root, IFsVisitor *visitor):root(root), 
    visitor(visitor) {}
  /* traverse the directory, for each file and dir, call
   * visitor->Visit(file_stat). In this function, you can also use
   * visitor->IsIgnore te determin the file or (dir and its sub files should be
   * ignored*/
  int traverse();

 protected:
  BOOL TraverseRecurcively(LPCTSTR szSubtree);

 private:
  OsFsTraverser(OsFsTraverser&);
  void operator=(OsFsTraverser&);

  const std::string root;
  IFsVisitor *visitor;
};

class OsFile {
public:
  OsFile() :pfile_(INVALID_HANDLE_VALUE),  file_end_(0){}
  ~OsFile() {
    if (pfile_ != INVALID_HANDLE_VALUE) {
	    CloseHandle(pfile_);
      pfile_ = INVALID_HANDLE_VALUE;
	  }
	}

  int Open(const std::string& path, const std::string &alias, const char* mode);
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
  int eof(){
	return file_end_;
  }
  void Close() {
    if (pfile_ != INVALID_HANDLE_VALUE) {
	  CloseHandle(pfile_);
	  pfile_ = INVALID_HANDLE_VALUE;
	}
  };

  void ChangeToWinMode(const char* mode, LPDWORD access, LPDWORD creation_disposition);

  static const char* ModeRead;
  static const char* ModeWrite;

private:
  HANDLE pfile_;
  int file_end_;
};

inline Platform GetPlatform() {
  return PLATFORM_WINDOWS;
}

inline std::string GetPlatformWithString() {
  return "Windows";
} 

inline int32_t GetPlatformWithNum() {
  return 1;
}

int ListIpAddresses(
  std::vector<struct sockaddr_in>* ipv4,
  std::vector<struct sockaddr_in6>* ipv6);

}  // namespace zs

extern "C" {
	int gettimeofday(struct timeval * tp, struct timezone * tzp);
};
#endif  // ZISYNC_KERNEL_PLATFORM_PLATFORM_WINDOWS_H_
