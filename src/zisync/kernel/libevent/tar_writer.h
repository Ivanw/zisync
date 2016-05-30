/**
 * @file tar_writer.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar writer.
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

#ifndef TAR_WRITER_H
#define TAR_WRITER_H

#include "zisync/kernel/platform/platform.h"

#include <stdio.h>
#include <event2/bufferevent.h>

#include <memory>

#include "zisync/kernel/libevent/libtar++.h"
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/libevent/http.h"


namespace zs {

class ITarWriterDelegate;
class ITarWriterDataSource;

class FileTarWriter {
 public:
  FileTarWriter();
  ~FileTarWriter();
  // Reset reader to init state, and do necessary prepare for
  // reading data, such open file
  //
  // @return
  //   ZISYNC_SUCCESS: if ok
  //   error code:     if failed: e.g. can not open file for
  //                   writing data
  err_t Reset(const std::string& real_path,
              const std::string& alias, 
              const std::string& encode_path);

  // Write Tar header. should never failed
  void WriteHead(struct bufferevent* bev);

  // parse all data in bev.
  //
  // @return
  //   ZISYNC_SUCCESS:     if finishing read the whole file.
  //   ZISYNC_ERROR_AGAIN: if more data requires.
  //   other error code:   if some error occurs.
  err_t WriteMore(struct bufferevent* bev,
                  ITarWriterDelegate* delegate);

 private:
  int64_t file_size_;
  OsFile file_;
  struct tar_header file_header_;
  std::string file_path_;
};

class TarWriter {
  enum WriteState {
    FILE_HEAD, FILE_DATA, FILE_DONE, FILE_ERROR
  };
 public:
  TarWriter(ITarWriterDelegate* delegate,
            ITarWriterDataSource* data_source);
  virtual ~TarWriter();

  virtual err_t WriteSome(struct bufferevent* bev);

 private:
  enum WriteState write_state_;
  std::unique_ptr<FileTarWriter> file_writer_;

  std::string real_path_;
  std::string encode_path_;
  std::string alias_;
  
  ITarWriterDelegate* delegate_;
  ITarWriterDataSource* data_source_;
};


}  // namespace zs

#endif
