/**
 * @file platform_linux.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Linux platform implementation.
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
#include <errno.h>
#include <unistd.h>
// #define _DARWIN_FEATURE_64_BIT_INODE
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <fts.h>
#include <uuid/uuid.h>
#include <cstring>
#include <cstdio>
#include <openssl/ssl.h>
#include <libgen.h>
#include <iostream>
#include <stdlib.h>
#include "zisync/kernel/platform/platform.h"
#include <ifaddrs.h>
#include <fstream>
using std::placeholders::_1;

namespace zs {

int OsDeleteFile(const char* path, bool move_to_trash) {
  int rc = unlink(path);
  if (rc == 0 || (rc == -1 && errno == ENOENT)) {
    return 0;
  }
  return rc;
}

int OsCreateDirectory(const char* path, bool delete_file_existing) {
 for (int i = 0; i < 2; i ++) {
    int rc = mkdir(path, 0777);
    if (rc == 0) {
        
      return 0;
    }
    /*  rc == -1 */
    if (errno == ENOENT && (i == 0)) {
      int ret = OsCreateDirectory(OsDirName(path), delete_file_existing);
      if (ret != 0) {
        return ret;
      }
    } else if (errno == EEXIST) {
      if (OsFileExists(path)) {
        if (delete_file_existing) {
          /* delete file try Create again */
          int ret = OsDeleteFile(path, false);
          if (ret != 0) {
            return ret;
          }
        } else {
          return -1;
        }
      } else {
        return 0;
      }
    } else {
      return -1;
    }
  }

  return 0;
}

FILE* OsFopen(const char* path, const char* mode) {
  return fopen(path, mode);
}

int OsPathAppend(char* path1, int path1_capacity, const char* path2) {
  assert(path1 != NULL && path2 != NULL);
  size_t length = strlen(path1);

  if (path1[length - 1] == '/') {
    snprintf(path1 + length, path1_capacity - length, "%s", path2);
  } else {
    path1[length] = '/';
    path1[length + 1] = '\0';
    snprintf(path1 + length + 1,
             path1_capacity - length -2, "%s", path2);
  }

  return 0;
}

int OsPathAppend(std::string* path1, const std::string& path2) {
  assert(path1 != NULL);

  if (path1->size() > 0) {
    if (path1->at(path1->size() - 1) == '/') {
      path1->append(path2);
    } else {
      path1->append(1, '/');
      path1->append(path2);
    }
  }else {
    path1->append(path2);
  }

  return 0;
}

bool OsExists(const char* path) {
  if (access(path, F_OK) != -1) {
    return true;
  } else {
    return false;
  }
}

bool OsFileExists(const char *path) {
  struct stat stat_;
  if (lstat(path, &stat_) != 0) {
    return false;
  }
  if (S_ISREG(stat_.st_mode)) {
    return true;
  } else {
    return false;
  }
}

bool OsDirExists(const char *path) {
  struct stat stat_;
//    if(strstr(path, AssetUrlParser::AssetsRoot) == path) {
//        return true;
//    }
  if (stat(path, &stat_) != 0) {
    return false;
  }
  if (S_ISDIR(stat_.st_mode)) {
    return true;
  } else {
    return false;
  }
}

int OsGetFullPath(const char* relative_path, std::string* full_path) {
  // @TODO(wangwencan): implement it;
  char *ret = realpath(relative_path, NULL);
  if (ret) {
    full_path->assign(ret);
    return 0;
  }else {
    return -1;
  }
}

int64_t OsTime() {
  return time(NULL);
}

int64_t OsTimeInS() {
  return time(NULL);
}

int64_t OsTimeInMs() {
  struct timeval time;
  gettimeofday(&time, NULL);
  return (int64_t)(time.tv_sec * 1000 + time.tv_usec / 1000);
}

int OsGetHostname(std::string* hostname) {
  static char buf[_SC_HOST_NAME_MAX + 1];
  int ret = gethostname(buf, _SC_HOST_NAME_MAX + 1);
  if (ret == -1) {
    hostname->clear();
    return -1;
  } else {
    *hostname = buf;
    return 0;
  }
}

const char* OsGetLastErr() {
  return strerror(errno);
}


static void* thread_func(void* args) {
  OsThread *thread = static_cast<OsThread*>(args);
  int ret = thread->Run();
  return reinterpret_cast<void*>(ret);
}

int OsDeleteDirectory(const char* path) {
  int rv = rmdir(path); 
  if (rv != -1) {
    return 0;
  }
  // in our system if rm return NOENT, we can ignore the error
  if (errno == ENOENT) {
    return 0;
  }
  if (errno == ENOTDIR) {
    return OsDeleteFile(path, false);
  }
  return -1;
}

int OsDeleteDirectories(const char* path, bool delete_self /* = true */) {
  FTS *tree = NULL;
  char *fts_path[2] = { const_cast<char*>(path), NULL };
  tree = fts_open(fts_path, FTS_NOCHDIR | FTS_PHYSICAL, NULL);
  if (tree == NULL) {
    if (errno == ENOENT) {
      return 0;
    } else {
      return -1;
    }
  }
  FTSENT *node;
  int ret = 0;
  while ((node = fts_read(tree))) {
    if (!delete_self && node->fts_level == 0) {
      continue;
    }
    switch (node->fts_info) {
      case FTS_SL:
      case FTS_F:
        ret = unlink(node->fts_path);
        if (ret != 0) {
          goto final;
        }
        break;
      case FTS_DP:
        ret = rmdir(node->fts_path);
        if (ret != 0) {
          goto final;
        }
        break;
      default:
        break;
    }
  }
final:
  fts_close(tree);
  return ret;
}

int OsSetMtime(const char *path, int64_t mtime_in_ms) {
  struct stat statbuf;
  struct timeval times[2];

  if (lstat(path, &statbuf) < 0) {
    return -1;
  }

  times[0].tv_sec = statbuf.st_atimespec.tv_sec;
  times[0].tv_usec = (__darwin_suseconds_t)statbuf.st_atimespec.tv_nsec / 1000;

  times[1].tv_sec = (__darwin_time_t) mtime_in_ms / 1000;
  times[1].tv_usec = (__darwin_suseconds_t)
      (mtime_in_ms - times[1].tv_sec * 1000) * 1000;

  if (utimes(path, times) < 0) {
    return -1;
  }
  return 0;
}

int OsChmod(const char *path, int32_t attr) {
  return chmod(path, attr);
}

int OsRename(const char* src, const char* dst, bool is_overwrite /* = true */) {
  if (!is_overwrite && OsExists(dst)) {
    errno = EEXIST;
    return -1;
  }

  int ret = rename(src, dst);
  if (ret == 0) {
    return ret;
  }
  string dst_parent_dir = OsDirName(dst);
  if (OsExists(dst_parent_dir)) {
    return -1;
  }
  if (OsCreateDirectory(dst_parent_dir, false) != 0) {
    return -1;
  }
  return rename(src, dst);
}

int OsLstat(const char *path, struct stat *st) {
  return lstat(path, st);
}

int OsThread::Startup() {
  return pthread_create(&pid, NULL, thread_func, this);
}

int OsThread::Shutdown() {
  void *ret;
  int err;
  if (pid != static_cast<pthread_t>(NULL)) {
    err = pthread_join(pid, &ret);
    if (err != 0) {
      return err;
    }
    pid = NULL;
    return reinterpret_cast<intptr_t>(ret);
  }
  return 0;
}

void* OsWorkerThread::ThreadFunc(void* args) {
  int ret = 0;
  OsWorkerThread* thread = static_cast<OsWorkerThread*>(args);
  if (thread->runnable_) {
    ret = thread->runnable_->Run();
    if (thread->auto_delete_) {
      delete thread->runnable_;
      thread->runnable_ = NULL;
      thread->auto_delete_ = false;
    }
  }
  return reinterpret_cast<void*>(ret);
}

int OsWorkerThread::Startup() {
  return pthread_create(&pid, NULL, ThreadFunc, this );
}

int OsWorkerThread::Shutdown() {
  void *ret;
  int err;
  if (pid != static_cast<pthread_t>(NULL)) {
    err = pthread_join(pid, &ret);
    if (err != 0) {
      return err;
    }
    return reinterpret_cast<intptr_t>(ret);
  }
  return 0;
}

const int UUID_LEN = 36;
void OsGenUuid(string *uuid) {
  char uuid_buf[UUID_LEN + 1];
  uuid_t _uuid;
  uuid_generate(_uuid);
  uuid_unparse(_uuid, uuid_buf);
  uuid->assign(uuid_buf);
}

