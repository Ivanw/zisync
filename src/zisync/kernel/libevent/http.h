/**
 * @file tar_api.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar api define.
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

#ifndef TAR_API_H
#define TAR_API_H

#include <string>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class HttpRequestHead;

class ITarParserDelegate {
 public:
  virtual ~ITarParserDelegate() { }

  virtual void OnTarWillTransfer(
      const std::string& tmp_dir) = 0;
  virtual void OnTarDidTransfer(
      const std::string& tmp_dir) = 0;
  
  virtual void OnFileWillTransfer(
      const std::string& real_path,
      const std::string& encode_path) = 0;
  
  virtual void OnFileDidTransfered(
      const std::string& real_path,
      const std::string& encode_path,
      const std::string& sha1) = 0;
  virtual void OnFileDidSkiped(
      const std::string& real_path,
      const std::string& encode_path) = 0;
  
  virtual void OnByteDidTransfered(
      const std::string& real_path,
      const std::string& encode_path,
      int32_t nbytes) = 0;
};

class ITarParserDataSource {
 public:
  virtual ~ITarParserDataSource() { }

  virtual std::string GetRealPath(const std::string& encode_path) = 0;
  virtual std::string GetAlias(const std::string& encode_path) = 0;
};

class ITarWriterDelegate {
 public:
  virtual ~ITarWriterDelegate() { }

  virtual void OnFileWillTransfer(
      const std::string& real_path,
      const std::string& encode_path) = 0;
  
  virtual void OnFileDidTransfered(
      const std::string& real_path,
      const std::string& encode_path) = 0;
  virtual void OnFileDidSkiped(
      const std::string& real_path,
      const std::string& encode_path) = 0;
  
  virtual void OnByteDidTransfered(int32_t nbytes) = 0;
  virtual void OnByteDidSkiped(int32_t nbytes) = 0;
};

class ITarWriterDataSource {
 public:
  virtual ~ITarWriterDataSource() { }

  virtual bool EnumNext(std::string* realpath, std::string* encode_path,
                        std::string* alias, int64_t* size) = 0;
};

class IHttpResponseWriter {
 public:
  virtual ~IHttpResponseWriter() { }

  virtual void  HeadWriteAll(struct bufferevent* bev) = 0;
  virtual err_t BodyWriteSome(struct bufferevent* bev) = 0;
};

class IHttpResponseParser {
 public:
  virtual ~IHttpResponseParser() { }

  virtual err_t ParseMore(struct bufferevent* bev) = 0;
  virtual err_t OnEOF(struct bufferevent* bev) = 0;
};

class IHttpRequestParser {
 public:
  virtual ~IHttpRequestParser() {  }

  /*
   * @return ZISYNC_ERROR_AGAIN if more data is required
   *         ZISYNC_SUCCESS if had read Content-Length bytes data
   *         other error number defined in zisync_kernel.h if any error occurs.
   */
  virtual err_t ParseMore(struct bufferevent* bev) = 0;
  virtual err_t OnEOF(struct bufferevent* bev) = 0;
  virtual IHttpResponseWriter* CreateResponse(HttpRequestHead* http_head) = 0;
};


}  // namespace zs

#endif
