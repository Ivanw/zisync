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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>
#include <uuid/uuid.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <fts.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <stdlib.h>
#include <libgen.h>
#include <vector>
#include <dirent.h>
#include "zisync/kernel/zslog.h"
#if defined (__ANDROID__)
#include "ifaddrs.c"
#else
#include <ifaddrs.h>
#endif

#ifndef HOST_NAME_MAX
#ifdef MAXHOSTNAMELEN
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 256
#endif
#endif

#include "zisync/kernel/platform/platform_linux.h"
#include "zisync/kernel/platform/os_file.cc"

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

bool OsExists(const char* path) {
  if (access(path, F_OK) != -1) {
    return true;
  } else {
    return false;
  }
}

bool OsFileExists(const char *path) {
  struct stat64 stat_;
  if (stat64(path, &stat_) != 0) {
    return false;
  }
  if (S_ISREG(stat_.st_mode)) {
    return true;
  } else {
    return false;
  }
}

bool OsDirExists(const char *path) {
  struct stat64 stat_;
  if (stat64(path, &stat_) != 0) {
    return false;
  }
  if (S_ISDIR(stat_.st_mode)) {
    return true;
  } else {
    return false;
  }
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
  struct stat64 statbuf;
  struct timeval times[2];

  if (stat64(path, &statbuf) < 0) {
    return -1;
  }

#if defined(__ANDROID__) 
  times[0].tv_sec = statbuf.st_atime;
  times[0].tv_usec = statbuf.st_atime_nsec / 1000;
#else
  times[0].tv_sec = statbuf.st_atim.tv_sec;
  times[0].tv_usec = statbuf.st_atim.tv_nsec / 1000;
#endif

  times[1].tv_sec = mtime_in_ms / 1000;
  times[1].tv_usec = 
      (mtime_in_ms - static_cast<int64_t>(times[1].tv_sec) * 1000) * 1000;

#if defined(__ANDROID__) 
  if (utimes(path, times) < 0 && errno != EPERM) {
    return -1;
  }
#else
  if (utimes(path, times) < 0) {
    return -1;
  }
#endif

  return 0;
}

int OsChmod(const char *path, int32_t attr) {
#if defined(__ANDROID__) 
  int ret = chmod(path, attr);
  if (ret != 0 && errno != EPERM) {
    return -1;
  }
  return 0;
#else
  return chmod(path, attr);
#endif
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

FILE* OsFopen(const char* path, const char* mode) {
#if defined(__ANDROID__) 
  return fopen(path, mode);
#else
  return fopen64(path, mode);
#endif
}

int OsPathAppend(char* path1, int path1_capacity, const char* path2) {
  assert(path1 != NULL && path2 != NULL);
  int length = strlen(path1);

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
  } else {
    path1->append(path2);
  }

  return 0;
}

int OsGetFullPath(const char* relative_path, std::string* full_path) {
  // @TODO(wangwencan): implement it;
  char *ret = realpath(relative_path, NULL);
  if (ret == NULL) {
    return -1;
  } else {
    full_path->assign(ret);
    free(ret);
    return 0;
  }
}

int64_t OsTimeInS() {
  return time(NULL);
}

int64_t OsTimeInMs() {
  struct timeval time;
  gettimeofday(&time, NULL);
  return (static_cast<int64_t>(time.tv_sec) * 1000 + 
          static_cast<int64_t>(time.tv_usec) / 1000);
}

int OsGetHostname(std::string* hostname) {
  char buf[HOST_NAME_MAX + 1];
  int ret = gethostname(buf, HOST_NAME_MAX + 1);
  if (ret == -1) {
    hostname->clear();
    return -1;
  } else {
    hostname->assign(buf);
    return 0;
  }
}

const char* OsGetLastErr() {
  return strerror(errno);
}

const int UUID_LEN = 36;
void OsGenUuid(string *uuid) {
  char uuid_buf[UUID_LEN + 1];
  uuid_t _uuid;
  uuid_generate(_uuid);
  uuid_unparse(_uuid, uuid_buf);
  uuid->assign(uuid_buf);
}

static void* thread_func(void* args) {
  OsThread *thread = static_cast<OsThread*>(args);
  int ret = thread->Run();
  return reinterpret_cast<void*>(ret);
}

int OsThread::Startup() {
  return pthread_create(&pid, NULL, thread_func, this);
}

int OsThread::Shutdown() {
  void *ret;
  int err = 0;
  if (pid != static_cast<pthread_t>(-1)) {
    err = pthread_join(pid, &ret);
    if (err != 0) {
      return err;
    }
    pid = -1;
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
  if (pid != static_cast<pthread_t>(-1)) {
    err = pthread_join(pid, &ret);
    if (err != 0) {
      return err;
    }
    return reinterpret_cast<intptr_t>(ret);
  }
  return 0;
}

OsTcpSocket::OsTcpSocket(const char* uri) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ >= 0);
  uri_ = uri;
}

OsTcpSocket::OsTcpSocket(const std::string& uri) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ >= 0);
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
  int ret = bind(fd_, (struct sockaddr *)&servaddr, sizeof(servaddr));
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

