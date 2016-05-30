/**
 * @file tar_head.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar head parser.
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

#ifndef TAR_READER_H
#define TAR_READER_H

#include "zisync/kernel/platform/platform.h"

#include <assert.h>
#include <fcntl.h>
#include <event2/bufferevent.h>
#include <openssl/sha.h>

#include <memory>


#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/libevent/http.h"
#include "zisync/kernel/libevent/libtar++.h"

namespace zs {

class ITarParserDelegate;

typedef enum {
  PS_PARSE_TAR_HEADER_BEGIN = 1,
  PS_PARSE_TAR_HEADER_LONGNAME,
  PS_PARSE_TAR_HEADER_READ_HEADER,
  PS_PARSE_TAR_HEADER_END,
}ps_parse_tar_header_t;

class FileHeadTarReader {
 public:
  FileHeadTarReader() : parse_state_(PS_PARSE_TAR_HEADER_BEGIN) {
    memset(&file_header_, 0, sizeof(struct tar_header));
  }
  ~FileHeadTarReader() {
  }
  // Reset reader to init state. Shoud never failed  
  void  Reset();

  // parse all data in bev.
  //
  // @return
  //   ZISYNC_SUCCESS:     if finishing parse tar header.
  //   ZISYNC_ERROR_AGAIN: if more data requires.
  //   other error code:   if some error occurs.
  err_t ParseMore(struct bufferevent* bev);
  err_t OnEOF(struct bufferevent* bev);

  std::string FileName() const {
    std::string filename;
    char namebuf[MAX_PATH] = {0};
    filename = th_get_pathname(&file_header_, namebuf, MAX_PATH);
    return filename;
  }

  int64_t FileSize() const {
    return th_get_size(&file_header_);
  }

  bool IsReg() const {
    return TH_ISREG(&file_header_);
  }

 private:
  struct tar_header file_header_;
  ps_parse_tar_header_t parse_state_;
  int longname_length_;
  char *longname_ptr_;
};

class FileDataTarReader {
 public:
  explicit FileDataTarReader()
      : ok_(true), file_size_(0) {
  }

  ~FileDataTarReader() {
  }
  // Reset reader to init state, and do necessary prepare for
  // reading data, such open file
  //
  // @return
  //   ZISYNC_SUCCESS: if ok
  //   error code:     if failed: e.g. can not open file for
  //                   writing data
  err_t Reset(const FileHeadTarReader& file_head,
              const std::string& real_path, const std::string &alias);

  // parse all data in bev.
  //
  // @return
  //   ZISYNC_SUCCESS:     if finishing read the whole file.
  //   ZISYNC_ERROR_AGAIN: if more data requires.
  //   other error code:   if some error occurs.
  err_t ParseMore(struct bufferevent* bev, ITarParserDelegate* delegate);
  err_t OnEOF(struct bufferevent* bev, ITarParserDelegate* delegate);

  const std::string& sha1() {
    return sha1_;
  }

  bool IsOk() {
    return ok_;
  }

 private:
  std::string sha1_;
  std::string real_path_;
  std::string encode_path_;
  OsFile file_;
  bool ok_;
  int64_t file_size_;
  SHA_CTX c_;
};


class TarParser {
  enum ParseState {
    FILE_HEAD, FILE_DATA, FILE_DONE, FILE_ERROR
  };

 public:
  TarParser(ITarParserDataSource* data_source,
            ITarParserDelegate* delegate);
  virtual ~TarParser();

  virtual err_t ParseMore(struct bufferevent* bev);
  virtual err_t OnEOF(struct bufferevent* bev);

 private:
  ITarParserDataSource* data_source_;
  ITarParserDelegate* delegate_;

  ParseState parse_state_;
  std::unique_ptr<FileHeadTarReader> file_head_;
  std::unique_ptr<FileDataTarReader> file_data_;
};


}  // namespace zs  

#endif  // TAR_READER_H