OsTcpSocket::OsTcpSocket(const char* uri) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ != -1);
  uri_ = uri;
}

OsTcpSocket::OsTcpSocket(const std::string& uri) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ != -1);
  uri_ = uri;
}

OsTcpSocket::~OsTcpSocket() {
  if (fd_ != -1) {
    close(fd_);
  }
}

int OsTcpSocket::Bind() {
  struct sockaddr_in servaddr;
  size_t pos_first_colon = uri_.find(":");
  assert(pos_first_colon > 0 && pos_first_colon < uri_.length() - 2);
  size_t pos_second_colon = uri_.find(":", pos_first_colon + 1);
  assert(pos_second_colon > pos_first_colon + 1 &&
         pos_second_colon < uri_.length() - 1);

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(atoi(uri_.substr(pos_second_colon + 1).c_str()));
  std::string addr =
      uri_.substr(pos_first_colon + 3, pos_second_colon - pos_first_colon - 3);
  if (addr == "*") {
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    if (inet_pton(AF_INET, addr.c_str(), &servaddr.sin_addr.s_addr) != 1) {
      return -1;
    }
  }

  // int optval = 1;
  // if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
  //   return -1;
  // }
    int ret = ::bind(fd_, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret == -1 && errno == EADDRINUSE) {
    return errno;
  } else {
    return ret;
  }
}

int OsTcpSocket::Listen(int backlog) {
  return listen(fd_, backlog);
}

int OsTcpSocket::Accept(OsTcpSocket **accepted_socket) {
  // @TODO(panghai@ict.ac.cn): make uri using accepted address
  //  int connfd = accept(fd_, NULL, NULL);
  //  if (connfd == -1) {
  //    *accepted_socket = NULL;
  //    return -1;
  //  }
  //
  //  std::string uri;
  //  OsTcpSocket* socket = new OsTcpSocket(uri);
  //  socket->SetSocket(connfd);
  //  *accepted_socket = socket;
  struct sockaddr_in sockaddr;
  socklen_t length = sizeof(sockaddr);
  int connfd = accept(fd_, (struct sockaddr*)&sockaddr, &length);
  if (connfd == -1) {
    *accepted_socket = NULL;
    return -1;
  }

  const int URI_LEN = 64;
  char uri[URI_LEN] = {0};
  char addr[INET_ADDRSTRLEN] = {0};
  int32_t port = ntohs(sockaddr.sin_port);
  const char *ret = inet_ntop(
      AF_INET, &sockaddr.sin_addr.s_addr, addr, INET_ADDRSTRLEN);
  assert(ret != NULL);
  int ret_val = snprintf(uri, sizeof(uri), "tcp://%s:%" PRId32, addr, port);
  assert(ret_val > 0 && ret_val < URI_LEN);
  OsTcpSocket* socket = new OsTcpSocket(uri);
  assert(socket != NULL);
  socket->SetSocket(connfd);
  *accepted_socket = socket;

  return 0;
}

int OsTcpSocket::Connect() {
  struct sockaddr* sa;
  OsSocketAddress address(uri_);

  for(sa = address.NextSockAddr(); sa != NULL; sa = address.NextSockAddr()) {
    int ret = connect(fd_, sa, sizeof(*sa));
    if (ret == 0) {
      return ret;
    } else {
      perror("connet() faid");
    }
  }

  return -1;
}


int OsTcpSocket::GetSockOpt(
    int level, int optname, void* optval, socklen_t* optlen) {
  return getsockopt(fd_, level, optname, optval, optlen);
}

int OsTcpSocket::SetSockOpt(
    int level, int optname, void* optval, socklen_t optlen) {
  return setsockopt(fd_, level, optname, optval, optlen);
}

int OsTcpSocket::Shutdown(const char* how) {
  if (strcmp(how, "r") == 0) {
    return shutdown(fd_, SHUT_RD);
  } else if (strcmp(how, "w") == 0) {
    return shutdown(fd_, SHUT_WR);
  } else if (strcmp(how, "rw") == 0) {
    return shutdown(fd_, SHUT_RDWR);
  }

  return -1;
}

int OsTcpSocket::Send(const char *buffer, int length, int flags) {
  return send(fd_, buffer, length, flags);
}

int OsTcpSocket::Send(const string &buffer, int flags) {
  return send(fd_, buffer.c_str(), buffer.length(), flags);
}

int OsTcpSocket::Recv(char *buffer, int length, int flags) {
  return recv(fd_, buffer, length, flags);
}

int OsTcpSocket::Recv(string *buffer, int flags) {
  char recv_buffer[512 + 1] = {0};
  int length = recv(fd_, recv_buffer, 512, flags);
  if (length == -1) {
    return -1;
  }
  *buffer = recv_buffer;

  return length;
}

void OsTcpSocket::SetSocket(int socket) {
    if(fd_ != -1){
        close(fd_);
    }
    fd_ = socket;
}

class SslTcpSocket : public OsTcpSocket {
 public:
  explicit SslTcpSocket(const std::string& uri, void* ctx)
      : OsTcpSocket(uri) {
        uri_ = uri;
        ctx_ = reinterpret_cast<SSL_CTX*>(ctx);
        ssl_ = SSL_new(ctx_);
        assert(ssl_ != NULL);
      }

  virtual ~SslTcpSocket() {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
  }

  virtual int Accept(OsTcpSocket** accepted_socket) {
    struct sockaddr_in sockaddr;
    socklen_t length = sizeof(sockaddr);
    int connfd = accept(fd_, (struct sockaddr*)&sockaddr, &length);
    if (connfd == -1) {
      *accepted_socket = NULL;
      return -1;
    }

    char uri[30] = {0};
    char addr[INET_ADDRSTRLEN] = {0};
    int16_t port = ntohs(sockaddr.sin_port);
    const char *ret = inet_ntop(
        AF_INET, &sockaddr.sin_addr.s_addr, addr, INET_ADDRSTRLEN);
    assert(ret != NULL);
    int ret_val = snprintf(uri, sizeof(uri), "tcp://%s:%d", addr, port);
    assert(ret_val > 0 && ret_val < 30);
    SslTcpSocket* socket = new SslTcpSocket(uri, ctx_);
    assert(socket != NULL);
    socket->SetSocket(connfd);
    *accepted_socket = socket;

    return socket->Accept();
  }

  /* @return : 0 if success, EADDRINUSE if addr in use, others -1*/
  virtual int Connect() {
    if (OsTcpSocket::Connect() == -1) {
      return -1;
    }

    if (SSL_set_fd(ssl_, fd_) != 1) {
      return -1;
    }

    if (SSL_connect(ssl_) != 1) {
      return -1;
    }

    return 0;
  }
  /**
   * @param how: how to shutdown, "r", "w", and "rw"
   */
  virtual int Send(const char *buffer, int length, int flags) {
    return SSL_write(ssl_, buffer, length);
  }

  virtual int Recv(char *buffer, int length, int flags) {
    return SSL_read(ssl_, buffer, length);
  }

 protected:
  int Accept() {
    if (SSL_set_fd(ssl_, fd_) != 1) {
      return -1;
    }

    if (SSL_accept(ssl_) != 1) {
      return -1;
    }

    return 0;
  }

 private:
  SSL* ssl_;
  SSL_CTX* ctx_;
};

class FileDumyTcpSocket : public OsTcpSocket {
 public:
  explicit FileDumyTcpSocket(const std::string& uri)
      : OsTcpSocket(uri) {
        uri_ = uri;
        write_fp_ = NULL;
        read_fp_ = NULL;
      }
  virtual ~FileDumyTcpSocket() {
    if (write_fp_ != NULL) {
      fclose(write_fp_);
    }
    if (read_fp_ != NULL) {
      fclose(read_fp_);
    }
  }

  // the addr should be proto://IP:Port
  virtual int Bind() {
    return 0;
  }

  virtual int Listen(int /* backlog */) {
    return 0;
  }
  virtual int Accept(OsTcpSocket ** /* accepted_socket */) {
    return 0;
  }

  virtual int Shutdown(const char* how) {
    return 0;
  }

  virtual int Connect() {
    size_t index = uri_.find(":");
    if (static_cast<int>(index) == -1 ||
        index == 0 ||
        index == uri_.length() - 1) {
      return -1;
    }

    if (uri_.at(index + 1) != '/' || uri_.at(index + 2) != '/') {
      return -1;
    }

    if (uri_.length() == index + 3) {
      return -1;
    }

    write_fp_ = fopen(uri_.substr(index + 3).c_str(), "w");
    if (write_fp_ == NULL) {
      return -1;
    }

    index = uri_.find("tmp_");
    std::string read_file;
    read_file.append("transfer/src/");
    if (index == std::string::npos) {
      read_file.append("put_response");
    } else {
      read_file.append(uri_.substr(index + 4));
    }
    read_fp_ = fopen(read_file.c_str(), "r");
    if (read_fp_ == NULL) {
      return -1;
    }

    fd_ = open(read_file.c_str(), O_RDONLY);
    assert(fd_ >= 0);

    return 0;
  }

