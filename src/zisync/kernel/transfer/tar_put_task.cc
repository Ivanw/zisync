/**
 * @file task_put_task.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Impmentation of tar format put task.
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

#include "zisync/kernel/platform/platform.h"  // NOLINT

#include <fcntl.h>
#include <libtar.h>
#include <openssl/err.h>
#include <ostream>
#include <istream>
#include <memory>

#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/transfer/tar_put_task.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/tree_status.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/transfer/tar_helper.h"

namespace zs {

TarPutTaskFactory::TarPutTaskFactory() {
  format_ = "tar";
}

TarPutTaskFactory::~TarPutTaskFactory() {
}

IPutTask* TarPutTaskFactory::CreateTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const std::string& remote_tree_uuid,
    const std::string& uri,
    SSL_CTX* ctx) {
  return new TarPutTask(monitor, local_tree_id, remote_tree_uuid, uri, ctx);
}

const std::string& TarPutTaskFactory::GetFormat() {
  return format_;
}

TarPutTask::TarPutTask() {
  ctx_ = NULL;
  total_size_ = 0;
}

TarPutTask::TarPutTask(ITaskMonitor* monitor,
                       const int32_t local_tree_id,
                       const std::string& remote_tree_uuid,
                       const std::string& uri,
                       SSL_CTX* ctx) {
  uri_ = uri;
  remote_tree_uuid_ = remote_tree_uuid;
  monitor_ = monitor;
  local_tree_id_ = local_tree_id;
  ctx_ = ctx;
  total_size_ = 0;
  task_id_ = GetTasksManager()->AddTask();
}

TarPutTask::~TarPutTask() {
  GetTasksManager()->SetTaskAbort(task_id_);
}

int TarPutTask::GetTaskId() {
  return task_id_;
}

err_t TarPutTask::AppendFile(
    const std::string& real_path,
    const std::string& encode_path,
    int64_t size) {
  real_path_vector_.push_back(real_path);
  encode_path_vector_.push_back(encode_path);
  if (encode_path != "/.zisync.meta") {
    size_list_.push_back(size);
    total_size_ += size;
  }
  return ZISYNC_SUCCESS;
}

err_t TarPutTask::AppendFile(
    const std::string& real_path,
    const std::string& encode_path,
    const std::string& signature,
    int64_t size) {
  real_path_vector_.push_back(real_path);
  encode_path_vector_.push_back(encode_path);
  signature_vector_.push_back(signature);
  size_list_.push_back(size);
  total_size_ += size;
  return ZISYNC_SUCCESS;
}

static int TarOpen(TAR *handle, void *usrdata) {
  std::ostream *out = static_cast<std::ostream *>(usrdata);
  handle->desp.fptr = out;

  return 0;
}

static int TarClose(TAR *handle) {
  if (handle->th_buf.gnu_longname != NULL)
    free(handle->th_buf.gnu_longname);
  if (handle->th_buf.gnu_longlink != NULL)
    free(handle->th_buf.gnu_longlink);
  return 0;
}

static ssize_t TarWrite(TAR *handle, const void *buffer, size_t length) {
  std::ostream *out = static_cast<std::ostream *>(handle->desp.fptr);
  out->write(static_cast<const char *>(buffer), static_cast<int>(length));
  if (out->fail()) {
    return -1;
  }

  return static_cast<ssize_t>(length);  // safe cast
}

err_t TarPutTask::SendPutHttpHeader(std::ostream& out) {
  std::string buffer;
  std::string local_tree_uuid =
      GetTransferServer()->GetTreeAgent()->GetTreeUuid(local_tree_id_);
  assert(!local_tree_uuid.empty());
  const char* http_header = "PUT tar HTTP/1.1\r\nZiSync-Remote-Tree-Uuid:%s\r\n"
      "ZiSync-Local-Tree-Uuid:%s\r\nZiSync-Total-Size:%" PRId64 "\r\n"
      "ZiSync-Total-Files:%d\r\n\r\n";
  int ret = StringFormat(
      &buffer, http_header, remote_tree_uuid_.c_str(), local_tree_uuid.c_str(),
      total_size_, size_list_.size());
  ZSLOG_INFO("Send http header: %s", buffer.c_str());
  assert(ret > 0);

  out.write(buffer.c_str(), buffer.length());
  if (out.fail()) {
    return ZISYNC_ERROR_TAR;
  }
  out.flush();
  if (out.fail()) {
    return ZISYNC_ERROR_TAR;
  }

  return  ZISYNC_SUCCESS;
}

err_t TarPutTask::Execute() {
  err_t zisync_ret = ZISYNC_SUCCESS;

  int32_t remote_tree_id = GetTransferServer()->GetTreeAgent()
      ->GetTreeId(remote_tree_uuid_);
  if (remote_tree_id == -1) {
    ZSLOG_ERROR("Remote tree(%s) does not exist", 
                remote_tree_uuid_.c_str());
    return ZISYNC_ERROR_TREE_NOENT;
  }

  std::unique_ptr<OsTcpSocket> socket(OsTcpSocketFactory::Create(uri_, ctx_));
  int new_value = g_socket_buffer_size;
  if (socket->SetSockOpt(
          SOL_SOCKET, SO_SNDBUF, &new_value, sizeof(int)) == -1) {
    ZSLOG_ERROR("Set socket os send buf length(256K) fail: %s", OsGetLastErr());
  }

  if (socket->Connect() != 0) {
    ZSLOG_ERROR("Put task connect fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  ITreeAgent* tree_agent = GetTransferServer()->GetTreeAgent();
  assert(tree_agent != NULL);
  TasksManager* tasks_manager = GetTasksManager();
  assert(tasks_manager != NULL);

  fdbuf fd_buf(socket.get());
  std::ostream out(&fd_buf);
  std::istream in(&fd_buf);

  TAR *handle = NULL;
  tartypex_t type = {
    &TarOpen, &TarClose, NULL, &TarWrite, NULL,
  };

  if (tar_open_raw(&handle, &type, TAR_GNU, O_RDONLY, &out) == -1) {
    return ZISYNC_ERROR_TAR;
  }
  TarCloseHelper tar_helper(handle);

  zisync_ret = SendPutHttpHeader(out);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Put task send http_header fail: %s", OsGetLastErr());
    return zisync_ret;
  }

  std::string meta_file("/");
  meta_file.append(SYNC_FILE_TASKS_META_FILE);

  StartSpeedHelper start_speed;
  for (size_t i = 0; i < real_path_vector_.size(); i++) {
    char* real_path = const_cast<char*>(real_path_vector_.at(i).c_str());
    char* encode_path = const_cast<char*>(encode_path_vector_.at(i).c_str());
    ZSLOG_INFO("Put file: %s", real_path);

    bool is_meta_file = (meta_file == encode_path) ? true : false;

    OsFileStat stat;
    if (OsStat(real_path_vector_.at(i), &stat) != 0) {
      if (!is_meta_file) {
        ZSLOG_INFO("Put task skip file(%s), length(%" PRId64 ")",
                   real_path, size_list_.at(i));
        monitor_->OnFileSkiped(1);
        monitor_->OnByteSkiped(size_list_.at(i));
      }
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
    OsFile file;
    if (!is_dir) {
      if (file.Open(real_path, "rb") != 0) {
        ZSLOG_ERROR("Put task open file(%s) fail: %s",
                    real_path, OsGetLastErr());
        ok = false;
      }
    }

    if (ok) {
      if (th_write(handle) != 0) {
        ZSLOG_ERROR("Put task write file(%s)tar header fail: %s",
                    encode_path, OsGetLastErr());
        ok = false;
      }
    }

    if (!ok) {
      if (!is_meta_file) {
        ZSLOG_INFO("Put task skip file(%s), length(%" PRId64 ")",
                   real_path, th_get_size(handle));
        monitor_->OnFileSkiped(1);
        monitor_->OnByteSkiped(size_list_.at(i));
      }
      continue;
    }

    if (!is_meta_file) {
      monitor_->OnFileTransfer(encode_path);
    }
    /* append regfile */
    if (!is_dir) {
      string buffer;

      for (int64_t i = stat.length; i > 0; i -= nbytes) {
        std::string relative_path(encode_path);
        if (!tree_agent->AllowPut(
                local_tree_id_, remote_tree_id, relative_path) ||
            tasks_manager->TaskAbort(task_id_)) {
          ZSLOG_INFO("Put task aborted.");
          return ZISYNC_ERROR_CANCEL;
        }

        buffer.resize(
            i > g_file_buffer_size ? g_file_buffer_size : i);
        if (file.Read(&buffer) != 1) {
          ZSLOG_ERROR(
              "tar_put_task: fread() fail:%s:%s", OsGetLastErr(), real_path);
          return ZISYNC_ERROR_OS_IO;
        }

        out.write(buffer, nbytes);
        if (out.fail()) {
          ZSLOG_ERROR("tar_put_task: socket write fail:%s:%s",
                      OsGetLastErr(), real_path);
          return ZISYNC_ERROR_OS_IO;
        }
        if (!is_meta_file) {
          monitor_->OnByteTransfered(nbytes);
        }
      }
    }

    if (!is_meta_file) {
      monitor_->OnFileTransfered(1);
    }
  }

  out.flush();
  if (out.fail()) {
    ZSLOG_ERROR("Ostream flush fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }

  if (socket->Shutdown("w") == -1) {
    ZSLOG_ERROR("Put task shutdown socket(\"w\") fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  std::string line;
  std::getline(in, line, '\n');

  if (in.fail()) {
    ZSLOG_ERROR("Put task recv http response fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  } else if (line.find("200") == std::string::npos) {
    ZSLOG_INFO("Put task refused by server.");
    return ZISYNC_ERROR_REFUSED;
  }

  return ZISYNC_SUCCESS;
}

}  // namespace zs

