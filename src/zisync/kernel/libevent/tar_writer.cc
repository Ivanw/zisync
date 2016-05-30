/**
 * @file tar_writer.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Write A file as tar segment.
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
#include "zisync/kernel/platform/platform.h"

#include <assert.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <stdio.h>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/libtar++.h"
#include "zisync/kernel/libevent/tar_writer.h"


namespace zs {

FileTarWriter::FileTarWriter()
    : file_size_(0) {
  memset(&file_header_, 0, sizeof(struct tar_header));
}

FileTarWriter::~FileTarWriter() {}

err_t FileTarWriter::Reset(const std::string& real_path,
                           const std::string& alias,
                           const std::string& encode_path) {

  file_.Close();
  int ret = file_.Open(real_path.c_str(), alias, "rb");
  if (ret != 0) {
    ZSLOG_ERROR("Open file(%s) fail: %s",
                real_path.c_str(), OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  if (file_header_.gnu_longname != NULL) {
    free(file_header_.gnu_longname);
    file_header_.gnu_longname = NULL;
  }
  memset(&file_header_, 0, sizeof(struct tar_header));
  OsFileStat stat;
  if (OsStat(real_path, alias, &stat) != 0) {
    ZSLOG_ERROR(
        "Get file(%s) attr fail: %s", real_path.c_str(), OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }

  assert(stat.type == OS_FILE_TYPE_REG);
  file_header_.typeflag = REGTYPE;
  th_set_mode(&file_header_, stat.attr);
  th_set_size(&file_header_, stat.length);
  th_set_path(&file_header_, const_cast<char*>(encode_path.c_str()));

  file_size_ = stat.length;
  file_path_ = real_path;

  return ZISYNC_SUCCESS;
}

void FileTarWriter::WriteHead(struct bufferevent* bev) {
  assert(bev != NULL);
  int ret = 0;

  if (file_header_.gnu_longname != NULL) {
    char temp_type = file_header_.typeflag;

    file_header_.typeflag = GNU_LONGNAME_TYPE;
    int longname_length = strlen(file_header_.gnu_longname);
    th_set_size(&file_header_, longname_length);
    th_finish(&file_header_);

    ret = bufferevent_write(bev, &file_header_, T_BLOCKSIZE);
    assert(ret == 0);

    ret = bufferevent_write(bev, file_header_.gnu_longname, longname_length);
    assert(ret == 0);

    char buffer[T_BLOCKSIZE] = {0};
    int append_bytes = T_BLOCKSIZE - longname_length % T_BLOCKSIZE;
    ret = bufferevent_write(bev, buffer, append_bytes);
    assert(ret == 0);

    file_header_.typeflag = temp_type;
    th_set_size(&file_header_, file_size_);
  }
  th_finish(&file_header_);

  ret = bufferevent_write(bev, &file_header_, T_BLOCKSIZE);
  assert(ret == 0);
}

err_t FileTarWriter::WriteMore(struct bufferevent* bev, ITarWriterDelegate* delegate) {
  assert(bev != NULL);
  if (file_size_ == 0) {
    return ZISYNC_SUCCESS;
  }

  size_t length = file_size_ > 4096 ? 4096 : file_size_;
  struct evbuffer *output = bufferevent_get_output(bev);
  struct evbuffer_iovec vec[2];
  int n = evbuffer_reserve_space(output, length, vec, 2);
  if (n <= 0) {
    ZSLOG_ERROR("Reserve space of evbuffer fail.");
    return ZISYNC_ERROR_LIBEVENT;
  }

  size_t bytes_to_add = length;
  int i;
  for (i = 0; i < n && bytes_to_add > 0; i++) {
    size_t iov_len = bytes_to_add < vec[i].iov_len ? bytes_to_add : vec[i].iov_len;
    if (file_.Read(static_cast<char*>(vec[i].iov_base), iov_len) != iov_len) {
      ZSLOG_ERROR("Read file(%s) fail: %s", file_path_.c_str(), OsGetLastErr());
      return ZISYNC_ERROR_OS_IO;
    }
    vec[i].iov_len = iov_len;
    bytes_to_add -= iov_len;
  }

  if (evbuffer_commit_space(output, vec, i) < 0) {
    ZSLOG_ERROR("Evbuffer commit file(%s) content fail.", file_path_.c_str());
    return ZISYNC_ERROR_LIBEVENT;
  }

  file_size_ -= length;

  if (delegate && length) {
    delegate->OnByteDidTransfered(length);
  }

  if (file_size_ == 0) {
    file_.Close();
    return ZISYNC_SUCCESS;
  }

  return ZISYNC_ERROR_AGAIN;
}

TarWriter::TarWriter(ITarWriterDelegate* delegate,
            ITarWriterDataSource* data_source)
    : write_state_(FILE_HEAD)
    , delegate_(delegate)
    , data_source_(data_source) {
  file_writer_.reset(new FileTarWriter);
}
TarWriter::~TarWriter() {
  
}

err_t TarWriter::WriteSome(struct bufferevent* bev) {
  err_t rc = ZISYNC_SUCCESS;

  struct evbuffer* buffer = bufferevent_get_output(bev);
  if (write_state_ == FILE_HEAD) {
    int64_t size;
    if (!data_source_->EnumNext(&real_path_, &encode_path_, &alias_, &size)) {
      return ZISYNC_SUCCESS;
    }

    rc = file_writer_->Reset(real_path_, alias_, encode_path_); 
      if (rc != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("failed to open file: %s", real_path_.c_str());
        //
        // Simple ignore failed file
        //
        if (delegate_) {
          delegate_->OnFileDidSkiped(real_path_, encode_path_);
          delegate_->OnByteDidSkiped(size);
        }
        return ZISYNC_ERROR_AGAIN;
      }

      if (delegate_) {
        delegate_->OnFileWillTransfer(real_path_, encode_path_);
      }
      
      write_state_ = FILE_DATA;
      file_writer_->WriteHead(bev);
      assert(evbuffer_get_length(buffer) > 0);
      return ZISYNC_ERROR_AGAIN;
  }

  if (write_state_ == FILE_DATA) {
    // process file data
    rc = file_writer_->WriteMore(bev, delegate_);
    if (rc == ZISYNC_ERROR_AGAIN) {
      assert(evbuffer_get_length(buffer) > 0);
      return ZISYNC_ERROR_AGAIN;
    }

    if (rc == ZISYNC_SUCCESS) {
      if (delegate_) {
        delegate_->OnFileDidTransfered(real_path_, encode_path_);
      }
      write_state_ = FILE_HEAD;
      return ZISYNC_ERROR_AGAIN;
    } else {
      write_state_ = FILE_ERROR;
      return rc;
    }
  }

  // Should never reach here
  assert(false);
  return ZISYNC_ERROR_GENERAL;
}


}  // namespace zs
