// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_UTILS_ZMQ_H_
#define ZISYNC_KERNEL_UTILS_ZMQ_H_

#include <zmq.h>
#include <cassert>
#include <string>

#ifdef _MSC_VER
  #pragma warning( push )
  #pragma warning( disable : 4244)
  #pragma warning( disable : 4267)
  #include <google/protobuf/message.h>
  #pragma warning( pop )
#else
  #include <google/protobuf/message.h>
#endif

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"

namespace zs {

using std::string;

class ZmqMsg;
class ZmqSocket;

class ZmqContext {
 public:
  ZmqContext() {
    context_ = zmq_ctx_new();
    assert(context_ != NULL);
  }

  ~ZmqContext() {
    zmq_ctx_destroy(context_);
  }

  void* context() const { return context_; }

 private:
  ZmqContext(ZmqContext&);
  void operator=(ZmqContext&);
  void* context_;
};

class ZmqMsg {
  friend class ZmqSocket;

 public:
  ZmqMsg() { zmq_msg_init(&msg_); }
  explicit ZmqMsg(size_t size) { zmq_msg_init_size(&msg_, size); }
  explicit ZmqMsg(const ::google::protobuf::Message &message) {
    zmq_msg_init_size(&msg_, message.ByteSize());
    message.SerializeToArray(zmq_msg_data(&msg_), size());
  }

  // include the '\0'
  explicit ZmqMsg(const string &data) {
    zmq_msg_init_size(&msg_, data.length() + 1);
    memcpy(zmq_msg_data(&msg_), data.c_str(), data.length() + 1);
  }

  virtual ~ZmqMsg() { zmq_msg_close(&msg_); }

  const void* data() const {
    return zmq_msg_data(const_cast<zmq_msg_t*>(&msg_));
  }

  const int size() const {
    return static_cast<int>(zmq_msg_size(const_cast<zmq_msg_t*>(&msg_)));
  }

  bool HasMore() {
    return zmq_msg_more(&msg_) ? true : false;
  }

  // without copy
  void SetData(void *data, size_t size, zmq_free_fn *func) {
    zmq_msg_close(&msg_);
    zmq_msg_init_data(&msg_, data, size, func, NULL);
  }

  // with copy
  void SetData(const void *data, int size_) {
    assert(size_ > 0);
    if (size_ != size()) {
      zmq_msg_close(&msg_);
      zmq_msg_init_size(&msg_, size_);
    }
    memcpy(zmq_msg_data(&msg_), data, size_);
  }

  // after resize, all data in the msg will lost
  void Resize(int size) {
    assert(size > 0);
    if (size != this->size()) {
      zmq_msg_close(&msg_);
      zmq_msg_init_size(&msg_, size);
    }
  }

  void CopyFrom(ZmqMsg *msg) {
    zmq_msg_copy(&msg_, &msg->msg_);
  }

  const zmq_msg_t& msg() const { return msg_; }

  virtual err_t SendTo(const ZmqSocket& socket, int flags);
  virtual err_t RecvFrom(const ZmqSocket& socket);

 private:
  void operator=(ZmqMsg&);

  zmq_msg_t msg_;
};

class ZmqIdentify {
 public :
  static const char ReadyStr[];

  ZmqIdentify() {}
  virtual ~ZmqIdentify() {}
  bool IsEmptyIdentify() { return identify_msg_.size() == 0; }

  explicit ZmqIdentify(const std::string& identify)
      : identify_msg_(identify) {
  }

  bool IsReadyMsg();
  
  virtual err_t SendTo(const ZmqSocket& socket, int flags = ZMQ_SNDMORE);
  virtual err_t RecvFrom(const ZmqSocket& socket);
  const void* data() const { 
    return identify_msg_.data();
  }
  const size_t size() { return identify_msg_.size(); }

 private:
  ZmqMsg identify_msg_;
  
  ZmqIdentify(ZmqIdentify&);
  void operator=(ZmqIdentify&);
};

class ZmqSocket {
  friend class ZmqMsg;  
 public:
  ZmqSocket(const ZmqContext &context, int type, int linger = 0 /* ms */ ) {
    socket_ = zmq_socket(const_cast<void*>(context.context()), type);
    if (socket_ == NULL) {
      ZSLOG_ERROR("zmq_socket() fail : %s", zmq_strerror(zmq_errno()));
    }
    assert(socket_ != NULL);
    if (type == ZMQ_SUB) {
      int ret = zmq_setsockopt(socket_, ZMQ_SUBSCRIBE, "", 0);
      assert(ret != -1);
    }
    int ret = zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(int));
    assert(ret != -1);
  }

  void Swap(ZmqSocket *that) {
    std::swap(socket_, that->socket_);
  }

  err_t GetSockOpt(
      int option_name, void *option_value, size_t *option_len);

  err_t SetSockOpt(
      int option_name, const void *option_value, size_t option_len);

  err_t Connect(const char *endpoint);
  err_t Bind(const char *endpoint);
  err_t Unbind(const char *endpoint);

  ~ZmqSocket() {
    assert(socket_ != NULL);
    zmq_close(socket_);
  }

  void* socket() const { return socket_; }

 private:
  ZmqSocket(ZmqSocket&);
  void operator=(ZmqSocket&);

  void* socket_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_ZMQ_H_

