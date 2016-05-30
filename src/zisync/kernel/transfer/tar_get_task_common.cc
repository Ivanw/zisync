// Copyright 2014, zisync.com

#include "zisync/kernel/platform/platform.h"

#include <libtar.h>
#include <cassert>
#include <string>
#include <iostream>

#include <fcntl.h>
#ifdef _MSC_VER
#include<libtar_internal.h>
#else
#include <libgen.h>
#endif

#include "zisync/kernel/transfer/tar_get_task_common.h"
#include "zisync/kernel/utils/std_file.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/transfer/transfer_server.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/transfer/tar_helper.h"

namespace zs {

/* class TarGetTask */
static int TarOpen(TAR* handle, void* usrdata) {
  std::istream* in = static_cast<std::istream*>(usrdata);
  handle->desp.fptr = in;
  return 0;
}

static int TarClose(TAR* handle) {
  if (handle->th_buf.gnu_longname != NULL)
    free(handle->th_buf.gnu_longname);
  if (handle->th_buf.gnu_longlink != NULL)
    free(handle->th_buf.gnu_longlink);

  return 0;  // nothing need to do
}

static ssize_t TarRead(TAR* handle, void* buffer, size_t length) {
  std::istream* in = static_cast<std::istream*>(handle->desp.fptr);
  in->read(static_cast<char*>(buffer), length);
  if (in->fail()) {
    return -1;
  }

  return static_cast<ssize_t>(in->gcount());
}

err_t TarGetTaskCommon::Execute() {
  some_download_fail = false;
  std::unique_ptr<OsTcpSocket> socket(OsTcpSocketFactory::Create(uri_, ctx_));

  if (socket.get() == NULL) {
    ZSLOG_ERROR("Create socket(%s) fail : %s", uri_.c_str(),
                OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  int new_value = g_socket_buffer_size;
  if (socket->SetSockOpt(
          SOL_SOCKET, SO_RCVBUF, &new_value, sizeof(int)) == -1) {
    ZSLOG_ERROR("Set socket os recv buf length(256K) fail: %s", OsGetLastErr());
  }

  if (socket->Connect() == -1) {
    ZSLOG_ERROR("Socket(%s) connect fail: %s", uri_.c_str(), OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  fdbuf fd_buf(socket.get());
  std::ostream out(&fd_buf);
  std::istream in(&fd_buf);

  err_t zisync_ret = SendGetHttpHeader(&out);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Get task send http header fail: %s", OsGetLastErr());
    return zisync_ret;
  }

  zisync_ret = SendRelativePathsProtobuf(&out);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Get task send protobuf stream fail: %s", OsGetLastErr());
    return zisync_ret;
  }
  out.flush();

  if (socket->Shutdown("w") == -1) {
    return ZISYNC_ERROR_OS_SOCKET;
  }

  // Read first line and check 200.
  std::string line;
  std::getline(in, line, '\n');
  if (line.length() > 0) {
    if (line.find("200") == std::string::npos) {
      ZSLOG_ERROR("Get task recv http response fail: %s", OsGetLastErr());
      return ZISYNC_ERROR_REFUSED;
    }
  } else {
    ZSLOG_ERROR("Get task recv http response fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  // Skip all headers
  while (!in.fail()) {
    std::getline(in, line, '\n');
    if (line == "\r") {
      break;
    }
  }

  TAR *tar = NULL;
  tartypex_t type = {
    &TarOpen, &TarClose, &TarRead, NULL, NULL,
  };

  if (tar_open_raw(&tar, &type, TAR_GNU, O_RDONLY, &in) == -1) {
    return ZISYNC_ERROR_TAR;
  }
  TarCloseHelper tar_helper(tar);

  ITreeAgent* tree_agent = GetTransferServer()->GetTreeAgent();
  assert(tree_agent != NULL);
  TasksManager* task_manager = GetTasksManager();
  assert(task_manager != 0);

  std::string filename; char namebuf[MAX_PATH];

  StartSpeedHelper start_speed;
  while (th_read(tar) == 0) {
    filename = th_get_pathname(tar, namebuf, MAX_PATH);
    assert(!filename.empty());
    ZSLOG_INFO("Get file: %s", filename.c_str());

    std::string path = GetTargetPath(filename);
    monitor_->OnFileTransfer(filename);

    if (TH_ISDIR(tar)) {
      tar_extract_dir(tar, const_cast<char*>(path.c_str()));
      monitor_->OnFileTransfered(1);
      continue;
    }

    assert(TH_ISREG(tar));
    if (TH_ISREG(tar)) {
      if (OsCreateDirectory(OsDirName(path), true) != 0) {
        ZSLOG_ERROR("PepareFileParentDir for (%s) fail", path.c_str());
        zisync_ret = ZISYNC_ERROR_OS_IO;
      }
      StdFile file;
      if (zisync_ret == ZISYNC_SUCCESS) {
        if (file.Open(path, "wb") != 0) {
          zisync_ret = ZISYNC_ERROR_OS_IO;
          ZSLOG_ERROR(
              "Get task open file(%s) fail: %s", path.c_str(), OsGetLastErr());
        }
      }

      char buffer[g_file_buffer_size];
      bool is_write_ok = true;
      size_t nbytes = 0;
      for (int64_t i = th_get_size(tar); i > 0; i -= nbytes) {
        if (!AllowPut(filename) || task_manager->TaskAbort(task_id_)) {
          ZSLOG_INFO("Get task aborted.");
          return ZISYNC_ERROR_CANCEL;
        }

        nbytes =
            i > g_file_buffer_size ? g_file_buffer_size : static_cast<int>(i);
        in.read(buffer, nbytes);
        if (in.fail()) {
          zisync_ret = ZISYNC_ERROR_TAR;
          ZSLOG_ERROR("Get task read file content fail: %s", OsGetLastErr());
          in.clear();
          break;
        }

        nbytes = in.gcount();
        monitor_->OnByteTransfered(nbytes);

        /* write block to output file */
        if (file.IsOk() && is_write_ok) {
          if (file.Write(buffer, nbytes, 1) != 1) {
            if (errno == ENOSPC) {
              ZSLOG_ERROR("Disk has no space to write data.");
              return ZISYNC_ERROR_TAR;
            } else {
              is_write_ok = false;
              zisync_ret = ZISYNC_ERROR_TAR;
              ZSLOG_ERROR("Transfer Worker fwrite fail.");
            }
          }
        }
      }

      if (zisync_ret == ZISYNC_SUCCESS) {
        monitor_->OnFileTransfered(1);
      } else {
        some_download_fail = true;
        monitor_->OnFileSkiped(1);
      }
    } else {
      some_download_fail = true;
      ZSLOG_ERROR("encounted unsupported file types in tar stream.");
    }
  }

  return ZISYNC_SUCCESS;
}

}  // namespace zs
