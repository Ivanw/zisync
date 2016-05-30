/**
 * @file http_response.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Impmentation of http response.
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

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <memory>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/tar_writer.h"
#include "zisync/kernel/libevent/http_request.h"
#include "zisync/kernel/libevent/http_response.h"

namespace zs {

err_t HttpResponseHead::ParseMore(struct bufferevent* bev) {
  int ret = 0;
  std::unique_ptr<char, decltype(free)*> line(NULL, free);

  assert(bev != NULL);
  struct evbuffer *input = bufferevent_get_input(bev);
  assert(input != NULL);

  do {
    line.reset(evbuffer_readln(input, NULL, EVBUFFER_EOL_CRLF));

    if (line == NULL) return ZISYNC_ERROR_AGAIN;

    switch (parse_state_) {
      case PS_PARSE_BEGIN:
        if (*line == '\0') {
          ZSLOG_ERROR("Read http header error.");
          return ZISYNC_ERROR_HTTP_RETURN_ERROR;
        }

        ret = sscanf(line.get(), "HTTP/1.1 %d ", &http_code_);
        assert(ret == 1);
        if (ret != 1) {
          ZSLOG_ERROR("Invaild Http request line: %s.", line.get());
          return ZISYNC_ERROR_HTTP_RETURN_ERROR;
        } 

        parse_state_ = PS_PARSE_HEAD;

        break;

      case PS_PARSE_HEAD:
        if (*line == '\0') {
          parse_state_ = PS_PARSE_END;
          return ZISYNC_SUCCESS;
        } else {
          ZSLOG_INFO("skip unknow header: %s", line.get());
        }

        break;

      default:
        ZSLOG_ERROR("Ooops! We are consuming http body.");
        assert(0);
    }
  } while (1);

}

err_t HttpResponseHead::OnEOF(struct bufferevent* bev) {
  if (parse_state_ == PS_PARSE_END) {
    return ZISYNC_SUCCESS;
  }

  return ZISYNC_ERROR_INVALID_MSG;
}


ErrorResponseWriter* ErrorResponseWriter::GetByErr(err_t eno) {
  switch (eno) {
    case ZISYNC_ERROR_GENERAL:
    case ZISYNC_ERROR_ADDRINUSE:
    case ZISYNC_ERROR_MEMORY:
    case ZISYNC_ERROR_OS_EVENT:
    case ZISYNC_ERROR_OS_THREAD:
    case ZISYNC_ERROR_LIBEVENT:
    case ZISYNC_ERROR_TIMEOUT:
    case ZISYNC_ERROR_OS_SOCKET:
    case ZISYNC_ERROR_NOT_STARTUP:
    case ZISYNC_ERROR_TAR:
    case ZISYNC_ERROR_OS_IO:
    case ZISYNC_ERROR_INVALID_PORT:
    case ZISYNC_ERROR_CANCEL:
      return new ErrorResponseWriter(500);
    case ZISYNC_ERROR_HTTP_RETURN_ERROR:
      return new ErrorResponseWriter(401);
    case ZISYNC_ERROR_INVALID_MSG:
      return new ErrorResponseWriter(400);

    default:
      assert(0);
      ZSLOG_ERROR("Invaild eno.");
      return NULL;
  }
}

void  ErrorResponseWriter::HeadWriteAll(struct bufferevent* bev) {
  struct evbuffer* output = bufferevent_get_output(bev);
  switch (http_code_) {
    case 400:
      evbuffer_add_printf(output, "HTTP/1.1 400 Bad Request\r\n");
      break;
    case 401:
      evbuffer_add_printf(output, "HTTP/1.1 401 unsupported\r\n");
      break;
    case 500:
      evbuffer_add_printf(output, "HTTP/1.1 500 Internal Server Error\r\n");
      break;
    default:
      ZSLOG_ERROR("Set invalid http error code.");
      evbuffer_add_printf(output, "HTTP/1.1 500 Internal Server Error\r\n");
  }
  evbuffer_add_printf(output, "\r\n");
}

err_t ErrorResponseWriter::BodyWriteSome(struct bufferevent* bev) {
  return ZISYNC_SUCCESS;
}

TarGetResponseWriter::TarGetResponseWriter(
    const std::string& tree_root, const std::string& tree_uuid, 
    const TarGetFileList& file_list, HttpRequestHead* head)
  : tree_root_(tree_root), tree_uuid_(tree_uuid),
    current_index_(0), file_list_(file_list) {
      body_writer_.reset(new TarWriter(this, this));

      ITreeAgent* tree_agent = GetTransferServer2()->GetTreeAgent();
      monitor_.reset(
          tree_agent->CreateTaskMonitor(
              TASK_TYPE_PUT,
              head->remote_tree_uuid(), // NOTE: this is local_tree_uuid
              head->local_tree_uuid(),  // NOTE: this is remote_tree_uuid
              head->total_files(),
              head->total_bytes()));

      std::string remote_tree_root =
          tree_agent->GetTreeRoot(head->local_tree_uuid());
      for (int i = 0; i < file_list_.relative_paths_size(); i++) {
        std::string local_path = tree_root_;
        std::string remote_path = remote_tree_root;

        const std::string &encode_path = file_list_.relative_paths(i);
        OsPathAppend(&local_path, encode_path);
        OsPathAppend(&remote_path, encode_path);

        OsFileStat st;
        int ret = OsStat(local_path, std::string(), &st);
        if (ret != 0) {
          ZSLOG_ERROR(
              "Get file(%s) stat fail: %s", local_path.c_str(), OsGetLastErr());
        } else {
          monitor_->AppendFile(local_path, remote_path, encode_path, st.length);
        }
      }
    }

TarGetResponseWriter::~TarGetResponseWriter() {

}

void  TarGetResponseWriter::HeadWriteAll(struct bufferevent* bev) {
  struct evbuffer* output = bufferevent_get_output(bev);
  evbuffer_add_printf(output, "HTTP/1.1 200 OK\r\n");
  evbuffer_add_printf(output, "\r\n");
}

err_t TarGetResponseWriter::BodyWriteSome(struct bufferevent* bev) {
  return body_writer_->WriteSome(bev);
}

bool TarGetResponseWriter::EnumNext(
    std::string* real_path, std::string* encode_path, 
    std::string* alias, int64_t* size) {
  if (current_index_ >= file_list_.relative_paths_size()) {
    return false;
  }

  //
  // we set size to 0, so that OnByteDidSkip(0) when failed to open
  // file. We do so because the actual file size may be different from
  // client expected, if we use actual file size to do skip will result
  // error in some special cause.
  //
  *size = 0; 
  *encode_path = file_list_.relative_paths(current_index_);
  *alias = zs::GetTransferServer2()->GetTreeAgent()->GetAlias(
      tree_uuid_, tree_root_, *encode_path);

  *real_path = tree_root_;
  OsPathAppend(real_path, *encode_path);

  ++current_index_;  
  return true;
}

void TarGetResponseWriter::OnFileWillTransfer(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("begin transfer file: %s", encode_path.c_str());
  if (monitor_) {
    monitor_->OnFileTransfer(encode_path);
  }
}

void TarGetResponseWriter::OnFileDidTransfered(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("end transfer file: %s", encode_path.c_str());
  if (monitor_) {
    monitor_->OnFileTransfered(1);
  }
}

void TarGetResponseWriter::OnFileDidSkiped(
    const std::string& real_path,
    const std::string& encode_path) {
  ZSLOG_INFO("end transfer file: %s", encode_path.c_str());
  if (monitor_) {
    monitor_->OnFileSkiped(1);
  }
}

void TarGetResponseWriter::OnByteDidTransfered(int32_t nbytes) {
  if (monitor_) {
    monitor_->OnByteTransfered(nbytes);
  }
}
void TarGetResponseWriter::OnByteDidSkiped(int32_t nbytes) {
  if (monitor_) {
    monitor_->OnByteSkiped(nbytes);
  }
}


ErrorResponseParser::ErrorResponseParser(int http_code) {
  ZSLOG_ERROR("http %d error", http_code);
}

}  // namespace zs
