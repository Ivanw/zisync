/**
 * @file http_response.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Http Response implementation.
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

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <memory>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/transfer/transfer.pb.h"
#include "zisync/kernel/libevent/http.h"

struct bufferevent;

namespace zs {

class TarWriter;
class ITaskMonitor;

class HttpResponseHead {
  enum ParseState {
    PS_PARSE_BEGIN, PS_PARSE_HEAD, PS_PARSE_END
  };
 public:
  HttpResponseHead()
      : http_code_(0), parse_state_(PS_PARSE_BEGIN) { }
  ~HttpResponseHead() { }

  err_t ParseMore(struct bufferevent* bev);
  err_t OnEOF(struct bufferevent* bev);

  int32_t httpcode() {
    return http_code_;
  }
 private:
  int32_t http_code_;
  ParseState parse_state_;
};

class ErrorResponseWriter : public IHttpResponseWriter {
 public:
  ErrorResponseWriter(int http_code) : http_code_(http_code) { }
  virtual ~ErrorResponseWriter() { }

  virtual void  HeadWriteAll(struct bufferevent* bev);
  virtual err_t BodyWriteSome(struct bufferevent* bev);

  static ErrorResponseWriter* GetByErr(err_t eno);

 private:
  int http_code_;
};

class TarGetResponseWriter : public IHttpResponseWriter
                           , public ITarWriterDelegate
                           , public ITarWriterDataSource {
  enum WriteState {
    FILE_HEAD, FILE_DATA, FILE_DONE, FILE_ERROR
  };
 public:
  TarGetResponseWriter(const std::string& tree_root,
                       const std::string& tree_uuid_,
                       const TarGetFileList& file_list,
                       HttpRequestHead* head);
  virtual ~TarGetResponseWriter();

  //
  // Implement IHttpResponseWriter
  //
  virtual void  HeadWriteAll(struct bufferevent* bev);  
  virtual err_t BodyWriteSome(struct bufferevent* bev);

  //
  // Implement ITarWriterDataSource
  //
  virtual bool EnumNext(
      std::string* real_path, std::string* encode_path, 
      std::string* alias, int64_t* size);

  //
  // Implement ITarWriterDelegate
  //
  virtual void OnFileWillTransfer(
      const std::string& real_path,
      const std::string& encode_path);
  
  virtual void OnFileDidTransfered(
      const std::string& real_path,
      const std::string& encode_path);
  virtual void OnFileDidSkiped(
      const std::string& real_path,
      const std::string& encode_path);
  
  virtual void OnByteDidTransfered(int32_t nbytes);
  virtual void OnByteDidSkiped(int32_t nbytes);

 private:
  std::unique_ptr<TarWriter> body_writer_;

  std::string tree_root_, tree_uuid_;
  int current_index_;
  TarGetFileList file_list_;
  std::unique_ptr<ITaskMonitor> monitor_;
};

class ErrorResponseParser : public IHttpResponseParser {
 public:
  ErrorResponseParser(int http_code);
  virtual ~ErrorResponseParser() { }

  virtual err_t ParseMore(struct bufferevent* bev) {
    return ZISYNC_SUCCESS; 
  }
  virtual err_t OnEOF(struct bufferevent* bev) {
    return ZISYNC_SUCCESS;
  }
};

class EmptyResponseParser : public IHttpResponseParser {
 public:
  EmptyResponseParser() {}
  virtual ~EmptyResponseParser() { }

  virtual err_t ParseMore(struct bufferevent* bev) {
    return ZISYNC_SUCCESS; 
  }
  virtual err_t OnEOF(struct bufferevent* bev) {
    return ZISYNC_SUCCESS;
  }
};

}  // namespace zs
#endif