  virtual int Send(const char *buffer, int length, int flags) {
    return fwrite(buffer, 1, length, write_fp_);
  }

  virtual int Send(const string &buffer, int flags) {
    return fwrite(buffer.c_str(), 1, buffer.length(), write_fp_);
  }

  virtual int Recv(char *buffer, int length, int flags) {
    return fread(buffer, 1, length, read_fp_);
  }

  virtual int Recv(string *buffer, int flags) {
    return 0;
  }

 private:
  FILE *write_fp_, *read_fp_;
  std::string uri_;
  FileDumyTcpSocket(FileDumyTcpSocket&);
  void operator=(FileDumyTcpSocket&);
};


OsTcpSocket* OsTcpSocketFactory::Create(const std::string& uri, void* arg) {
  if (strncmp(uri.c_str(), "tcp", 3) == 0 && arg == NULL) {
    return new OsTcpSocket(uri);
  } else if (strncmp(uri.c_str(), "file", 4) == 0) {
    return new FileDumyTcpSocket(uri);
  } else if (strncmp(uri.c_str(), "tcp", 3) == 0 && arg != NULL) {
    return new SslTcpSocket(uri, arg);
  }

  return NULL;
}


int OsUdpSocket::Bind() {
  struct sockaddr_in servaddr;
  size_t pos_first_colon = uri_.find(":");
  assert(pos_first_colon > 0 && pos_first_colon < uri_.length() - 1);
  size_t pos_second_colon = uri_.find(":", pos_first_colon + 1);
  assert(pos_second_colon > pos_first_colon + 1 &&
         pos_second_colon < uri_.length() - 1);

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(atoi(uri_.substr(pos_second_colon + 1).c_str()));
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  // int optval = 1;
  // if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
  //   return -1;
  // }
  int ret = ::bind(fd_, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret == -1 && errno == EADDRINUSE) {
    return errno;
  } else {
    return ret;
  }
}

int OsUdpSocket::RecvFrom(string *buffer, int flags, string *src_addr) {//todo: need think through
    buffer->resize(2048);
    struct sockaddr addr;
    socklen_t addr_length = sizeof(addr);
    int nbytes = recvfrom(
        fd_, const_cast<char*>(buffer->data()), 2048, flags, &addr, &addr_length);
    if (nbytes >= 0) {
        buffer->resize(nbytes);
        if (src_addr != NULL) {
            src_addr->assign(reinterpret_cast<char*>(&addr), addr_length);
        }
        return nbytes;
    } else {
        // error occur
        // ZSLOG_ERROR("recvfrom failed(%d): %s", errno, strerror(errno));
        fprintf(stderr, "%s(%d): recvfrom failed(%d): %s\n", __FILE__, __LINE__, errno, strerror(errno));
        if(errno == ENOTCONN){
            close(fd_);
            fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            assert(fd_ != -1);
            this ->Bind();
        }
        return -1;
    }

}

int OsUdpSocket::Shutdown(const char* how) {
  if (strcmp(how, "r") == 0) {
    return shutdown(fd_, SHUT_RD);
  } else if (strcmp(how, "w") == 0) {
    return shutdown(fd_, SHUT_WR);
  } else if (strcmp(how, "rw") == 0) {
    return shutdown(fd_, SHUT_RDWR);
  }

  return -1;
}

int OsUdpSocket::EnableBroadcast() {
    int bOptVal = 1;
    ZSLOG_INFO("OsUdpSocket::EnableBroadcast");
  return setsockopt(fd(), SOL_SOCKET, SO_BROADCAST, (const char*)&bOptVal,
                    sizeof(int));
}

int OsUdpSocket::EnableMulticast(const std::string& multicast_addr) {
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr=inet_addr(multicast_addr.c_str());
  mreq.imr_interface.s_addr=htonl(INADDR_ANY);
  if (setsockopt(fd(),IPPROTO_IP,IP_ADD_MEMBERSHIP,
                 (const char*)&mreq, sizeof(mreq)) < 0) {
    return -1;
  }
  return 0;
}

AtomicInt64::AtomicInt64(int64_t init_value) {
  std::atomic_store(&value_, init_value);
}

int64_t AtomicInt64::value() {
  return std::atomic_load(&value_);
}
void AtomicInt64::set_value(int64_t new_value) {
  std::atomic_store(&value_, new_value);
}

int64_t AtomicInt64::FetchAndInc(int64_t value) {
  return std::atomic_fetch_add(&value_, value);
}

int64_t AtomicInt64::FetchAndSub(int64_t value) {
  return std::atomic_fetch_sub(&value_, value);
}

AtomicInt32::AtomicInt32(int32_t init_value) {
  std::atomic_store(&value_, init_value);
}

int32_t AtomicInt32::value() {
  return std::atomic_load(&value_);
}
void AtomicInt32::set_value(int32_t new_value) {
  std::atomic_store(&value_, new_value);
}

int32_t AtomicInt32::FetchAndInc(int32_t value) {
  return std::atomic_fetch_add(&value_, value);
}

int32_t AtomicInt32::FetchAndSub(int32_t value) {
  return std::atomic_fetch_sub(&value_, value);
}

int OsGetThreadId() {
  // On Linux and MacOSX, we try to use gettid().
#if defined OS_LINUX || defined OS_MACOSX
#ifndef __NR_gettid
#ifdef OS_MACOSX
#define __NR_gettid SYS_gettid
#elif !defined __i386__
#error "Must define __NR_gettid for non-x86 platforms"
#else
#define __NR_gettid 224
#endif
#endif
  static bool lacks_gettid = false;
  if (!lacks_gettid) {
    pid_t tid = syscall(__NR_gettid);
    if (tid != -1) {
      return tid;
    }
    // Technically, this variable has to be volatile, but there is a small
    // performance penalty in accessing volatile variables and there should
    // not be any serious adverse effect if a thread does not immediately see
    // the value change to "true".
    lacks_gettid = true;
  }
#endif  // OS_LINUX || OS_MACOSX

  return getpid();  // Linux:  getpid returns thread ID when gettid is absent
}

bool OsGetTimeString(char* buffer, int buffer_length) {
if (buffer_length < 18) {
    return false;
  }

  time_t time_;
  time(&time_);
  struct tm tm_;
  localtime_r(&time_, &tm_);

  strftime(buffer, buffer_length, "%F %X", &tm_);
  return true;
}



static void timer_fired_handler(CFRunLoopTimerRef timer, void *info){

    if(info == NULL)return;
    IOnTimer *timer_handler = (IOnTimer*)info;
    timer_handler ->OnTimer();
}

int ZsTimerSchedulerThreadInstantCnt = 0;
void  ZsTimerSchedulerThread::initIdleTimer(){
        double tm_in_sec = 10;
        CFRunLoopTimerContext context = {0, this, NULL , NULL, NULL};
        idleTimer_  = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + tm_in_sec, tm_in_sec, 0, 0, timer_fired_handler, &context);
};

void ZsTimerSchedulerThread::RemoveIdleTimer()
{
    if(runloop == NULL)return;
    CFRunLoopRemoveTimer(runloop, idleTimer_, kCFRunLoopCommonModes);
    printf("ZsTimerSchedulerThread::RemmoveTimer: remain cnt: %d\n", --numberOfTimers);

    if(idleTimer_ != NULL){
        CFRelease(idleTimer_);
        idleTimer_ = NULL;
    }
    if(runloop != NULL){
        //CFRelease(runloop);
        runloop = NULL;
    }
    numberOfTimers = 0;
}

bool ZsTimerSchedulerThread::runloopValid()
{
    return runloop != NULL;
}

bool ZsTimerSchedulerThread::runloopWaiting()
{
    return runloopValid() && CFRunLoopIsWaiting(runloop);
}

bool ZsTimerSchedulerThread::isTimerScheduled(CFRunLoopTimerRef timer_)
{
    return timer_ != NULL && runloopValid() && CFRunLoopContainsTimer(runloop, timer_, kCFRunLoopCommonModes);
}