#if 0
static void DomainToAddr(const std::string& node,
                            std::vector<std::string>* super_nodes) {
  assert(super_nodes);
  std::string str_addr = node.substr(0, node.find(':'));
  std::string str_port =
      node.substr(node.find(':'), node.size() - node.find(':'));
  std::string str_node;

  struct addrinfo* result = NULL;
  struct addrinfo* ptr = NULL;
  struct addrinfo hints;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo(str_addr.data(), 0, &hints, &result);
  if (ret != 0) {
    return ;
  } else {
    for (ptr = result; ptr != NULL; ptr=ptr->ai_next) {
      switch (ptr->ai_family) {
        case AF_INET:
          str_node = inet_ntoa(reinterpret_cast<struct
                               sockaddr_in*>(ptr->ai_addr)->sin_addr);
          str_node += str_port;
          super_nodes->push_back(str_node);
          break;
        case AF_INET6:
          /*
          struct sockaddr_in6* ipv6 = 
              reinterpret_cast<struct sockaddr_in6*>(ptr->ai_addr);
          char to_ipv6[INET6_ADDRSTRLEN] = {0};
          //inet_ntop(AF_INET6, &ipv6->sin6_addr, to_ipv6, INET6_ADDRSTRLEN);
          str_node = to_ipv6;
          str_node += str_port;
          super_nodes->push_back(str_node);
          */
          break;
      }
    }
  }
}
#endif

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
  if (fd_ != -1) {
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

OsTcpSocket* OsTcpSocketFactory::Create(
    const std::string& uri, void* arg) {
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
  int ret = bind(fd_, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret == -1 && errno == EADDRINUSE) {
    return errno;
  } else {
    return ret;
  }
}

int OsUdpSocket::RecvFrom(string *buffer, int flags, string *src_addr) {
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
    fprintf(stderr, "recvfrom failed(%d): %s", errno, strerror(errno));
    return -1;
  }
  /*
     if (src_addr != NULL) {
     src_addr->append("udp://");
     src_addr->append(inet_ntoa(addr.sin_addr));
     src_addr->append(":");
     std::string port;
     StringFormat(&port, "%u", ntohs(addr.sin_port));
     src_addr->append(port);
     }
     */

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

  return pthread_self();  // Linux:  getpid returns thread ID when gettid is absent
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

static void timer_notify_function(union sigval sigev_value) {
  IOnTimer *on_timer = reinterpret_cast<IOnTimer*>(sigev_value.sival_ptr);
  on_timer->OnTimer();
}

int OsTimer::Initialize() {
#if defined(__ANDROID__) 
  if (timer_id == -1) {
#else
  if (timer_id == NULL) {
#endif
    struct sigevent sevp;
    memset(&sevp, 0, sizeof(sevp));
    sevp.sigev_notify = SIGEV_THREAD;
    sevp.sigev_value.sival_ptr = timer_func_;
    sevp.sigev_notify_function = timer_notify_function;
    int ret = timer_create(CLOCK_REALTIME, &sevp, &timer_id);
    if (ret == -1) {
      return ret;
    }
  }

  struct itimerspec timer_spec;

  time_t tv_sec = static_cast<time_t>(interval_in_ms_) / 1000;
  int64_t tv_nsec = (static_cast<int64_t>(interval_in_ms_) -
                     static_cast<int64_t>(tv_sec) * 1000) * 1000000;

  time_t due_tv_sec = static_cast<time_t>(due_time_in_ms_) / 1000;
  int64_t due_tv_nsec = (static_cast<int64_t>(due_time_in_ms_) -
                     static_cast<int64_t>(due_tv_sec) * 1000) * 1000000;

  timer_spec.it_interval.tv_sec = tv_sec;
  timer_spec.it_interval.tv_nsec = tv_nsec;
  timer_spec.it_value.tv_sec = due_tv_sec;
  timer_spec.it_value.tv_nsec = due_tv_nsec;

  int ret = timer_settime(timer_id, 0, &timer_spec, NULL);
  if (ret == -1) {
    return ret;
  }
  return 0;
}

int OsTimer::CleanUp() {
#if defined(__ANDROID__) 
  if (timer_id == -1) {
#else
  if (timer_id == NULL) {
#endif
    return 0;
  }
  struct itimerspec timer_spec;
  memset(&timer_spec, 0, sizeof(itimerspec));
  int ret = timer_settime(timer_id, 0, &timer_spec, NULL);
  if (ret == -1) {
    return ret;
  }
  return 0;
}

int OsStat(const std::string& path, const std::string&, 
           OsFileStat* file_stat) {
  struct stat64 stat_buf;
  int ret = stat64(path.c_str(), &stat_buf);

  if (ret == -1) {
    if (errno == ENOENT) {
      return errno;
    } else {
      return ret;
    }
  }

  if (S_ISDIR(stat_buf.st_mode)) {
    file_stat->type = OS_FILE_TYPE_DIR;
  } else if (S_ISREG(stat_buf.st_mode)) {
    file_stat->type = OS_FILE_TYPE_REG;
  } else {
    return ENOENT;
  }

  file_stat->path = path;
#if defined(__ANDROID__) 
  file_stat->mtime = static_cast<int64_t>(stat_buf.st_mtime) 
      * 1000 + stat_buf.st_mtime_nsec / 1000000;
#else
  file_stat->mtime = static_cast<int64_t>(stat_buf.st_mtim.tv_sec) 
      * 1000 + stat_buf.st_mtim.tv_nsec / 1000000;
#endif
  file_stat->length = stat_buf.st_size;
  file_stat->attr = stat_buf.st_mode & 07777;
  return 0;
}
  
#if defined (__ANDROID__)
int OsFsTraverser::traverse_helper(const char *dir_path) {
  errno = 0;
  DIR *dir = opendir(dir_path);
  if (!dir) {
    ZSLOG_ERROR("Open dir(%s) failed: %s", dir_path, strerror(errno));
    if (errno != ENOENT) {
      return -1;
    } else {
      return 0;
    }
  }
  
//  int name_max = pathconf(dirpath, _PC_NAME_MAX);
//  if (name_max == -1)         /* Limit not defined, or error */
//    name_max = 255;         /* Take a guess */
//  len = offsetof(struct dirent, d_name) + name_max + 1;
//  struct dirent * entryp = malloc(len);
//  assert(entryp);
  
  struct dirent *result = NULL;
  int readdir_ret = 0;
  
  errno = 0;
  while ((result = readdir(dir)) != NULL) {
    if (strcmp(result->d_name, ".") == 0
        || strcmp(result->d_name, "..") == 0) {
      continue;
    }
    
    std::string path(dir_path);
    path  = path + "/" + result->d_name;
    
    if (path != root && !visitor->IsIgnored(path.c_str())) {//path compare safe ?
      
      OsFileStat file_stat;
      int ret = OsStat(path.c_str(), string(), &file_stat);
      
      if (visitor->Visit(file_stat) < 0) {
        closedir(dir);
        ZSLOG_ERROR("visit file(%s) failed: ", path.c_str(), strerror(errno));
        //free(entryp);
        return -1;
      }
      
      if (file_stat.type == OS_FILE_TYPE_DIR) {
        if (readdir_ret == 0 && (readdir_ret = traverse_helper(path.c_str())) != 0) {
          closedir(dir);
          ZSLOG_ERROR("traverse dir(%s) failed: %s", path.c_str(), strerror(errno));
          //free(entryp);
          return readdir_ret;
        }
      }
    }
  }

  if (errno != 0) {
    ZSLOG_ERROR("traverse dir(%s) failed: %s", dir_path, strerror(errno));
  }
  
  closedir(dir);
  //free(entryp);
  return readdir_ret;
}

int OsFsTraverser::traverse() {
  char *path = const_cast<char*>(root.c_str());
  return traverse_helper(path);
}

#else
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
#endif

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

bool OsTempPath(const string& dir, const string& prefix, string* tmp_path) {
  char *tmp_path_ = tempnam(dir.c_str(), prefix.c_str());
  if (tmp_path_ == NULL) {
    return false;
  }
  tmp_path->assign(tmp_path_);
  free(tmp_path_);
  return true;
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
  if(bind(s_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
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

  pthread_cond_init(&cond_, 0); // never fail

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
  }

  pthread_cond_broadcast(&cond_); // never failed

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

std::string OsGetMacAddress() {
  struct ifreq io;
  struct ifconf ios_conf;
  char buf[1024] = {0};
  int fd = -1;
  std::string mac_address;

  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (fd == -1) {
    assert(0);
    return std::string();
  }

  ios_conf.ifc_len = sizeof(buf);
  ios_conf.ifc_buf = buf;
  if (ioctl(fd, SIOCGIFCONF, &ios_conf) == -1) {
    assert(0);
    return std::string();
  }

  struct ifreq *it = ios_conf.ifc_req;
  const struct ifreq* const end =
      it + (ios_conf.ifc_len / sizeof(struct ifreq));

  for (; it != end; it++) {
    strcpy(io.ifr_name, it->ifr_name);
    if (ioctl(fd, SIOCGIFFLAGS, &io) == 0) {
      if (io.ifr_flags & IFF_LOOPBACK) {
        continue;
      }
      if (ioctl(fd, SIOCGIFHWADDR, &io) == 0) {
        mac_address.resize(13);
        int ret = snprintf(&(*mac_address.begin()), 13,
                          "%02X%02X%02X%02X%02X%02X",
                          (unsigned char)io.ifr_hwaddr.sa_data[0],
                          (unsigned char)io.ifr_hwaddr.sa_data[1],
                          (unsigned char)io.ifr_hwaddr.sa_data[2],
                          (unsigned char)io.ifr_hwaddr.sa_data[3],
                          (unsigned char)io.ifr_hwaddr.sa_data[4],
                          (unsigned char)io.ifr_hwaddr.sa_data[5]);
        assert(ret == 12);
        mac_address.resize(12);
        break;
      }
    }
  }
  close(fd);

  return mac_address;
}

}  // namespace zs

