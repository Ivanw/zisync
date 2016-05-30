// Copyright 2014 zisync.com

#include <zmq.h>

#include <cassert>
#include <string>
#include <typeinfo>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/cipher.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/token.h"

namespace zs {

using std::string;

err_t ZmqMsg::SendTo(const ZmqSocket& socket, int flags) {
  if (zmq_msg_send(&msg_, socket.socket_, flags) == -1)  {
      ZSLOG_ERROR("zmq_msg_send() fail:%s", zmq_strerror(zmq_errno()));
      return ZISYNC_ERROR_ZMQ;
  }
  return zs::ZISYNC_SUCCESS;
}

// err_t ZmqSocket::Recv(ZmqMsg *msg) const {
//   // zmq_pollitem_t item[] = {
//   //   { socket_, -1, 0, 0 },
//   // };

//   // if (zmq_poll(item, 1, timeout) == -1) {
//   //   LOG(ERROR) << "zmq_pool(timeout = " << timeout << ") fail : " <<
//   //       zmq_strerror(zmq_errno());
//   //   return ZISYNC_ERROR_ZMQ;
//   // }
//   // if (item[0].revents & ZMQ_POLLIN) {
//   if (zmq_msg_recv(&msg->msg_, socket_, 0) == -1) {
//     ZSLOG_ERROR("zmq_msg_recv() fail : %s", zmq_strerror(zmq_errno()));
//     return ZISYNC_ERROR_ZMQ;
//   } else {
//     return ZISYNC_SUCCESS;
//   }
//   //} else {
//   //  return ZISYNC_ERROR_TIMEOUT;
//   //}
// }

err_t ZmqMsg::RecvFrom(const ZmqSocket& socket) {
  if (zmq_msg_recv(&msg_, socket.socket_, 0) == -1) {
    ZSLOG_ERROR("zmq_msg_recv() fail : %s", zmq_strerror(zmq_errno()));
    return ZISYNC_ERROR_ZMQ;
  }
  return zs::ZISYNC_SUCCESS;
}



// err_t ZmqSocket::SendIdentify(ZmqIdentify *identify) const {
//   err_t zisync_ret = Send(identify, ZMQ_SNDMORE);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Send identify fail : %s",
//         zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }

//   ZmqMsg empty_msg;
//   zisync_ret = Send(&empty_msg, ZMQ_SNDMORE);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Send empty msg fail : %s",
//         zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }

//   return ZISYNC_SUCCESS;
// }

err_t ZmqIdentify::SendTo(const ZmqSocket& socket, int flags) {
  err_t zisync_ret = identify_msg_.SendTo(socket, ZMQ_SNDMORE);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send identify fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  ZmqMsg empty_msg;
  zisync_ret = empty_msg.SendTo(socket, flags);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send empty msg fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  return ZISYNC_SUCCESS;
}


// err_t ZmqSocket::RecvIdentify(ZmqIdentify *identify) const {
//   err_t zisync_ret = Recv(identify);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Recv identify fail : %s",
//         zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }

//   ZmqMsg empty_msg;
//   zisync_ret = Recv(&empty_msg);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Recv empty msg fail : %s",
//         zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }
//   assert(identify->IsReadyMsg() || empty_msg.HasMore());

//   return ZISYNC_SUCCESS;
// }


err_t ZmqIdentify::RecvFrom(const ZmqSocket& socket) {
  // @TODO(liuchuanshi): implement it
  err_t zisync_ret = identify_msg_.RecvFrom(socket);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Recv identify fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  
  ZmqMsg empty_msg;
  zisync_ret = empty_msg.RecvFrom(socket);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Recv empty msg fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  if (!this->IsReadyMsg() && !empty_msg.HasMore()) {
    printf("haha");
  }
  assert(this->IsReadyMsg() || empty_msg.HasMore());
  return zs::ZISYNC_SUCCESS;
}

err_t ZmqSocket::GetSockOpt(
    int option_name, void *option_value, size_t *option_len) {
  if (zmq_getsockopt(socket_, option_name,
                     option_value, option_len) == -1) {
    ZSLOG_ERROR("zmq_getsockopt(%d) fail : %s", option_name,
        zmq_strerror(zmq_errno()));
    return ZISYNC_ERROR_ZMQ;
  }
  return ZISYNC_SUCCESS;
}

err_t ZmqSocket::SetSockOpt(
    int option_name, const void *option_value, size_t option_len) {
  if (zmq_setsockopt(socket_, option_name,
                     option_value, option_len) == -1) {
    ZSLOG_ERROR("zmq_setsockopt(%d) fail : %s", option_name,
        zmq_strerror(zmq_errno()));
    return ZISYNC_ERROR_ZMQ;
  } else {
    return ZISYNC_SUCCESS;
  }
}

err_t ZmqSocket::Connect(const char *endpoint) {
  if (zmq_connect(socket_, endpoint) == -1) {
    ZSLOG_ERROR("zmq_connect(%s) fail : %s", endpoint,
        zmq_strerror(zmq_errno()));
    return ZISYNC_ERROR_ZMQ;
  } else {
    return ZISYNC_SUCCESS;
  }
}

err_t ZmqSocket::Bind(const char *endpoint) {
  if (zmq_bind(socket_, endpoint) == -1) {
    ZSLOG_ERROR("zmq_bind(%s) fail : %s", endpoint,
        zmq_strerror(zmq_errno()));
    if (zmq_errno() == EADDRINUSE) {
      return ZISYNC_ERROR_ADDRINUSE;
    } else {
      return ZISYNC_ERROR_ZMQ;
    }
  } else {
    return ZISYNC_SUCCESS;
  }
}

err_t ZmqSocket::Unbind(const char *endpoint) {
  if (zmq_unbind(socket_, endpoint) == -1) {
    ZSLOG_ERROR("zmq_unbind(%s) fail : %s", endpoint,
        zmq_strerror(zmq_errno()));
      return ZISYNC_ERROR_ZMQ;
  } else {
    return ZISYNC_SUCCESS;
  }
}

const char ZmqIdentify::ReadyStr[] = "READY_MSG";
// err_t ZmqSocket::SendReadyMsg() const {
//   ZmqMsg msg(ReadyStr);
//   err_t zisync_ret = msg.Send(*this, 0);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Send Ready Msg fail : %s",
//         zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }

//   return ZISYNC_SUCCESS;
// }

bool ZmqIdentify::IsReadyMsg() {
  std::string data(reinterpret_cast<const char*>(identify_msg_.data()), identify_msg_.size());
  return (identify_msg_.size() == sizeof(ReadyStr)) && 
      memcmp(ReadyStr, data.data(), data.size()) == 0;
}
// 
// 
//   assert(identify->HasMore());
// 
//   ZmqMsg empty_msg;
//   zisync_ret = Recv(&empty_msg);
//   if (zisync_ret != ZISYNC_SUCCESS) {
//     ZSLOG_ERROR("Recv empty msg fail : %s",
//         zisync_strerror(zisync_ret));
//     return zisync_ret;
//   }
//   assert((!identify->IsReadyMsg() && empty_msg.HasMore()) || 
//          (identify->IsReadyMsg() && !empty_msg.HasMore()));


}  // namespace zs