bool ZsTimerSchedulerThread::ScheduleTimerUnsafe(CFRunLoopTimerRef timer)
{
    if(runloopValid()){
        printf("ZsTimerSchedulerThread::ScheduleTimer, number: %d\n", ++numberOfTimers);

        CFRunLoopAddTimer(runloop, timer, kCFRunLoopCommonModes);
        return true;
    }else{
        todo.push_back(timer);
        return false;
    }
}

bool ZsTimerSchedulerThread::ScheduleTimer(CFRunLoopTimerRef timer)
{
    MutexAuto mut(&mutx);
    return ScheduleTimerUnsafe(timer);
}

void ZsTimerSchedulerThread::RemoveTimer(CFRunLoopTimerRef timer_)
{
    MutexAuto mut(&mutx);
    if(isTimerScheduled(timer_)){
        printf("ZsTimerSchedulerThread::RemmoveTimer: remain cnt: %d\n", --numberOfTimers);

        CFRunLoopRemoveTimer(runloop, timer_, kCFRunLoopCommonModes);
    }
    if(numberOfTimers == 1){
      RemoveIdleTimer();  
    }
    
}

int ZsTimerSchedulerThread::Run(){
    mutx.AquireMutex();
    runloop = CFRunLoopGetCurrent();
    if(runloop){
        for(int i = 0; i < todo.size(); i++){
            this ->ScheduleTimerUnsafe(todo[i]);
        }
    } 
    mutx.ReleaseMutex();

    if(runloop != NULL){
        assert(idleTimer_);
        this ->ScheduleTimer(idleTimer_);

        mutx.AquireMutex();
        for(int i = 0; i < todo.size(); i++){
            this ->ScheduleTimerUnsafe(todo[i]);
        }
        todo.clear();
        mutx.ReleaseMutex();   

        CFRunLoopRun();
    }else{
        ZSLOG_INFO("failed to get runloop");
    }
    ZSLOG_INFO("ZsTimerSchedulerThread quit normally");
    return 0;
};   // return error_t

void ZsTimerSchedulerThread::OnTimer()
{
    //better with try lock
    if(todo.size() == 0){
        return;
    }
    mutx.AquireMutex();
    if(runloopValid()){
        for(int i = 0; i < todo.size(); i++){
            this ->ScheduleTimerUnsafe(todo[i]);
        }
        todo.clear();
    }
    mutx.ReleaseMutex();
}
vector<CFRunLoopTimerRef> ZsTimerSchedulerThread::dump()
{
    return todo;
}

void ZsTimerSchedulerThread::import(const vector<CFRunLoopTimerRef> &toto_)
{
    MutexAuto mut(&mutx);
    for(int i = 0; i < toto_.size(); i++){
        todo.push_back(toto_[i]);
    }
}
ZsTimerSchedulerThread * OsTimer::timer_scheduler_ = NULL;

Mutex & getOsTimerMutex()
{
    static Mutex mutex;
    return mutex;
}
    
OsTimer::OsTimer(int due_time_in_ms, int interval_in_ms, IOnTimer* timer_func) {
    printf("OsTimer::OsTimer(int, IOnTimer*)\n");
    due_time_in_ms_ = due_time_in_ms;
    interval_in_ms_ = interval_in_ms;
    timer_func_ = timer_func;
    timer_ = NULL;
    getOsTimerMutex().Aquire();
    if(OsTimer::timer_scheduler_ == NULL){
        OsTimer::timer_scheduler_ = new ZsTimerSchedulerThread(threadName().c_str());
    }
    getOsTimerMutex().Release();
}

OsTimer::OsTimer(int interval_in_ms, IOnTimer* timer_func)
  : OsTimer(interval_in_ms, interval_in_ms, timer_func) {}

int OsTimer::Initialize() {
    printf("OsTimer::Initialize\n");

    double tm_in_sec = 0.001 * (double)interval_in_ms_; 
    double tm_in_sec_due = 0.001 * (double)due_time_in_ms_;
    CFRunLoopTimerContext context = {0, timer_func_, NULL , NULL, NULL};
    timer_ = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + tm_in_sec_due, tm_in_sec, 0, 0, timer_fired_handler, &context);

    if(!OsTimer::timer_scheduler_ ->ScheduleTimer(timer_)){
        ZsTimerSchedulerThread * another = new ZsTimerSchedulerThread(threadName().c_str());
        vector<CFRunLoopTimerRef> tmp = OsTimer::timer_scheduler_ ->dump();
        another ->import(tmp);
        delete OsTimer::timer_scheduler_;
        OsTimer::timer_scheduler_ = another;
    }
    return 0;
}

int OsTimer::CleanUp() {
    printf("OsTimer::CleanUp\n");
    if(OsTimer::timer_scheduler_ != NULL && timer_ != NULL){
        OsTimer::timer_scheduler_ ->RemoveTimer(timer_);
    }
    return 0;
}

string OsTimer::threadName()
{
    char buf[256];
    if (OsGetTimeString(buf, 256) == false) {
        buf[0] = '\0';
    }
    return string("ZsTimerSchedulerThread-")+ buf;
}

int OsFsTraverser::traverse() {
  FTS *tree = NULL;
  char *fts_path[2] = { const_cast<char*>(root.c_str()), NULL };
  tree = fts_open(fts_path, FTS_NOCHDIR | FTS_PHYSICAL |
                  FTS_NOSTAT, NULL);
  if (tree == NULL) {
    return -1;
  }
  FTSENT *node;
  while ((node = fts_read(tree))) {
    if (node->fts_level == 0) {
      continue;
    }
    if (node->fts_info != FTS_D && node->fts_info != FTS_NSOK) {
      continue;
    }
    if (visitor->IsIgnored(node->fts_path)) {
      fts_set(tree, node, FTS_SKIP);
      continue;
    }
    OsFileStat file_stat;
    int ret = OsStat(node->fts_path, string(), &file_stat);
    if (ret == 0) {
      if (visitor->Visit(file_stat) < 0) {
        return -1;
      }
    }
  }

  return 0;
}

int OsFile::Open(
	const string &path, const std::string&, const char *mode) {
		fp_ = OsFopen(path.c_str(), mode);
		return (fp_ == NULL) ? -1 : 0;
}

size_t OsFile::Read(std::string *buffer) {
	assert(fp_ != NULL);
	void* ptr = &(*buffer->begin());
	size_t nmemb = buffer->size();
	size_t nbytes = fread(ptr, 1, nmemb, fp_);
	if (nbytes != buffer->size()) {
		buffer->resize(nbytes);
	}
	return nbytes;
}

size_t OsFile::Read(char *buf, size_t length) {
	assert(fp_ != NULL);
	size_t nbytes = fread(buf, 1, length, fp_);
	return nbytes;
}

size_t OsFile::Write(const std::string &buffer) {
	assert(fp_ != NULL);
	size_t nbytes = fwrite(&(*buffer.begin()), 1, buffer.size(), fp_);
	return nbytes;
}

size_t OsFile::Write(const char *buffer, size_t length) {
	assert(fp_ != NULL);
	size_t nbytes = fwrite(buffer, 1, length, fp_);
	return nbytes;
}

size_t OsFile::ReadWholeFile(std::string* buffer) {
	assert(fp_ != NULL);
	if (fp_ == NULL) {
		return -1;
	}

	int ret = fseek(fp_, 0, SEEK_END);
	assert(ret != -1);
	if (ret == -1) {
		return -1;
	}

	size_t offset = static_cast<size_t>(ftell(fp_));

	ret = fseek(fp_, 0, SEEK_SET);
	assert(ret != -1);

	buffer->resize(offset);
	size_t nbytes = fread(&*buffer->begin(), offset, 1, fp_);

	return nbytes * offset;
}

#if TARGET_OS_IPHONE
int OsStatAsset(ZiGroup *group, ZiAsset *asset, OsFileStat* file_stat) {
    const char *name = [[asset url] absoluteString].UTF8String;
    if (name == NULL || strlen(name) == 0) {
        return -1;
    }
    string assetUrl = name;
    name = [[group url] absoluteString].UTF8String;
    if (name == NULL || strlen(name) == 0) {
        return -1;
    }
    string groupUrl = name;
    
    file_stat ->path = AssetUrlParser::AssetsRoot;
    file_stat ->path += "/";
    file_stat ->path += [[group groupName] UTF8String];
    file_stat ->path += "/";
    file_stat ->path += [[asset assetName] UTF8String];
    file_stat ->alias = assetUrl + AssetUrlParser::SeparatorString +
    [asset assetName].UTF8String + AssetUrlParser::SeparatorString +
    groupUrl + AssetUrlParser::SeparatorString +
    [group groupName].UTF8String;
    file_stat ->length = [asset length];
    return 0;
}
#endif
  
