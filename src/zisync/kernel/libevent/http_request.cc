/**
 * @file http_request.cc
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
#include "zisync/kernel/platform/platform.h"

#include <assert.h>
#include <event2/buffer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/http_request.h"
#include "zisync/kernel/libevent/http_response.h"

namespace zs {

err_t HttpRequestHead::OnRead(struct bufferevent* bev) {
  int ret;
  char method[4] = {0};
  char format[40] = {0};
  char key[30] = {0};
  char val[256] = {0};
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
        } else {
          ret = sscanf(line.get(), "%4[^ ] %40s", method, format);
          assert(ret == 2);
          if (ret != 2) {
            ZSLOG_ERROR("Invaild Http request line: %s.", line.get());
            return ZISYNC_ERROR_HTTP_RETURN_ERROR;
          } else {
            method_ = method;
            format_ = format;
            parse_state_ = PS_PARSE_HEAD;
          }
        }

        break;

      case PS_PARSE_HEAD:
        if (*line == '\0') {
          parse_state_ = PS_PARSE_END;
          return ZISYNC_SUCCESS;
        } else {
          ret = sscanf(line.get(), "%30[^:]:%256s", key, val);
          assert(ret > 0);

          if (memcmp(key, "ZiSync-Remote-Tree-Uuid", 23) == 0 ||
              memcmp(key, "ZiSync-Tree-Uuid", 16) == 0) {
            remote_tree_uuid_ = val;
          } else if (memcmp(key, "ZiSync-Local-Tree-Uuid", 22) == 0) {
            local_tree_uuid_ = val;
          } else if (memcmp(key, "ZiSync-Total-Size", 17) == 0) {
            ret = sscanf(val, "%" PRId64, &total_bytes_);
            assert(ret > 0);
          } else if (memcmp(key, "ZiSync-Total-Files", 18) == 0) {
            ret = sscanf(val, "%d", &total_files_);
            assert(ret > 0);
          } else {
            ZSLOG_ERROR("Unknow http header, ignored: %s.", line.get());
          }
        }

        break;

      default:
        ZSLOG_ERROR("Ooops! We are consuming http body.");
        assert(0);
    }
  } while (1);
}

err_t HttpRequestHead::OnEOF(struct bufferevent* bev) {
  if (parse_state_ == PS_PARSE_END) {
    return ZISYNC_SUCCESS;
  }

  return ZISYNC_ERROR_INVALID_MSG;
}

TarGetRequestParser::TarGetRequestParser(
    const std::string& tree_root, const std::string &tree_uuid)
    : tree_root_(tree_root), tree_uuid_(tree_uuid)
    , buffer_(NULL, evbuffer_free) {
  buffer_.reset(evbuffer_new());
}

TarGetRequestParser::~TarGetRequestParser() {
}

err_t TarGetRequestParser::ParseMore(struct bufferevent* bev) {
  struct evbuffer* input = bufferevent_get_input(bev);
  // Move all data in input to buffer_
  evbuffer_remove_buffer(input, buffer_.get(), INT32_MAX);
  return ZISYNC_ERROR_AGAIN;
}

err_t TarGetRequestParser::OnEOF(struct bufferevent* bev) {
  ParseMore(bev);

  size_t size = evbuffer_get_length(buffer_.get());
  const char* data = (const char*)evbuffer_pullup(buffer_.get(), -1);
  
  if (file_list_.ParseFromArray(data, size)) {
    return ZISYNC_SUCCESS;
  } else {
    ZSLOG_ERROR("Failed to Parse TarGetFileList");
    return ZISYNC_ERROR_INVALID_MSG;
  }
}


IHttpResponseWriter* TarGetRequestParser::CreateResponse(HttpRequestHead* http_head) {
  return new TarGetResponseWriter(tree_root_, tree_uuid_, file_list_, http_head);
}

}  // namespace zs
