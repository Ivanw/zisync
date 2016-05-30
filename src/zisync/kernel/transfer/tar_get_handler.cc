/**
 * @file tar_get_handler.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar get task handler implementation.
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
// We must put #include "platform.h" here, since libtar has to include 
// Windows.h too. but Winsock2.h must be include before Windows.h
#include "zisync/kernel/platform/platform.h"

#include <libtar.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include "zisync/kernel/transfer/transfer.pb.h"
#pragma warning(pop)
#else
#include "zisync/kernel/transfer/transfer.pb.h"
#endif


#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/std_file.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/transfer/tar_get_handler.h"
#include "zisync/kernel/status.h"
#include "zisync/kernel/transfer/tar_helper.h"

namespace zs {

static int TarOpen(TAR* handle, void* usrdata) {
  std::ostream* out = static_cast<std::ostream*>(usrdata);
  handle->desp.fptr = out;
  return 0;
}

static ssize_t TarWrite(TAR* handle, const void* buffer, size_t length) {
  IAbortable* abortable = static_cast<IAbortable*>(handle->type->old);
  if (abortable->IsAborted()) {
    return -1;
  }

  std::ostream* out = static_cast<std::ostream*>(handle->desp.fptr);
  out->write(static_cast<const char*>(buffer), static_cast<int>(length));
  if (out->fail()) {
    return -1;
  }

  return static_cast<ssize_t>(length);  // safe cast
}

static int TarClose(TAR* handle) {
  if (handle->th_buf.gnu_longname != NULL)
    free(handle->th_buf.gnu_longname);
  if (handle->th_buf.gnu_longlink != NULL)
    free(handle->th_buf.gnu_longlink);
  return 0;
}

ITaskHandler* TarGetHandlerFactory::CreateTaskHandler() {
  return new TarGetHandler;
}

static inline void SendErrorResponse(std::ostream& out, int error_code) {
  switch (error_code) {
    case 400:
      out.write("HTTP/1.1 400 Bad Request\r\n\r\n", 28);
      break;
    case 401:
      out.write("HTTP/1.1 401 unsupported\r\n\r\n", 28);
      break;
    case 500:
      out.write("HTTP/1.1 500 Internal Server Error\r\n\r\n", 28);
      break;
    default:
      ZSLOG_ERROR("Set invalid http error code.");
  }
}

err_t TarGetHandler::OnHandleTask(
    IAbortable* abortable, const std::string& local_tree_uuid,
    const std::string &remote_tree_uuid,
    std::istream& in, std::ostream& out, ITaskMonitor* monitor) {

  TarGetFileList message;
  bool ret = message.ParseFromIstream(&in);
  assert(ret == true);

  ITransferServer* server = GetTransferServer();
  assert(server != NULL);

  ITreeAgent* tree_agent = server->GetTreeAgent();
  assert(tree_agent != NULL);
  int32_t local_tree_id = tree_agent->GetTreeId(local_tree_uuid);
  if (local_tree_id == -1) {
    ZSLOG_ERROR("Have no tree(%s).", local_tree_uuid.c_str());
    SendErrorResponse(out, 400);
    return ZISYNC_ERROR_TREE_NOENT;
  }
  
  int32_t remote_tree_id;
  if (remote_tree_uuid.empty()) {
    remote_tree_id = NULL_TREE_ID;
  } else {
    remote_tree_id = tree_agent->GetTreeId(remote_tree_uuid);
    if (remote_tree_id == -1) {
      ZSLOG_ERROR("Have no tree(%s).", remote_tree_uuid.c_str());
      SendErrorResponse(out, 400);
      return ZISYNC_ERROR_TREE_NOENT;
    }
  }

  std::string tree_root = tree_agent->GetTreeRoot(local_tree_uuid);
  if (tree_root.empty() || !OsDirExists(tree_root)) {
    ZSLOG_ERROR("Transfer Worker get tree root fail by tree_uuid: %s",
                local_tree_uuid.c_str());
    SendErrorResponse(out, 400);
    return ZISYNC_ERROR_GETTREEROOT;
  }

  tartypex_t type = {
    &TarOpen, &TarClose, NULL, &TarWrite, NULL, abortable,
  };

  TAR* handle = NULL;
  if (tar_open_raw(&handle, &type, TAR_GNU, O_WRONLY, &out) == -1) {
    ZSLOG_ERROR("Transfer Worker tar open raw fail");
    return ZISYNC_ERROR_TAR;
  }
  TarCloseHelper tar_help(handle);

  out.write("HTTP/1.1 200 OK\r\n\r\n", 19);
  if (out.fail()) {
    ZSLOG_ERROR("Write http response fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_TAR;
  }

  StartSpeedHelper start_speed;
  ZSLOG_INFO("Get handler start transfer files");
  for (int i = 0; i < message.relative_paths_size(); i++) {
    std::string relative_path = tree_root;
    int ret = OsPathAppend(&relative_path, message.relative_paths(i));
    assert(ret == 0);

    char* real_path = const_cast<char*>(relative_path.c_str());
    char* encode_path = const_cast<char*>(message.relative_paths(i).c_str());
    ZSLOG_INFO("Get handler transfer file: %s", real_path);
    OsFileStat stat;
    if (OsStat(relative_path, &stat) != 0) {
      monitor->OnFileSkiped(1);
      continue;
    }

    memset(&(handle->th_buf), 0, sizeof(struct tar_header));

    bool is_dir = (stat.type == OS_FILE_TYPE_DIR);
    th_set_mode(handle, stat.attr);
    if (is_dir) {
      // set dir type of handle, since th_set_mode can not
      // handle dir correctly
      handle->th_buf.typeflag = DIRTYPE;
    }

    th_set_size(handle, is_dir ? 0 : stat.length);
    th_set_path(handle, encode_path ? encode_path : real_path);

    bool ok = true;
    // try open file
    StdFile file;
    if (!is_dir) {
      if (file.Open(real_path, "rb") != 0) {
        ZSLOG_ERROR("Get handler open file(%s) fail: %s",
                    real_path, OsGetLastErr());
        ok = false;
      }
    }

    if (ok) {
      if (th_write(handle) != 0) {
        ZSLOG_ERROR("Get handler write file(%s)tar header fail: %s",
                    encode_path, OsGetLastErr());
        ok = false;
      }
    }

    if (!ok) {
      ZSLOG_INFO("Get handler skip file(%s), length(%" PRId64 ")",
                 real_path, th_get_size(handle));
      monitor->OnFileSkiped(1);
      continue;
    }

    monitor->OnFileTransfer(encode_path);

    /* append regfile */
    if (!is_dir) {
      int nbytes;
      char buffer[g_file_buffer_size];

      for (int64_t i = stat.length; i > 0; i -= nbytes) {
        if (abortable->IsAborted() ||
            !tree_agent->AllowPut(
                local_tree_id, remote_tree_id, encode_path)) {
          ZSLOG_INFO("Get handler aborted.");
          return ZISYNC_ERROR_CANCEL;
        }

        nbytes =
            i > g_file_buffer_size ? g_file_buffer_size : static_cast<int>(i);
        if (file.Read(&buffer, nbytes, 1) != 1) {
          ZSLOG_ERROR(
              "Get handler: fread() fail:%s:%s", OsGetLastErr(), real_path);
          return ZISYNC_ERROR_OS_IO;
        }

        out.write(buffer, nbytes);
        if (out.fail()) {
          ZSLOG_ERROR("Get handler: socket write fail:%s:%s",
                      OsGetLastErr(), real_path);
          return ZISYNC_ERROR_OS_IO;
        }
        monitor->OnByteTransfered(nbytes);
      }
    }
    monitor->OnFileTransfered(1);
  }
  out.flush();
  if (out.fail()) {
    ZSLOG_ERROR("Ostream flush fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  return zs::ZISYNC_SUCCESS;
}

}  // namespace zs