//bool shouldSkipGroup(const char *group_name) {
//    static const char *AssetsGroupNamesToSkip[] = {"Last Import", "Last 12 Months"};
//    for(int i = 0; i < sizeof(AssetsGroupNamesToSkip) / sizeof(const char*); i++) {
//        if(strcmp(group_name, AssetsGroupNamesToSkip[i]) == 0) {
//            return true;
//        }
//    }
//    return false;
//}
    
//int OsFsTraverser::traverse() {
//#if TARGET_OS_IPHONE
//    if (m_is_asset) {
//        return traverseAsset();
//    }else{
//        return traverseFileSystem();
//    }
//#else
//  traverseFileSystem();
//#endif
//}

//OsFsTraverser::OsFsTraverser(const string& root_, IFsVisitor *visitor):root(root_),
//visitor(visitor){
//#if TARGET_OS_IPHONE
//    m_is_asset = false;
//    if (root_ == "" || strstr(root_.c_str(), AssetUrlParser::AssetsRoot) == root_.c_str()) {
//        m_is_asset = true;
//        m_condition_assets = [[NSCondition alloc] init];
//        m_condition_group = [[NSCondition alloc] init];
//    }
//#endif
//}

//int OsStat(const std::string& path, OsFileStat* file_stat);
//int OsFsTraverser::traverseFileSystem() {
//    FTS *tree = NULL;
//    char *fts_path[2] = { const_cast<char*>(root.c_str()), NULL };
//    tree = fts_open(fts_path, FTS_NOCHDIR | FTS_PHYSICAL |
//                    FTS_NOSTAT, NULL);
//    if (tree == NULL) {
//        return -1;
//    }
//    FTSENT *node;
//    while ((node = fts_read(tree))) {
//        if (node->fts_level == 0) {
//            continue;
//        }
//        if (node->fts_info != FTS_D && node->fts_info != FTS_NSOK) {
//            continue;
//        }
//        if (visitor->IsIgnored(node->fts_path)) {
//            fts_set(tree, node, FTS_SKIP);
//            continue;
//        }
//        OsFileStat file_stat;
//        int ret = OsStat(node->fts_path, &file_stat);
//        if (ret == 0) {
//            if (visitor->Visit(file_stat) < 0) {
//                return -1;
//            }
//        }
//    }
//    
//    return 0;
//}
//
#if TARGET_OS_IPHONE
void OsFsTraverser::enumerateGroup(ZiGroup *group){
    
    if ([group numberOfAssets] > 0) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [group enumerateAssetsUsingBlock:^(ZiAsset *asset) {
             if(asset) {
             OsFileStat stat_asset;
             if(OsStatAsset(group, asset, &stat_asset) != -1) {
                visitor ->Visit(stat_asset);
             }
             
             }

             if(asset == nil) {
             [m_condition_assets signal];
             }

             }];
        });
        [m_condition_assets lock];
        [m_condition_assets wait];
        [m_condition_assets unlock];
    }
    
}

    int OsFsTraverser::traverseAsset() {
        m_asset_groups = [NSMutableDictionary dictionary];
        __block int ret = -1;
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [[ZiAssetsLibrary sharedZiAssetsLibrary] enumerateGroupsWithTypes:ALAssetsGroupAlbum | ALAssetsGroupSavedPhotos
                                                                  resultBlock:^(ZiGroup *group) {
             if(group == nil){
                 [m_condition_group signal];
                 ret = 0;
             }else{
                 const char *group_name = [[group groupName] UTF8String];
                 if(!shouldSkipGroup(group_name)) {
                    enumerateGroup(group);
                 }
            }
             }
                                                                      failure:^(NSError *error) {
             [m_condition_group signal];
             ret = -1;
             }];
        });
        [m_condition_group lock];
        [m_condition_group wait];
        [m_condition_group unlock];
        
        return ret;
    }
  
#endif
    

bool OsTempPath(const string& dir, const string& prefix, string* tmp_path) {
  std::string fullpath = dir + "/" + prefix + "XXXXXX";
  char *tmp_path_ = mktemp(const_cast<char*>(fullpath.data()));
  if (tmp_path == NULL) {
    return false;
  }
  tmp_path->assign(tmp_path_);
//  free(tmp_path_);
  return true;
}

#if TARGET_OS_IPHONE
void getAssetAndContainingGroup(const std::string &path, ZiAsset **asset, ZiGroup **group) {
    AssetUrlParser parser(path);
    NSString *asset_url_string = [NSString stringWithUTF8String:parser.AssetUrl().c_str()];
    NSURL *asset_url = [NSURL URLWithString:asset_url_string];
    NSString *group_url_string = [NSString stringWithUTF8String:parser.GroupUrl().c_str()];
    NSURL *group_url = [NSURL URLWithString:group_url_string];
    
    NSCondition *cond = [[NSCondition alloc] init];
    __block ZiAsset *asset_ = nil;
    __block ZiGroup *group_ = nil;
    __block int count_down = 2;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        [[ZiAssetsLibrary sharedZiAssetsLibrary] assetForUrl:asset_url
                              result:^(ZiAsset *asset) {
             asset_ = asset;
             [cond lock];
             --count_down;
             [cond signal];
             [cond unlock];
         } failure:^(NSError *error) {
             [cond lock];
             --count_down;
             [cond signal];
             [cond unlock];
         }];
    });
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        [[ZiAssetsLibrary sharedZiAssetsLibrary] groupForUrl:group_url
         result:^(ZiGroup *group) {
             group_ = group;
             [cond lock];
             --count_down;
             [cond signal];
             [cond unlock];
         } failure:^(NSError *error) {
             group_ = nil;
             [cond lock];
             --count_down;
             [cond signal];
             [cond unlock];
         }];
    });
    
    [cond lock];
    while (count_down > 0) {
        [cond wait];
    }
    [cond unlock];
    
    *asset = asset_;
    *group = group_;
}
#endif
  
//  int OsStat(const std::string& path, OsFileStat* file_stat);
// int OsStat(const std::string& path, const std::string &alias, OsFileStat* file_stat) {
//     AssetUrlParser parser;
//     parser.SetUrl(alias);
//   
//#if TARGET_OS_IPHONE
//   if (parser.IsAsset()) {
//     
//     if (OsFile::isFsFile(path)) {
//         string path_ = OsFile::pathForMetaFile(path);
//         return OsStat(path_, file_stat);
//     }
//
//     ZiAsset *asset = nil;
//     ZiGroup *group = nil;
//     getAssetAndContainingGroup(alias, &asset, &group);
//     if (asset == nil || group == nil) {
//         return ENOENT;
//     }else{
//         file_stat ->path = path;
//         file_stat ->length = [asset length];
//         file_stat ->alias = alias;
//         //file_stat ->alias = parser.AssetUrl() + AssetUrlParser::SeparatorString;
//     }
//     return 0;
//   }
//#endif
//     return OsStat(path, file_stat);
//
// }

int OsStat(const std::string& path, const string&, OsFileStat* file_stat) {
    struct stat stat_buf;
    int ret = stat(path.c_str(), &stat_buf);

    if (ret == -1) {
        if (errno == ENOENT) {
            return errno;
        } else {
            return ret;
        }
    }

    if (S_ISDIR(stat_buf.st_mode)) {
        file_stat->type = OS_FILE_TYPE_DIR;
    } else if (S_ISREG(stat_buf.st_mode)){
        file_stat->type = OS_FILE_TYPE_REG;
    } else {
        return ENOENT;
    }
    file_stat->path = path;
    file_stat->mtime = static_cast<int64_t>(stat_buf.st_mtimespec.tv_sec)
    * 1000 + stat_buf.st_mtimespec.tv_nsec / 1000000;
    file_stat->length = stat_buf.st_size;
    file_stat->attr = stat_buf.st_mode & 07777;
    return 0;
}
    
int32_t OsGetRandomTcpPort() {
  int s_fd;
  struct sockaddr_in addr;
  s_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(s_fd == -1){
    return -1;
  }
  // int optval = 1;
  // if (setsockopt(s_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
  //   return -1;
  // }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if(::bind(s_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(s_fd);
    return -1;
  }
  socklen_t addr_len = sizeof(addr);
  if(getsockname(s_fd, (struct sockaddr *)&addr, &addr_len) == -1){
    close(s_fd);
    return -1;
  }

  close(s_fd);
  return ntohs(addr.sin_port); 
}

int SockAddrToString(struct sockaddr_in* sockaddr, std::string* host) {
	assert(sockaddr->sin_family == AF_INET);

	char addr[INET_ADDRSTRLEN] = {0};
	const char *ret = inet_ntop(
		AF_INET, &sockaddr->sin_addr, addr, INET_ADDRSTRLEN);
	assert(ret != NULL);

	host->assign(ret);

	return 0;
}

