/**
 * @file tar_put_handler.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar put task handler implementation.
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
#ifdef _MSC_VER
#include<libtar_internal.h>
#endif
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <ostream>
#include <istream>
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/transfer/tar_put_handler.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/transfer/transfer_server.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/status.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/transfer/tar_helper.h"

namespace zs {

static int TarOpen(TAR* handle, void* usrdata) {
  std::istream* in = static_cast<std::istream*>(usrdata);
  handle->desp.fptr = in;
  return 0;
}

static ssize_t TarRead(TAR* handle, void* buffer, size_t length) {
  IAbortable* abortable = static_cast<IAbortable*>(handle->type->old);
  if (abortable->IsAborted()) {
    return -1;
  }

  std::istream* in = static_cast<std::istream*>(handle->desp.fptr);
  in->read(static_cast<char*>(buffer), length);
  if (in->fail()) {
    return -1;
  }

  return static_cast<ssize_t>(in->gcount());
}

static int TarClose(TAR* handle) {
  if (handle->th_buf.gnu_longname != NULL)
    free(handle->th_buf.gnu_longname);
  if (handle->th_buf.gnu_longlink != NULL)
    free(handle->th_buf.gnu_longlink);

  return 0;
}

ITaskHandler* TarPutHandlerFactory::CreateTaskHandler() {
  return new TarPutHandler;
}

class TmpDir {
 public:
  explicit TmpDir(const std::string& tmp_path) {
    int rc = zs::OsCreateDirectory(tmp_path, false);
    if (rc != 0) {
      ZSLOG_ERROR("Create TmpDir(%s) fail : %s", tmp_path.c_str(),
                  OsGetLastErr());
      ok_ = false;
      return;
    }

    if (OsAddHiddenAttr(tmp_path.c_str()) != 0) {
      ZSLOG_ERROR("OsAddHiddenAttr(%s) fail : %s", tmp_path.c_str(),
                  OsGetLastErr());
      ok_ = false;
    } else {
      ok_ = true;
      path_ = tmp_path;
    }
  }

  bool IsOk() {
    return ok_;
  }

  const std::string& path() {
    return path_;
  }

  ~TmpDir() {
    int ret = OsDeleteDirectories(path_.c_str());
    if (ret != 0) {
      ZSLOG_ERROR("Delete tempdir(%s) fail: %s", path_.c_str(), OsGetLastErr());
    }
  }

 private:
  std::string path_;
  bool ok_;
};

err_t TarPutHandler::OnHandleTask(
    IAbortable* abortable, const std::string& local_tree_uuid,
    const std::string &remote_tree_uuid,
    std::istream& in, std::ostream& out, ITaskMonitor* monitor) {
  assert(abortable != NULL);
  assert(monitor != NULL);

  ITransferServer* server = GetTransferServer();
  assert(server != NULL);
  ITreeAgent* tree_agent = server->GetTreeAgent();
  assert(tree_agent != NULL);
  int32_t local_tree_id = tree_agent->GetTreeId(local_tree_uuid);
  if (local_tree_id == -1) {
    ZSLOG_ERROR("Have no tree(%s).", local_tree_uuid.c_str());
    return ZISYNC_ERROR_TREE_NOENT;
  }

  int32_t remote_tree_id;
  if (remote_tree_uuid.empty()) {
    remote_tree_id = NULL_TREE_ID;
  } else {
    remote_tree_id = tree_agent->GetTreeId(remote_tree_uuid);
    if (remote_tree_id == -1) {
      ZSLOG_ERROR("Have no tree(%s).", remote_tree_uuid.c_str());
      return ZISYNC_ERROR_TREE_NOENT;
    }
  }

  TmpDir tmpdir(tree_agent->GetNewTmpDir(local_tree_uuid));
  if (tmpdir.path().empty() || !tmpdir.IsOk()) {
    ZSLOG_ERROR("Create tmp_dir fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }

  tartypex_t type = {
    &TarOpen, &TarClose, &TarRead, NULL, NULL, abortable,
  };

  TAR* handle = NULL;
  if (tar_open_raw(&handle, &type, TAR_GNU, O_RDONLY, &in) == -1) {
    ZSLOG_ERROR("Transfer Worker tar open raw fail");
    return ZISYNC_ERROR_TAR;
  }
  TarCloseHelper tar_help(handle);
  /* tar extract all */
  char namebuf[MAX_PATH];
  std::string meta_file("/");
  meta_file.append(SYNC_FILE_TASKS_META_FILE);

  StartSpeedHelper start_speed;
  ZSLOG_INFO("Put handler start transfer files");
  while (th_read(handle) == 0) {
    err_t err = ZISYNC_SUCCESS;
    std::string path = tmpdir.path();
    std::string filename = th_get_pathname(handle, namebuf, MAX_PATH);
    ZSLOG_INFO("Put handler transfer file: %s", filename.c_str());
    bool is_meta_file = (filename == meta_file);

    OsPathAppend(&path, filename);

    if (TH_ISDIR(handle)) {
      tar_extract_dir(handle, const_cast<char*>(path.c_str()));
      monitor->OnFileTransfered(1);
      continue;
    }
    assert(TH_ISREG(handle));
    
    if (!is_meta_file) {
      monitor->OnFileTransfer(filename);
    }

    if (TH_ISREG(handle)) {
      if (OsCreateDirectory(OsDirName(path), true) != 0) {
        if (!is_meta_file) {
          err = ZISYNC_ERROR_OS_IO;
          ZSLOG_ERROR("Put handler mkdirhier(%s) fail: %s",
                      path.c_str(), OsGetLastErr());
        }
        continue;
      }

      OsFile file;
      if (err == ZISYNC_SUCCESS) {
        if (file.Open(path, "wb") != 0) {
          err = ZISYNC_ERROR_OS_IO;
          ZSLOG_ERROR("Put handler open file(%s) fail: %s",
                      path.c_str(), OsGetLastErr());
        }
      }

      char buffer[g_file_buffer_size];
      bool is_write_ok = true;
      int nbytes = 0;
      for (int64_t i = th_get_size(handle); i > 0; i -= nbytes) {
        if (abortable->IsAborted() ||
            !tree_agent->AllowPut(local_tree_id, remote_tree_id, filename)) {
          ZSLOG_INFO("Put handler aborted.");
          return ZISYNC_ERROR_CANCEL;
        }

        nbytes =
            i > g_file_buffer_size ? g_file_buffer_size : static_cast<int>(i);
        in.read(buffer, nbytes);
        if (in.fail()) {
          err = ZISYNC_ERROR_OS_IO;
          ZSLOG_ERROR("Transfer Worker read file(%s) fail.", path.c_str());
          in.clear();
          break;
        }

        nbytes = static_cast<int>(in.gcount());
        if (!is_meta_file) {
          monitor->OnByteTransfered(nbytes);
        }

        /* write block to output file */
        if (file.IsOk() && is_write_ok) {
          if (file.Write(buffer, nbytes, 1) != 1) {
            if (errno == ENOSPC) {
              ZSLOG_ERROR("Disk has no space to write data.");
              return ZISYNC_ERROR_OS_IO;
            } else {
              is_write_ok = false;
              err = ZISYNC_ERROR_TAR;
              ZSLOG_ERROR("Transfer Worker fwrite fail.");
            }
          }
        }
      }

      if (!is_meta_file) {
        if (err == ZISYNC_SUCCESS) {
          monitor->OnFileTransfered(1);
        } else {
          monitor->OnFileSkiped(1);
        }
      }
    } else {
      ZSLOG_ERROR("encounted unsupported file types in tar stream.");
    }
  }

  // return server->GetPutHandler()->OnHandlePut(tmpdir.path());
  return ZISYNC_SUCCESS;
}

}  // namespace zs
