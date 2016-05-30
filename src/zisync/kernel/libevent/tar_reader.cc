/**
 * @file tar_reader.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar read implementation.
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

#include <stdlib.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <assert.h>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/libevent/libtar++.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/transfer.h"

#define BIT_ISSET(bitmask, bit) ((bitmask) & (bit))

namespace zs {

void ShowHeader(struct tar_header *file_header) {
          ZSLOG_ERROR("Tar header:\n"
                      "name       = \"%.100s\"\n"
                      "mode       = \"%.8s\"\n"
                      "uid        = \"%.8s\"\n"
                      "gid        = \"%.8s\"\n"
                      "size       = \"%.12s\"\n"
                      "mtime      = \"%.12s\"\n"
                      "chksum     = \"%.8s\"\n"
                      "typeflag   = \'%c\'\n"
                      "linkname   = \"%.100s\"\n"
                      "magic      = \"%.6s\"\n"
                      "version[0] = \'%c\', version[1] = \'%c\'\n"
                      "uname      = \"%.32s\"\n"
                      "gname      = \"%.32s\"\n"
                      "devmajor   = \"%.8s\"\n"
                      "devminor   = \"%.8s\"\n"
                      "prefix     = \"%.155s\"\n"
                      "padding    = \"%.1s\"\n",
                      file_header->name, file_header->mode, file_header->uid,
                      file_header->gid, file_header->size, file_header->mtime,
                      file_header->chksum, file_header->typeflag,
                      file_header->linkname, file_header->magic,
                      file_header->version[0], file_header->version[1],
                      file_header->uname, file_header->gname, file_header->devmajor,
                      file_header->devminor, file_header->prefix,
                      file_header->padding);
}

void FileHeadTarReader::Reset() {
  if (file_header_.gnu_longname != NULL) {
    free(file_header_.gnu_longname);
    file_header_.gnu_longname = NULL;
  }
  memset(&file_header_, 0, sizeof(struct tar_header));

  parse_state_ = PS_PARSE_TAR_HEADER_BEGIN;
}

err_t FileHeadTarReader::ParseMore(struct bufferevent* bev) {
  assert(bev != NULL);
  struct evbuffer *input = bufferevent_get_input(bev);
  assert(input != NULL);

  while (parse_state_ != PS_PARSE_TAR_HEADER_END) {
    if (evbuffer_get_length(input) < T_BLOCKSIZE) {
      return ZISYNC_ERROR_AGAIN;
    }
    int nbytes = evbuffer_remove(input, &file_header_, T_BLOCKSIZE);
    assert(nbytes == T_BLOCKSIZE);

    switch (parse_state_) {
      case PS_PARSE_TAR_HEADER_BEGIN:

        if (!th_crc_ok(&file_header_)) {
          ZSLOG_ERROR("Read file header fail.");
          ShowHeader(&file_header_);
          return ZISYNC_ERROR_TAR;
        }
        if (TH_ISLONGNAME(&file_header_)) {
          int64_t size = th_get_size(&file_header_);
          longname_length_ = (int) ((size / T_BLOCKSIZE) +
                              (size % T_BLOCKSIZE ? 1 : 0)) * T_BLOCKSIZE;
          file_header_.gnu_longname = (char*)malloc(longname_length_);
          assert(file_header_.gnu_longname != NULL);
          longname_ptr_ = file_header_.gnu_longname;
          parse_state_ = PS_PARSE_TAR_HEADER_LONGNAME;
        } else {
          parse_state_ = PS_PARSE_TAR_HEADER_END;
        }
        break;

      case PS_PARSE_TAR_HEADER_LONGNAME:
        memcpy(longname_ptr_, &file_header_, T_BLOCKSIZE);
        longname_length_ -= T_BLOCKSIZE;
        if (longname_length_ == 0) {
          parse_state_ = PS_PARSE_TAR_HEADER_READ_HEADER;
        } else {
          longname_ptr_ += T_BLOCKSIZE;
        }
        break;

      case PS_PARSE_TAR_HEADER_READ_HEADER:
        if (!th_crc_ok(&file_header_)) {
          ZSLOG_ERROR("Read file header fail.");
          ShowHeader(&file_header_);
          return ZISYNC_ERROR_TAR;
        } else {
          parse_state_ = PS_PARSE_TAR_HEADER_END;
        }

      default:
        // PS_PARSE_TAR_HEADER_END
        break;
    }
  }

  return ZISYNC_SUCCESS;
}

err_t FileHeadTarReader::OnEOF(struct bufferevent* bev) {
  assert(bev != NULL);

  err_t rc = ParseMore(bev);
  if (rc != ZISYNC_ERROR_AGAIN) {
    return rc;
  }
  
  if (parse_state_ == PS_PARSE_TAR_HEADER_BEGIN
      && evbuffer_get_length(bufferevent_get_input(bev)) == 0) {
    return ZISYNC_SUCCESS;
  }

  return ZISYNC_ERROR_TAR;
}

err_t FileDataTarReader::Reset(
    const FileHeadTarReader& file_head, const std::string& real_path,
    const string &alias) {
  int ret = 0;
  assert(file_head.IsReg());
  real_path_ = real_path;
  encode_path_ = file_head.FileName();
  if (OsCreateDirectory(OsDirName(real_path_), true) != 0) {
    ZSLOG_ERROR(
        "Mkdirhier(%s) fail: %s", real_path_.c_str(), OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }

  file_.Close();

  file_size_ = file_head.FileSize();
  assert(file_size_ >= 0);
  ret = SHA1_Init(&c_);
  assert(ret == 1);

  ret = file_.Open(real_path_, alias, "wb");
  if (ret != 0) {
    ZSLOG_WARNING(
        "Open file(%s) fail: %s. Try Skip.", real_path_.c_str(), OsGetLastErr());
    // return ZISYNC_ERROR_OS_IO;
    ok_ = false;
  } else {
    ok_ = true;
  }

  return ZISYNC_SUCCESS;
}

err_t FileDataTarReader::ParseMore(
    struct bufferevent* bev, ITarParserDelegate* delegate) {
  assert(bev != NULL);
  int ret = 0;
  int length = 0;
  struct evbuffer_iovec v;

  struct evbuffer *input = bufferevent_get_input(bev);
  assert(input != NULL);

  while (file_size_ > 0) {
    if (evbuffer_get_length(input) <= 0) {
      return ZISYNC_ERROR_AGAIN;
    }
    
    if (evbuffer_peek(input, -1, NULL, &v, 1) != 1) {
      assert(0);
      return ZISYNC_ERROR_AGAIN;
    }

    length = (int)(file_size_ < (int64_t) v.iov_len ? file_size_ : v.iov_len);
    assert(length > 0);

    if (ok_) {
       if (static_cast<int>(file_.Write(
                               static_cast<char*>(v.iov_base), length)) != length) {
          if (errno == ENOSPC ||
              errno == EAGAIN) {
             ZSLOG_ERROR("Disk has no space to write data.");
             return ZISYNC_ERROR_OS_IO;
          } else {
             ZSLOG_ERROR("Write data to file(%s) fail: %s",
                         real_path_.c_str(), OsGetLastErr());
             assert(0);
             // return ZISYNC_ERROR_OS_IO;   // NOTE: Do not return
          }
       } else {
          ret = SHA1_Update(&c_, v.iov_base, length);
          assert(ret == 1);
       }
    }

    file_size_ -= length;
    assert(file_size_ >= 0);
    ret = evbuffer_drain(input, length);
    assert(ret == 0);

    if (delegate) {
      delegate->OnByteDidTransfered(real_path_, encode_path_, length);
    }
  }
  
  file_.Close();

  unsigned char sha1[20] = {0};
  ret = SHA1_Final(sha1, &c_);
  assert(ret == 1);
  Sha1MdToHex(sha1, &sha1_);

  return ZISYNC_SUCCESS;
}

err_t FileDataTarReader::OnEOF(struct bufferevent* bev,
                               ITarParserDelegate* delegate) {
  assert(bev != NULL);

  err_t rc = ParseMore(bev, delegate);
  if (rc == ZISYNC_ERROR_AGAIN) {
    ZSLOG_ERROR("uncompleted tar data recevied.");
    return ZISYNC_ERROR_TAR;
  }
  
  return rc;
}

TarParser::TarParser(ITarParserDataSource* data_source,
                     ITarParserDelegate* delegate)
    : data_source_(data_source)
    , delegate_(delegate)
    , parse_state_(FILE_HEAD) {
  file_head_.reset(new FileHeadTarReader);
  file_data_.reset(new FileDataTarReader);
}
TarParser::~TarParser() {
}

err_t TarParser::ParseMore(struct bufferevent* bev) {
  err_t rc = ZISYNC_SUCCESS;

  while (parse_state_ != FILE_ERROR && parse_state_ != FILE_DONE) {
    if (parse_state_ == FILE_HEAD) {
        // process http header
        rc = file_head_->ParseMore(bev);
        if (rc == ZISYNC_ERROR_AGAIN) {
          return rc;
        }
        
        if (rc == ZISYNC_SUCCESS) {
          std::string encode_path = file_head_->FileName();
          std::string real_path = data_source_->GetRealPath(encode_path);
          std::string alias = data_source_->GetAlias(encode_path);

          if (delegate_) {
            delegate_->OnFileWillTransfer(real_path, encode_path);
          }
          
          rc = file_data_->Reset(*file_head_, real_path, alias);
          if (rc == ZISYNC_SUCCESS) {
            parse_state_ = FILE_DATA;
          } else {
            parse_state_ = FILE_ERROR;
          }
        } else {
          parse_state_ = FILE_ERROR;
        }
    }
    else if (parse_state_ ==  FILE_DATA) {
      // process http body
      rc = file_data_->ParseMore(bev, delegate_);
      if (rc == ZISYNC_ERROR_AGAIN) {
        return rc;
      }

      if (rc == ZISYNC_SUCCESS) {

        if (delegate_) {
          std::string encode_path = file_head_->FileName();
          std::string real_path = data_source_->GetRealPath(encode_path);
          if (file_data_->IsOk()) {
             delegate_->OnFileDidTransfered(
                real_path, encode_path, file_data_->sha1());
          } else {
             delegate_->OnFileDidSkiped(real_path, encode_path);
          }
        }

        file_head_->Reset();
        parse_state_ = FILE_HEAD;
      } else {
        parse_state_ = FILE_ERROR;
      }
    }
  }

  if (parse_state_ == FILE_DONE) {
    return ZISYNC_SUCCESS;
  }
  
  return rc;
}

err_t TarParser::OnEOF(struct bufferevent* bev) {
  err_t rc = ParseMore(bev);
  if (rc != ZISYNC_ERROR_AGAIN) {
    return ZISYNC_ERROR_INVALID_MSG;
  }
  
  if (parse_state_ == FILE_HEAD) {
    rc = file_head_->OnEOF(bev);
    if (rc == ZISYNC_SUCCESS) {
      parse_state_ = FILE_DONE;
    } else {
      parse_state_ = FILE_ERROR;
    }
    return rc;
  }

  if (parse_state_ == FILE_DATA) {
    rc = file_data_->OnEOF(bev, delegate_);
    if (rc == ZISYNC_SUCCESS) {
      parse_state_ = FILE_DONE;
    } else {
      parse_state_ = FILE_ERROR;
    }
    return rc;
  } else {
    assert(false);
    return ZISYNC_ERROR_GENERAL;
  }
}

}  // namespace zs