int OsAddHiddenAttr(const char *path) {
  return 0;
}

OsSocketAddress::OsSocketAddress(const std::string& uri)
  : uri_(uri), result_(NULL), ptr_(NULL) {
    std::string schema, host, port;

    std::string::size_type found1 = uri_.find_first_of("://");
    assert(found1 != std::string::npos);
    if (found1 != std::string::npos) {
      schema = uri_.substr(0, found1);
      found1 += 3;
    } else {
      found1 = 0;
    }

    std::string::size_type found2 = uri_.find_last_of(':');
    assert (found2 != std::string::npos);
    host = uri_.substr(found1, found2 - found1);
    port = uri_.substr(found2 + 1);

    int ret;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (schema == "tcp" || schema == "http") {
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
    } else {
      hints.ai_socktype = SOCK_DGRAM;
      hints.ai_protocol = IPPROTO_UDP;
    }

    ret = getaddrinfo(host.data(), port.data(), &hints, &result_);
    if (ret != 0) {
      return;
    } else {
      ptr_ = result_;
    }
}

struct sockaddr* OsSocketAddress::NextSockAddr() {
  struct sockaddr* addr = ptr_ ? ptr_->ai_addr : NULL;
  if (ptr_ != NULL) {
    ptr_ = ptr_->ai_next;
  }

  return addr;
}

int ListIpAddresses(
  std::vector<struct sockaddr_in>* ipv4,
  std::vector<struct sockaddr_in6>* ipv6) {
    typedef unsigned char BYTE;
    typedef unsigned short WORD;
    struct ifaddrs * ifaddr_struct = NULL;

    if (getifaddrs(&ifaddr_struct) == -1) {
      return -1;
    }

    while (ifaddr_struct != NULL) {
      if (strcmp(ifaddr_struct->ifa_name, "lo") == 0 ||
          ifaddr_struct->ifa_addr == NULL) {
      } else if (ifaddr_struct->ifa_addr->sa_family == AF_INET) { 
        // check it is IP4 is a valid IP4 Address
        struct sockaddr_in* sin = 
          reinterpret_cast<struct sockaddr_in*>(ifaddr_struct->ifa_addr);
        const BYTE& b0 = *(BYTE*)(&sin->sin_addr.s_addr);
        const BYTE& b1 = *(BYTE*)((BYTE*)(&sin->sin_addr.s_addr) + 1);
        if (!(b0 == 169 && b1 == 254)) {
          // skip 169.254.X.X
          ipv4->emplace_back(*sin);
        }
      } else if (ifaddr_struct->ifa_addr->sa_family == AF_INET6) { // check it is IP6
        // is a valid IP6 Address
        struct sockaddr_in6* sin6 = 
          reinterpret_cast<struct sockaddr_in6*>(ifaddr_struct->ifa_addr);
        const BYTE& b0 = *(BYTE*)(&sin6->sin6_addr.s6_addr);
        const BYTE& b1 = *(BYTE*)((BYTE*)(&sin6->sin6_addr.s6_addr) + 1);
        const WORD& w0 = *(WORD*)(&sin6->sin6_addr.s6_addr);
        const WORD& w1 = *(WORD*)((BYTE*)(&sin6->sin6_addr.s6_addr) + 2);

        bool is_link_local = false;
        bool is_special_use = false;

        if (b0 == 0xFE && ((b1 & 0xF0) >= 0x80 && (b1 & 0xF0) <= 0xb0)) { 
          is_link_local = true;
        } else if (w0 == 0x0120 && w1 == 0) {
          is_special_use = true;
        }

        if (!  (is_link_local || is_special_use)) {
          ipv6->emplace_back(*sin6);
        }
      }
      ifaddr_struct = ifaddr_struct->ifa_next;
    }

    return 0;
}

std::string OsDirName(const std::string& path) {
  assert(path.find('\\') == std::string::npos);
  if (path.empty()) {
    return ".";
  }

  std::size_t pos = path.find_last_not_of('/');
  if (pos == std::string::npos) {
    return "/";  // format://////
  }
  pos = path.find_last_of('/', pos);
  if (pos == std::string::npos) {
    if (path.size() > 2 && path.substr(1, 3) == ":/") {
      return path.substr(0, 3);  // format: c:///
    } else if (path.size() == 2 && path.at(1) == ':') {
      return path;  // format: c:
    }else {
      return ".";  // format: usr
    }   
  }

  if (pos == 0) {
    return "/";  // format: /usr///
  }

  return path.substr(0, pos);
}

int OsEvent::Initialize(bool signaled) {
  int ret = pthread_mutex_init(&mutex_, NULL);
  if (ret != 0) {
      assert(0);
      return ret;
  }
  
  ret = pthread_cond_init(&cond_, 0);
  if (ret != 0) {
    assert(0);
    return ret;
  }

  if (signaled == true) {
    ret = Signal();
    assert(ret == 0);
  } else {
    ret = Reset();
    assert(ret == 0);
  }

  return ret;
}

int OsEvent::CleanUp() {
  int ret = pthread_cond_destroy(&cond_);
  if (ret != 0) {
    assert(0);
    return ret;
  }

  ret = pthread_mutex_destroy(&mutex_);
  assert(ret == 0);

  return ret;
}

int OsEvent::Reset() {
  int ret = pthread_mutex_lock(&mutex_);
  if (ret != 0) {
    assert(0);
    return ret;
  }

  signaled_ = false;

  ret = pthread_mutex_unlock(&mutex_);
  assert(ret == 0);

  return ret;
}

int OsEvent::Signal() {
  int ret = pthread_mutex_lock(&mutex_);
  if (ret != 0) {
    assert(0);
    return ret;
  }

  signaled_ = true;

  ret = pthread_mutex_unlock(&mutex_);
  if (ret != 0) {
    assert(0);
    return ret;
  }

  ret = pthread_cond_broadcast(&cond_);
  assert(ret == 0);

  return ret;
}

int OsEvent::Wait() {
  int ret = pthread_mutex_lock(&mutex_);
  if (ret != 0) {
    assert(0);
    return ret;
  }

  while (!signaled_) {
    ret = pthread_cond_wait(&cond_, &mutex_);
    assert(ret == 0);
  } 

  ret = pthread_mutex_unlock(&mutex_);
  assert(ret == 0);

  return ret;
}



