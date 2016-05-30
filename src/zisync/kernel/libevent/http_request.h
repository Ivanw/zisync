/**
 * @file http_request.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief http request.
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

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <memory>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/transfer/transfer.pb.h"
#include "zisync/kernel/libevent/http.h"


namespace zs {

class TarParser;
class IHttpResponseWriter;

class HttpRequestHead {
  enum ParseState {
    PS_PARSE_BEGIN, PS_PARSE_HEAD, PS_PARSE_END
  };

 public:
  HttpRequestHead() :
      total_files_(0), total_bytes_(0),
      parse_state_(PS_PARSE_BEGIN) { }
  ~HttpRequestHead() { }

  err_t OnRead(struct bufferevent* bev);
  err_t OnEOF(struct bufferevent* bev);

  const std::string& method() {
    return method_;
  }

  const std::string& format() {
    return format_;
  }

  const std::string& local_tree_uuid() {
    return local_tree_uuid_;
  }

  const std::string& remote_tree_uuid() {
    return remote_tree_uuid_;
  }

  int32_t total_files() {
    return total_files_;
  }

  int64_t total_bytes() {
    return total_bytes_;
  }

 private:
  std::string method_;
  std::string format_;
  std::string local_tree_uuid_;
  std::string remote_tree_uuid_;

  int32_t total_files_;
  int64_t total_bytes_;

  ParseState parse_state_;
};



// class IHttpRequestWriter {
//  public:
//   virtual ~IHttpRequestWriter() {  }
//
//   virtual void  HeadWriteAll(struct bufferevent* bev) = 0;
//   virtual err_t BodyWriteSome(struct bufferevent* bev) = 0;
// };

class TarGetRequestParser : public IHttpRequestParser {
 public:
  TarGetRequestParser(const std::string& tree_root, 
                      const std::string &tree_uuid);
  virtual ~TarGetRequestParser();

  virtual err_t ParseMore(struct bufferevent* bev);
  virtual err_t OnEOF(struct bufferevent* bev);
  virtual IHttpResponseWriter* CreateResponse(HttpRequestHead* http_head);

 private:
  std::string tree_root_, tree_uuid_;
  TarGetFileList file_list_;
  std::unique_ptr<struct evbuffer, decltype(evbuffer_free)*> buffer_;
};

}  // namespace zs

#endif
