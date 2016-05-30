/**
 * @file response.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Implment of response.h.
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

#include <string>
#include <memory>
#include <cassert>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/cipher.h"
#include "zisync/kernel/utils/token.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/configure.h"

namespace zs {

static inline MsgErrorCode ErrToErrorCode(err_t error) {
  switch (error) {
    case ZISYNC_ERROR_ADDRINUSE:
      return ZE_ADDRINUSE;
    case ZISYNC_ERROR_TREE_NOENT:
      return ZE_TREE_NOENT;
    case ZISYNC_ERROR_SYNCDIR_MISMATCH:
      return ZE_SYNCDIR_MISMATCH;
    case ZISYNC_ERROR_FILE_NOENT:
      return ZE_FILE_NOENT;
    case ZISYNC_ERROR_INVALID_MSG:
      return ZE_BAD_MSG;
    case ZISYNC_ERROR_VERSION_INCOMPATIBLE:
      return ZE_VERSION_INCOMPATIBLE;
    case ZISYNC_ERROR_OS_IO:
      return ZE_FILESYSTEM;
    case ZISYNC_ERROR_NOT_DIR:
      return ZE_NOT_DIR;
    case ZISYNC_ERROR_TREE_EXIST:
      return ZE_TREE_EXIST;
    case ZISYNC_ERROR_PERMISSION_DENY:
      return ZE_PERMISSION_DENY;
    default:
      return ZE_GENERAL;
  }
}

static inline err_t ErrorCodeToErr(MsgErrorCode error) {
  switch (error) {
    case ZE_ADDRINUSE:
      return ZISYNC_ERROR_ADDRINUSE;
    case ZE_TREE_NOENT:
      return ZISYNC_ERROR_TREE_NOENT;
    case ZE_SYNCDIR_MISMATCH:
      return ZISYNC_ERROR_SYNCDIR_MISMATCH;
    case ZE_FILE_NOENT:
      return ZISYNC_ERROR_FILE_NOENT;
    case ZE_BAD_MSG:
      return ZISYNC_ERROR_INVALID_MSG;
    case ZE_VERSION_INCOMPATIBLE:
      return ZISYNC_ERROR_VERSION_INCOMPATIBLE;
    case ZE_FILESYSTEM:
      return ZISYNC_ERROR_OS_IO;
    case ZE_NOT_DIR:
      return ZISYNC_ERROR_NOT_DIR;
    case ZE_TREE_EXIST:
      return ZISYNC_ERROR_TREE_EXIST;
    case ZE_PERMISSION_DENY:
      return ZISYNC_ERROR_PERMISSION_DENY;
    default:
      return ZISYNC_ERROR_GENERAL;
  }
}

err_t Message::SendTo(const ZmqSocket& socket) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  MsgHead head;
  head.set_version(MSG_VERSION);
  head.set_code(msg_code());
  head.set_level(EL_NONE);
  head.set_uuid(Config::device_uuid());

  ZmqMsg head_zmq_msg(head);
  zisync_ret = head_zmq_msg.SendTo(
      socket, mutable_msg() == NULL ? 0: ZMQ_SNDMORE);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send head fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  if (mutable_msg() == NULL) {
    return ZISYNC_SUCCESS;
  }

  ZmqMsg body_zmq_msg(*mutable_msg());
  zisync_ret = body_zmq_msg.SendTo(socket, 0);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send body fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  return ZISYNC_SUCCESS;
}

err_t Message::SendToWithAccountEncrypt(const ZmqSocket& socket) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  MsgHead head;
  head.set_version(MSG_VERSION);
  head.set_code(msg_code());
  head.set_level(EL_ENCRYPT_WITH_ACCOUNT);
  head.set_uuid(Config::device_uuid());

  ZmqMsg head_zmq_msg(head);
  zisync_ret = head_zmq_msg.SendTo(
      socket, mutable_msg() == NULL ? 0: ZMQ_SNDMORE);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send head fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  if (mutable_msg() == NULL) {
    return ZISYNC_SUCCESS;
  }

  ZmqMsg body_zmq_msg(*mutable_msg()), encrypted_body_zmq_msg;
  AesCipher cipher(Config::account_key());

  zisync_ret = cipher.Encrypt(&body_zmq_msg, &encrypted_body_zmq_msg);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Encrypt body fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  zisync_ret = encrypted_body_zmq_msg.SendTo(socket, 0);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send entrypt body fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  return ZISYNC_SUCCESS;
}

// err_t Message::SendTo(const ZmqSocket& socket,
//                         const std::string& local_uuid,
//                         const std::string& remote_uuid) {
//   err_t zisync_ret = ZISYNC_SUCCESS;
// 
//   MsgHead head;
//   head.set_version(MSG_VERSION);
//   head.set_code(msg_code());
//   head.set_level(EL_ENCRYPT);
//   head.set_uuid(local_uuid);
// 
//   ZmqMsg head_zmq_msg(head);
//   zisync_ret = head_zmq_msg.SendTo(
//       socket, mutable_msg() == NULL ? 0 : ZMQ_SNDMORE);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Send head fail : %s", zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }
// 
//   if (mutable_msg() == NULL) {
//     return ZISYNC_SUCCESS;
//   }
// 
//   ZmqMsg body_zmq_msg(*mutable_msg()), encrypted_body_zmq_msg;
//   // AesCipher cipher(GetTokenService()->GetSendToken(remote_uuid));
// 
//   encrypted_body_zmq_msg.CopyFrom(&body_zmq_msg);
//   // zisync_ret = cipher.Encrypt(&body_zmq_msg, &encrypted_body_zmq_msg);
//   // if (zisync_ret != ZISYNC_SUCCESS) {
//   //   ZSLOG_ERROR("Encrypt body fail : %s", zisync_strerror(zisync_ret));
//   //   return zisync_ret;
//   // }
// 
//   zisync_ret = encrypted_body_zmq_msg.SendTo(socket, 0);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Send entrypt body fail : %s", zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }
// 
//   return ZISYNC_SUCCESS;
// }

static inline void RecvTailMsg(
    const ZmqSocket& socket, ZmqMsg* last_msg) {
  bool has_more = last_msg->HasMore();
  while (has_more) {
    ZmqMsg msg;
    err_t ret = msg.RecvFrom(socket);
    assert(ret == ZISYNC_SUCCESS);
    has_more = msg.HasMore();
  }
}

static inline err_t DecrytBodyZmqMsg(
    const MsgHead &head, ZmqMsg *body_zmq_msg, 
    ZmqMsg *decrypted_body_zmq_msg) {
  if (head.level() == EL_NONE) {
    decrypted_body_zmq_msg->CopyFrom(body_zmq_msg);
  } else if (head.level() == EL_ENCRYPT_WITH_ACCOUNT) {
    AesCipher cipher(Config::account_key());
    return cipher.Decrypt(body_zmq_msg, decrypted_body_zmq_msg);
  } else {
    // TODO
    decrypted_body_zmq_msg->CopyFrom(body_zmq_msg);
  }
  return ZISYNC_SUCCESS;
}

err_t Message::RecvFrom(
    const ZmqSocket& socket, MsgEncryptLevel expected_encrypt_level, 
    std::string* src_uuid) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  ZmqMsg head_zmq_msg;
  zisync_ret = head_zmq_msg.RecvFrom(socket);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Recv request head fail : %s", zmq_strerror(zisync_ret));
    return zisync_ret;
  }

  MsgHead head;
  if (!head.ParseFromArray(head_zmq_msg.data(), head_zmq_msg.size())) {
    ZSLOG_ERROR("Parse head fail.");
    RecvTailMsg(socket, &head_zmq_msg);
    return ZISYNC_ERROR_INVALID_MSG;
  }

  if (head.version() != MSG_VERSION) {
    ZSLOG_ERROR("Incompatible version");
    return ZISYNC_ERROR_VERSION_INCOMPATIBLE;
  }

  if (head.code() != msg_code() && head.code() != MC_ERROR) {
    ZSLOG_ERROR("MsgCode should be (%d) but is (%d)",
                msg_code(), head.code());
    RecvTailMsg(socket, &head_zmq_msg);
    return ZISYNC_ERROR_INVALID_MSG;
  }
  if (head.level() != expected_encrypt_level && head.code() != MC_ERROR) {
    ZSLOG_ERROR("Expected EncryptLevel(%d) but is (%d)",
                expected_encrypt_level, head.level());
    return ZISYNC_ERROR_PERMISSION_DENY;
  }
  if (src_uuid != NULL) {
    *src_uuid = head.uuid();
  }
  // head.set_level(EL_NONE);
  // head.set_uuid("");

  // Handle body
  if (head_zmq_msg.HasMore()) {
    if (mutable_msg() == NULL && head.code() != MC_ERROR) {
      ZSLOG_ERROR("Request should not have body but have.");
      RecvTailMsg(socket, &head_zmq_msg);
      return ZISYNC_ERROR_INVALID_MSG;
    }

    ZmqMsg body_zmq_msg;
    zisync_ret = body_zmq_msg.RecvFrom(socket);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Recv request body fail : %s", zmq_strerror(zisync_ret));
      return zisync_ret;
    }

    if (body_zmq_msg.HasMore()) {
      ZSLOG_ERROR("Expect no more msg but have");
      RecvTailMsg(socket, &body_zmq_msg);
      return ZISYNC_ERROR_INVALID_MSG;
    }
    
    ZmqMsg decrypted_body_zmq_msg;
    zisync_ret = DecrytBodyZmqMsg(
        head, &body_zmq_msg, &decrypted_body_zmq_msg);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Decrypt request body fail : %s", zmq_strerror(zisync_ret));
      return ZISYNC_ERROR_PERMISSION_DENY;
    }

    MsgError msg_error;
    auto body_msg = (head.code() == MC_ERROR) ? &msg_error : mutable_msg();
    if (!body_msg->ParseFromArray(
            decrypted_body_zmq_msg.data(), decrypted_body_zmq_msg.size())) {
        ZSLOG_ERROR("ParseFromArray fail.");
        return ZISYNC_ERROR_INVALID_MSG;
    }
    if (head.code() == MC_ERROR) {
      return ErrorCodeToErr(msg_error.errcode());
    }
  } else {
    if (mutable_msg() != NULL || head.code() == MC_ERROR) {
      ZSLOG_ERROR("Request should not have body but have.");
      return ZISYNC_ERROR_INVALID_MSG;
    }
  }

  return ZISYNC_SUCCESS;
}

err_t Message::RecvFrom(
    const ZmqSocket& socket, MsgEncryptLevel expected_encrypt_level, 
    int timeout, std::string* src_uuid) {
  zmq_pollitem_t item[] = {
     { socket.socket(), -1, ZMQ_POLLIN, 0 },
  };
  int ret = 0;

  assert(timeout >= 0);
  if (timeout < 0) {
    timeout = 0;
  }
  while (timeout >= 0) {
    if (zs::IsAborted()) {
      return ZISYNC_ERROR_TIMEOUT;
    }
    ret = zmq_poll(item, 1, timeout < 1000 ? timeout :1000);
    if (ret == -1) {
      ZSLOG_WARNING("zmq_poll error, timeout(%d): %s",
                    timeout, zmq_strerror(zmq_errno()));
      return ZISYNC_ERROR_GENERAL;
    } else if (ret != 0) {
      break;
    }
    timeout -= 1000;
  }
  
  if (!(item[0].revents & ZMQ_POLLIN)) {
     return ZISYNC_ERROR_TIMEOUT;
  }

  return RecvFrom(socket, expected_encrypt_level, src_uuid);
}

err_t ErrorResponse::SendTo(const ZmqSocket& socket) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  MsgHead head;
  head.set_version(MSG_VERSION);
  head.set_code(MC_ERROR);
  head.set_level(EL_NONE);
  head.set_uuid(Config::device_uuid());

  ZmqMsg head_zmq_msg(head);
  zisync_ret = head_zmq_msg.SendTo(socket, ZMQ_SNDMORE);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send error head fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  MsgError body;
  body.set_errcode(ErrToErrorCode(zisync_errno_));
  
  ZmqMsg body_zmq_msg(body);
  zisync_ret = body_zmq_msg.SendTo(socket, 0);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send error body fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  return ZISYNC_SUCCESS;
}


MessageContainer::MessageContainer(
    std::map<MsgCode, MessageHandler*>& handler_map, bool has_identify) 
    : has_identify_(has_identify), handler_map_(handler_map) {
}

err_t MessageContainer::RecvMessage(
    const ZmqSocket& socket, MsgHead *head, MessageHandler **handler_p) {
  
  err_t zisync_ret = ZISYNC_SUCCESS;

  ZmqMsg head_zmq_msg;
  zisync_ret = head_zmq_msg.RecvFrom(socket);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Recv request head fail : %s", zmq_strerror(zisync_ret));
    return zisync_ret;
  }

  if (!head->ParseFromArray(head_zmq_msg.data(), head_zmq_msg.size())) {
    ZSLOG_ERROR("Parse head fail.");
    RecvTailMsg(socket, &head_zmq_msg);
    return ZISYNC_ERROR_INVALID_MSG;
  }

  if (head->version() != MSG_VERSION) {
    ZSLOG_ERROR("Incompatible version");
    return ZISYNC_ERROR_VERSION_INCOMPATIBLE;
  }

  MessageHandler* handler = NULL;
  auto it = handler_map_.find(head->code());
  if (it != handler_map_.end()) {
    handler = it->second;
  } else {
    ZSLOG_ERROR("Can not find handler for request(%d): "
                "unexpected or unsuppored msg_code.", head->code());
    RecvTailMsg(socket, &head_zmq_msg);
    return ZISYNC_ERROR_INVALID_MSG;
  }
  assert(handler != NULL);

  // Handle body
  if (head_zmq_msg.HasMore()) {
    if (handler->mutable_msg() == NULL) {
      ZSLOG_ERROR("Request(%d) should not have body but have.", 
                  head->code());
      RecvTailMsg(socket, &head_zmq_msg);
      return ZISYNC_ERROR_INVALID_MSG;
    }

    ZmqMsg body_zmq_msg;
    zisync_ret = body_zmq_msg.RecvFrom(socket);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Recv request body fail : %s", zmq_strerror(zisync_ret));
      return zisync_ret;
    }

    if (body_zmq_msg.HasMore()) {
      ZSLOG_ERROR("Expect no more msg but have");
      RecvTailMsg(socket, &body_zmq_msg);
      return ZISYNC_ERROR_INVALID_MSG;
    }

    ZmqMsg decrypted_body_zmq_msg;
    zisync_ret = DecrytBodyZmqMsg(
        *head, &body_zmq_msg, &decrypted_body_zmq_msg);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("Decrypt request body fail : %s", zmq_strerror(zisync_ret));
      return ZISYNC_ERROR_PERMISSION_DENY;
    }

    auto body_msg = handler->mutable_msg();
    if (!body_msg->ParseFromArray(
            decrypted_body_zmq_msg.data(), decrypted_body_zmq_msg.size())) {
        ZSLOG_ERROR("ParseFromArray fail.");
        return ZISYNC_ERROR_INVALID_MSG;
    }
  } else {
    if (handler->mutable_msg() != NULL) {
      ZSLOG_ERROR("Request(%d) should have body but not.", head->code());
      return ZISYNC_ERROR_INVALID_MSG;
    }
  }

  *handler_p = handler;
  return ZISYNC_SUCCESS;
}


err_t MessageContainer::RecvAndHandleSingleMessageInternal(
    const ZmqSocket& socket, void* userdata) {
  err_t zisync_ret = ZISYNC_SUCCESS;

  ZmqIdentify client_identify;
  if (has_identify_) {
    zisync_ret = client_identify.RecvFrom(socket);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  MessageHandler *handler = NULL;
  MsgHead head;
  zisync_ret = RecvMessage(socket, &head, &handler);

  if (has_identify_) {
    err_t not_ret = client_identify.SendTo(socket);
    assert(not_ret == ZISYNC_SUCCESS);

    if (zisync_ret != ZISYNC_SUCCESS) {
      return zisync_ret;
    }
  }

  msg_code = head.code();
  // Handle message
  return handler->HandleMessage(socket, head, userdata);
}

err_t MessageContainer::RecvAndHandleSingleMessage(
    const ZmqSocket& socket, void* userdata) {
  err_t zisync_ret = RecvAndHandleSingleMessageInternal(socket, userdata);
  if (zisync_ret != ZISYNC_SUCCESS) {
    /* before handle, send client_identify first */
    err_t not_ret = ErrorResponse(zisync_ret).SendTo(socket);
    assert(not_ret == ZISYNC_SUCCESS);
  }

  return zisync_ret;
}




}  // namespace zs