//const char * OsFile::ModeRead = "rb";
//const char *OsFile::ModeWrite = "wb";
//
//static void handleAssetsLibraryChange(CFNotificationCenterRef center,
//                                     void *observer,
//                                     CFStringRef name,
//                                     const void *object,
//                                     CFDictionaryRef userInfo) {
//    OsFile* file = (OsFile*)observer;
//    if (userInfo == nil) {
//        file ->reset();
//        return;
//    }
//    if (CFDictionaryGetCount(userInfo) == 0) {
//        return;
//    }
//    NSSet *updatedAssets = (NSSet*)CFDictionaryGetValue(userInfo, (CFStringRef)ALAssetLibraryUpdatedAssetsKey);
//    if ([updatedAssets member:[file ->m_asset url]]) {
//        file ->reset();
//    }
//}
//    bool OsFile::isFsFile(const string &meta_path) {
//        const char *cpath = meta_path.c_str();
//        const char *suffix = ".zisync.meta";
//        const char *psuffix = strstr(cpath, suffix);
//        if (cpath && strstr(cpath, AssetUrlParser::AssetsRoot) == cpath && psuffix && strcmp(suffix, psuffix) == 0) {
//            return true;
//        }else {
//            return false;
//        }
//    }
//    
//    std::string OsFile::pathForMetaFile(const string &meta_path) {
//        char buf[1024];
//        [ZiKernel getAppTmpPathRoot:buf];
//        std::string fullPath = buf;
//        if (fullPath.at(fullPath.size()-1) == '/') {
//            fullPath.pop_back();
//        }
//        fullPath += meta_path;
//        return fullPath;
//    }
//    
//OsFile::OsFile():{
//  m_asset(nil), m_group(nil), m_rd_offset(0), m_failed(false), ifile(NULL), ofile(NULL)
//    m_condition = [[NSCondition alloc] init];
//}
//    
//int OsFile::Open(const std::string &path, const std::string &alias, const char *mode) {
//    assert(strcmp(mode, OsFile::ModeRead) == 0 || strcmp(mode, OsFile::ModeWrite) == 0);
//    if (isFsFile(path)) {
//        string path_  = pathForMetaFile(path);
//        NSString *ocpath = [[NSString stringWithUTF8String:path_.c_str()] stringByDeletingLastPathComponent];
//        NSFileManager *fm = [NSFileManager defaultManager];
//        if (![fm fileExistsAtPath:ocpath]) {
//            [fm createDirectoryAtPath:ocpath withIntermediateDirectories:YES attributes:nil error:nil];
//        }
//        m_mode = mode;
//        m_video_cache_path = path;
//        if (strcmp(mode, "wb") == 0) {
//            ofile = new ofstream;
//            ofile ->open(pathForMetaFile(path));
//            assert(ofile);
//            NSLog(@"opened for mode %s: %s\n\n\n\n", mode, pathForMetaFile(path).c_str());
//        }else if(strcmp(mode, "rb") == 0){
//            ifile = new ifstream;
//            ifile ->open(pathForMetaFile(path));
//            assert(ifile);
//            NSLog(@"opened for mode %s: %s\n\n\n\n", mode, pathForMetaFile(path).c_str());
//        }else {
//            NSLog(@"what is the mode: %s\n\n\n\n", mode);
//        }
//        return 0;
//    }
//    
//    m_mode = mode;
//    m_url_parser.SetUrl(alias);
//    NSURL *url_asset = [NSURL URLWithString:
//                  [NSString stringWithUTF8String:m_url_parser.AssetUrl().c_str()]
//                  ];
//    NSURL *url_group = [NSURL URLWithString:
//                        [NSString stringWithUTF8String:m_url_parser.GroupUrl().c_str()]
//                        ];
//    __block int ret = 0;
//    ZiAsset *asset = nil;
//    ZiGroup *group = nil;
//    getAssetAndContainingGroup(alias, &asset, &group);
//    m_asset = asset;
//    m_group = group;
//    if(asset == nil && strcmp(mode, OsFile::ModeRead)){
//        m_failed = true;
//        return -1;//in read mode but no asset to read, all other cases are ok
//    }
//    m_rd_offset = 0;
//    return ret;
//}
//
//std::string OsFile::assetType() {
//    if (m_asset) {
//        return [m_asset assetType].UTF8String;
//    }else{
//        return m_url_parser.AssetType();
//    }
//}
//
//size_t OsFile::Read(std::string *buffer) {
//    size_t ret = Read(const_cast<char*>(buffer ->data()), buffer ->size());
//    if (buffer ->size() < buffer ->capacity()) {
//        buffer ->resize(buffer ->size());
//    }
//    return ret;
//}
//
//size_t OsFile::Read(char *buf, size_t length) {
//    if (isFsFile(m_video_cache_path)) {
//        assert(ifile != NULL);
//        ifile ->read(buf, length);
//        return ifile ->gcount();
//    }
//    int ret = 0;
//    NSError *error = nil;
//    ret = (int)[m_asset getBytes:(uint8_t*)buf
//                          length:length
//                          offset:m_rd_offset
//                           error:&error];
//    m_rd_offset += ret;
//    if (error != nil) {
//        ZSLOG_ERROR("OsFile::Read error: %s", error.description.UTF8String);
//        m_failed = true;
//        return -1;
//    }
//    return ret;
//}
//
//int OsFile::eof() {
//    if (isFsFile(m_video_cache_path)) {
//        if(ifile) {
//            return ifile ->eof();
//        }else if(ofile) {
//            return ofile ->eof();
//        }else {
//            assert(false);
//        }
//    }
//    return m_rd_offset == [m_asset length] ? 1 : 0;
//}
//
//size_t OsFile::Write(const std::string &buffer) {
//    if (isFsFile(m_video_cache_path)) {
//        assert(ofile);
//        ofile ->write(buffer.c_str(), buffer.size());
//        ofile ->flush();
//        return buffer.size();
//    }
//    return Write(buffer.data(), buffer.size());
//}
//
//size_t OsFile::Write(const char *buffer, size_t length) {
//    if (assetType() == ALAssetTypePhoto.UTF8String) {
//        m_write_buf.append(buffer, length);
//        return 0;
//    }else{
//        return appendToFile(buffer, length);
//    }
//}
//
//int OsFile::appendToFile(const char *buffer, size_t length) {
//    if (m_video_cache_path.size() == 0) {
//        NSString *cache_root = [[ZiKernel getAppTmpPathRoot]
//                                stringByAppendingPathComponent:@"back-up-cache"];
//        if (OsCreateDirectory(cache_root.UTF8String, false) == -1) {
//            return -1;
//        }
//        bool tmp_valide = OsTempPath(cache_root.UTF8String,
//                   [m_asset assetName].UTF8String,
//                   &m_video_cache_path);
//        if(!tmp_valide) {
//            m_failed = true;
//            return -1;
//        }
//    }
//    if (length > 0) {
//        ofstream out;
//        out.open(m_video_cache_path.c_str(), ios::app | ios::binary);
//        out.write(buffer, length);//todo: exceptions are not handled
//        out.close();
//    }
//    return 0;
//}
//    
//size_t OsFile::ReadWholeFile(std::string *buffer) {
//    NSError *error = nil;
//    long long totalBytes = [m_asset length];
//    size_t ret = (size_t)[m_asset getBytes:(uint8_t *)buffer->data()
//                              length:(NSInteger)totalBytes
//                              offset:0
//                               error:&error];
//    if (error != nil) {
//        m_failed = true;
//        ZSLOG_ERROR("%s", error.description.UTF8String);
//        return 0;
//    }
//    return ret;
//}
//
//int OsFile::Close() {
//    if (isFsFile(m_video_cache_path)) {
//        if(strcmp(m_mode.c_str(), "rb") == 0) {
//            if(ifile) {
//                ifile ->close();
//                delete ifile;
//                NSLog(@"closed for mode %s: %s\n\n\n\n", m_mode.c_str(), pathForMetaFile(m_video_cache_path).c_str());
//            }
//            ifile = NULL;
//        }else if(ofile) {
//            if(ofile) {
//                ofile ->close();
//                delete ofile;
//                NSLog(@"closed for mode %s: %s\n\n\n\n", m_mode.c_str(), pathForMetaFile(m_video_cache_path).c_str());
//            }
//            ofile = NULL;
//        }else {
//            assert(false);
//        }
//        reset();
//        return 0;
//    }
//#if TARGET_OS_IPHONE
//    if (m_failed) {
//        return -1;
//    }
//    if (strcmp(m_mode.c_str(), OsFile::ModeWrite) == 0) {
//        if (m_video_cache_path.size() != 0 || m_write_buf.size() != 0) {
//            writeIntoAssetLibraryComplete(^{[m_condition signal];});
//            [m_condition lock];
//            [m_condition wait];
//            [m_condition unlock];
//        }
//    }
//#endif
//  
//    reset();
//    CFNotificationCenterRemoveObserver(CFNotificationCenterGetLocalCenter(), (void*)this, nil, nil);
//    return 0;
//}
//
//#if TARGET_OS_IPHONE
//int OsFile::writeIntoAssetLibraryComplete(dispatch_block_t complete) {
//    NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:m_video_cache_path.c_str()]];
//    assert(m_write_buf.size() > 0 || m_video_cache_path.size() > 0);
//    LoadGroup();
//    if (assetType() == ALAssetTypePhoto.UTF8String) {
//        NSData *data = [NSData dataWithBytes:(const void *)m_write_buf.data()
//                                      length:m_write_buf.size()];
//        if (!m_asset) {
//            [[ZiAssetsLibrary sharedZiAssetsLibrary] writeImageDataToSavedPhotosAlbum:data
//                                                     metadata:nil
//                                              completionBlock:^(NSURL *assetURL, NSError *error) {
//                 if(error)logError(assetURL.absoluteString, @"recreate photo", error);
//             }];
//        }else if (![m_asset editable]) {
//            [m_asset writeModifiedImageDataToSavedPhotosAlbum:data
//                                                     metadata:nil
//                                              completionBlock:^(NSURL *assetURL, NSError *error) {
//                 if(error)logError(assetURL.absoluteString, @"duplicate photo", error);
//                 if(complete)complete();
//             }];
//        }else{
////since m_asset exist, this logic branch is to write the metadata back into asset-library
////but currently no method of get that metadata
//
////            [m_asset setImageData:data
////                         metadata:nil
////                  completionBlock:^(NSURL *assetURL, NSError *error) {
////                 if(error)logError(assetURL.absoluteString, @"Modify photo", error);
////             }];
//        }
//        
//    }else if(assetType() == ALAssetTypeVideo.UTF8String){
//        if (!m_asset) {
//            [[ZiAssetsLibrary sharedZiAssetsLibrary] writeVideoAtPathToSavedPhotosAlbum:[NSURL URLWithString:[NSString stringWithUTF8String:m_video_cache_path.c_str()]]
//                                                completionBlock:^(NSURL *assetURL, NSError *error) {
//                 if(error)logError(assetURL.absoluteString, @"recreate video", error);
//             }];
//        }else if (![m_asset editable]) {
//            [m_asset writeModifiedVideoAtPathToSavedPhotosAlbum:url
//                                                completionBlock:^(NSURL *assetURL, NSError *error) {
//                 if(error)logError(assetURL.absoluteString, @"duplicate video", error);
//                 if(complete)complete();
//             }];
//        }else{
//            ;
//        }
//    }
//    
//    [m_group addAsset:m_asset];
//
//}
//    
//void OsFile::CreateGroup() {
//    NSString *groupName = [NSString stringWithUTF8String:m_url_parser.GroupName().c_str()];
//    [[ZiAssetsLibrary sharedZiAssetsLibrary] addAssetsGroupAlbumWithName:groupName
//                                     resultBlock:^(ALAssetsGroup *group) {
//     m_group = [[ZiGroup alloc] initWithAlAssetsGroup:group];
//     [m_condition signal];
//     } failureBlock:^(NSError *error) {
//     [m_condition signal];
//     }];
//    [m_condition lock];
//    [m_condition wait];
//    [m_condition unlock];
//}
//
//void OsFile::LoadGroup() {
//    NSString *groupUrlString = [NSString stringWithUTF8String:m_url_parser.GroupUrl().c_str()];
//    NSURL *groupUrl = [NSURL URLWithString:groupUrlString];
//    
//    [[ZiAssetsLibrary sharedZiAssetsLibrary] groupForUrl:groupUrl
//                     result:^(ZiGroup *group) {
//     m_group = group;
//     [m_condition signal];
//     } failure:^(NSError *error) {
//     CreateGroup();
//     [m_condition signal];
//     }];
//    [m_condition lock];
//    [m_condition wait];
//    [m_condition unlock];
//}
//
//void OsFile::logError(NSString *info, NSString *action, NSError *error) {
//    if(error){
//        ZSLOG_ERROR("%s(which now is: %s) %s error: %s", [m_asset assetName].UTF8String, info.UTF8String, action.UTF8String, error.description.UTF8String);
//    }else{
//        ZSLOG_INFO("%s(which now is: %s) %s done.", [m_asset assetName].UTF8String, info.UTF8String, action.UTF8String);
//    }
//}
//  
//#endif
//  
//void OsFile::reset() {
//#if TARGET_OS_IPHONE
//    m_failed = false;
//    m_asset = nil;
//    if (OsExists(m_video_cache_path)) {
//        OsDeleteFile(m_video_cache_path, false);
//    }
//    m_rd_offset = 0;
//    m_write_buf.clear();
//    m_video_cache_path.clear();
//#endif
//}
//
//const char *AssetUrlParser::SeparatorString = "___ZiSyncIOS___";
//
//// if you modify this const, should also modify the same value
//// sync_tree_agent.cc: SyncTreeAgent::GetAlias 
//const char *AssetUrlParser::AssetsRoot = "/assets-library";
////composed url string: "assetUrl + SeparatorString + assetName + SeparatorString + groupUrl + SeparatorString + groupName"
//string AssetUrlParser::composeUrlWith(const string &assetUrl, const string &groupUrl) {
//    return assetUrl + SeparatorString + SeparatorString + groupUrl + SeparatorString + SeparatorString + SeparatorString ;
//}
//    
//AssetUrlParser::AssetUrlParser(const string &url) {
//    SetUrl(url);
//}
//    
//void AssetUrlParser::SetUrl(const string &url){
//    const size_t Len = url.size();
//    const size_t SepLen = strlen(SeparatorString);
//    
//    m_asset_url = GetSegment(0, url);
//    m_asset_name = GetSegment(1, url);
//    m_group_url = GetSegment(2, url);
//    m_group_name = GetSegment(3, url);
//    m_asset_type = GetSegment(4, url);
//    m_is_asset = true;
//    if (m_asset_url.find(AssetLibrarySchemeAsset) != 0) {
//        m_is_asset = false;
//    }
//    if (m_group_url.find(AssetLibrarySchemeGroup) != 0) {
//        m_is_asset = false;
//    }
//    if (m_asset_name.size() == 0) {
//        m_is_asset = false;
//    }
//    if (m_group_name.size() == 0) {
//        m_is_asset = false;
//    }
//    if (m_group_name.size() == 0) {
//        m_is_asset = false;
//    }
//    assert(m_is_asset);
//}
//
//string AssetUrlParser::GetSegment(int index, const string &str) {
//    size_t i = 0;
//    for (int k = 0; k < index; k++) {
//        i = str.find(SeparatorString, i);
//        if (i == string::npos) {
//            return "";
//        }else{
//            i += strlen(SeparatorString);
//        }
//    }
//    if (i >= str.size()) {
//        return "";
//    }
//    size_t j = str.find(SeparatorString, i);
//    if (j == string::npos) {
//        j = str.size();
//    }
//    return str.substr(i, j-i);
//}
//
//string AssetUrlParser::GroupUrl() {
//    return m_group_url;
//}
//
//string AssetUrlParser::AssetUrl() {
//    return m_asset_url;
//}
//
//string AssetUrlParser::AssetName() {
//    return m_asset_name;
//}
//
//string AssetUrlParser::GroupName() {
//    return m_group_name;
//}
//
//string AssetUrlParser::AssetType() {
//    return m_asset_type;
//}
  
  static void RunLoopSourceFiredCallback(void *info) {
    RunLoopSource *source = (RunLoopSource*)info;
    source->mutex_sync_.AquireMutex();
    source->HandleRequests();
    source->mutex_sync_.ReleaseMutex();
  }
  
  void RunLoopSource::HandleRequests() {
    auto it = requests_.begin();
    auto nd = requests_.end();
    std::function<RunLoopCallbackFunctor_t> functor;
    void *data;
    OsEvent *wait;
    for(; it != nd; ++it) {
      std::tie(functor, data, wait) = *it;
      functor(data);
      wait->Signal();
    }
    requests_.clear();
  }
  
  static void RunLoopDidScheduleCallback(void *info
                                         , CFRunLoopRef runloop
                                         , CFStringRef mode) {
    RunLoopSource *source = (RunLoopSource*)info;
    source->is_valid_ = true;
  }
  
  static void RunLoopDidRemoveCallback(void *info
                                       , CFRunLoopRef runloop
                                       , CFStringRef mdoe) {
    
    RunLoopSource *source = (RunLoopSource*)info;
    source->is_valid_ = false;
  }
  
  void RunLoopSource::Initialize() {
    
    CFRunLoopSourceContext    context = {0, this, NULL, NULL, NULL, NULL, NULL,
      RunLoopDidScheduleCallback,
      RunLoopDidRemoveCallback,
      RunLoopSourceFiredCallback
      };
    
    runloop_source_ = CFRunLoopSourceCreate(NULL, 0, &context);
  }
  
  void RunLoopSource::AddToRunloop() {
    runloop_ = CFRunLoopGetCurrent();
    CFRunLoopAddSource(runloop_
                       , runloop_source_
                       , kCFRunLoopDefaultMode);
  }
  
  void RunLoopSource::SignalSourceWithFunctorAndData(
                                                     std::function<RunLoopCallbackFunctor_t> functor
                                                     , void *data) {
    OsEvent *wait = new OsEvent;
    wait->Initialize(false);
    {
      MutexAuto mutex_auto(&mutex_sync_);
      if (!is_valid_) {
        wait->CleanUp();
        delete wait;
        return;
      }
      requests_.push_back(MakeRequest(functor, data, wait));
      
      assert(runloop_source_);
      CFRunLoopSourceSignal(runloop_source_);
      if (CFRunLoopIsWaiting(RunLoop())) {
        CFRunLoopWakeUp(RunLoop());
      }
    }
    wait->Wait();
    wait->CleanUp();
    delete wait;
  }
  
  
  void RunLoopSource::Invalidate() {
    std::function<RunLoopCallbackFunctor_t> functor =
    std::bind(&RunLoopSource::InvalidateIntern, this, _1);
    SignalSourceWithFunctorAndData(functor, NULL);
  }
  
  void RunLoopSource::InvalidateIntern(void *nouse) {
    is_valid_ = false;
    CFRunLoopSourceInvalidate(runloop_source_);
  }
  
  
  
}  // namespace zs
