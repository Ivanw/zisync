#ifndef ZISYNC_KERNEL_UTILS_MESSAGE_H_
#define ZISYNC_KERNEL_UTILS_MESSAGE_H_

#ifdef _MSC_VER
  #pragma warning( push )
  #pragma warning( disable : 4244)
  #pragma warning( disable : 4267)
  #include <google/protobuf/message.h>
  #include "zisync/kernel/proto/kernel.pb.h"
  #pragma warning( pop )
#else
  #include <google/protobuf/message.h>
  #include "zisync/kernel/proto/kernel.pb.h"
#endif

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class ZmqSocket;
class ZmqIdentify;

const int MSG_VERSION = 1;

class Message {
 public:
  virtual ~Message() {}
  virtual ::google::protobuf::Message* mutable_msg() = 0;
  virtual MsgCode msg_code() const = 0;

  err_t SendTo(const ZmqSocket& socket);
  err_t SendToWithAccountEncrypt(const ZmqSocket& socket);
  // err_t SendTo(const ZmqSocket& socket,
  //            const std::string& local_uuid,
  //            const std::string& remote_uuid);
  err_t RecvFrom(const ZmqSocket& socket) {
    return RecvFrom(socket, EL_NONE, NULL);
  }
  err_t RecvFromWithAccountEncrypt(
      const ZmqSocket& socket, int timeout, std::string* src_uuid) {
    return RecvFrom(socket, EL_ENCRYPT_WITH_ACCOUNT, timeout, src_uuid);
  }
  err_t RecvFrom(
      const ZmqSocket& socket, int timeout, std::string* src_uuid) {
    return RecvFrom(socket, EL_NONE, timeout, src_uuid);
  }
  err_t RecvFrom(
      const ZmqSocket& socket, std::string* src_uuid) {
    return RecvFrom(socket, EL_NONE, src_uuid);
  }
 private:
  err_t RecvFrom(
      const ZmqSocket& socket, MsgEncryptLevel expected_encrypt_level, 
      std::string* src_uuid);
  err_t RecvFrom(
      const ZmqSocket& socket, MsgEncryptLevel expected_encrypt_level, 
      int timeout, std::string* src_uuid);

  err_t RecvHeadFrom(const ZmqSocket& socket, MsgHead *head);
};

class ErrorResponse {
 public:
  explicit ErrorResponse(err_t zisync_errno)
      : zisync_errno_(zisync_errno) {}
  virtual ~ErrorResponse() {}
  
  err_t SendTo(const ZmqSocket& socket);

 private:
  err_t zisync_errno_;
};

class NullBodyMessage : public Message {
 public:
  NullBodyMessage(MsgCode code) : code_(code) { }
  virtual ~NullBodyMessage() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  virtual MsgCode msg_code() const {
    return code_;
  }

 private:
  MsgCode code_;  
};

class MessageHandler {
 public:
  virtual ~MessageHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() = 0;
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata) = 0;
};

class MessageContainer {
 public:
  MessageContainer(std::map<MsgCode, MessageHandler*>& handler_map, bool has_identify);
  //
  // @param userdata will be passed to MessageHandler::HandleMessage()
  //
  err_t RecvAndHandleSingleMessage(const ZmqSocket& socket, void* userdata);
 protected:
  err_t RecvMessage(const ZmqSocket& socket, MsgHead *head, 
                    MessageHandler **handler_p);
  err_t RecvAndHandleSingleMessageInternal(
      const ZmqSocket& socket, void* userdata);
 private:
  bool has_identify_; 
  std::map<MsgCode, MessageHandler*>& handler_map_;
  MsgCode msg_code;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_MESSAGE_H_
