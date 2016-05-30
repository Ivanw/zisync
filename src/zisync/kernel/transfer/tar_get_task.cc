/**
 * @file task_get_task.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Impmentation of tar format get task.
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

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"  // NOLINT

#include <stdio.h>
#include <fcntl.h>
#include <libtar.h>
#include <assert.h>

#ifdef _MSC_VER
#include<libtar_internal.h>
#else
#include <libgen.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include "zisync/kernel/transfer/transfer.pb.h"
#pragma warning(pop)
#else
#include "zisync/kernel/transfer/transfer.pb.h"
#endif

#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/transfer/tar_get_task.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/tree_status.h"

namespace zs {

TarGetTask::TarGetTask(ITaskMonitor* monitor,
                       const int32_t local_tree_id,
                       const std::string& remote_tree_uuid,
                       const std::string& uri,
                       SSL_CTX* ctx):
  TarGetTaskCommon(
      monitor, local_tree_id, remote_tree_uuid, uri, ctx), 
  remote_tree_id_(-1) { }

TarGetTask::~TarGetTask() {
}

int TarGetTask::GetTaskId() {
  return TarGetTaskCommon::GetTaskId();
}

err_t TarGetTask::AppendFile(
    const std::string& encode_path, int64_t size) {
  TarGetTaskCommon::AppendFile(encode_path, size);
  return ZISYNC_SUCCESS;
}

err_t TarGetTask::AppendFile(
    const std::string& encode_path,
    const std::string& signature,
    int64_t size) {
  TarGetTaskCommon::AppendFile(encode_path, size);
  return ZISYNC_SUCCESS;
}

err_t TarGetTask::Execute(const string& tmp_dir) {
  tmp_dir_ = tmp_dir;
  remote_tree_id_ = GetTransferServer()->GetTreeAgent()
      ->GetTreeId(remote_tree_uuid_);
  if (remote_tree_id_ == -1) {
    ZSLOG_ERROR("Remote Tree(%s) does not exist", remote_tree_uuid_.c_str());
    return ZISYNC_ERROR_TREE_NOENT;
  }
  // Create temp directory
  if (!OsDirExists(tmp_dir)) {
    ZSLOG_ERROR("tmp_dir(%s) should exits but not", tmp_dir.c_str());
    return ZISYNC_ERROR_OS_IO;
  }

  return TarGetTaskCommon::Execute();
}

//bool TarGetTask::PrepareFileParentDir(const std::string &path) {
//  char bname[MAX_PATH] = {0};
//  if (MkDirhier(OpenbsdDirName(path.c_str(), bname, MAX_PATH)) == -1) {
//    ZSLOG_ERROR(
//        "Get task mkdirhier(%s) fail: %s", path.c_str(), OsGetLastErr());
//    return false;
//  } else {
//    return true;
//  }
//}

bool TarGetTask::AllowPut(const string &path) {
  assert(remote_tree_id_ != -1);
  return GetTransferServer()->GetTreeAgent()->AllowPut(
      local_tree_id_, remote_tree_id_, path);
}
std::string TarGetTask::GetTargetPath(const std::string &encode_path) {
  std::string path = tmp_dir_;
  OsPathAppend(&path, encode_path);
  return path;
}

TarGetTaskFactory::TarGetTaskFactory() {
  format_ = "tar";
}

TarGetTaskFactory::~TarGetTaskFactory() {
}

IGetTask* TarGetTaskFactory::CreateTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const std::string& remote_tree_uuid,
    const std::string& uri,
    SSL_CTX* ctx) {
  return new TarGetTask(monitor, local_tree_id, remote_tree_uuid, uri, ctx);
}

const std::string& TarGetTaskFactory::GetFormat() {
  return format_;
}

err_t TarGetTask::SendGetHttpHeader(std::ostream *out) {
  std::string buffer;
  assert(local_tree_id_ != -1);
  std::string local_tree_uuid = GetTransferServer()->GetTreeAgent()->
      GetTreeUuid(local_tree_id_);
  if (local_tree_uuid.empty()) {
    ZSLOG_ERROR("Noent local tree(%d)", local_tree_id_);
    return ZISYNC_ERROR_TREE_NOENT;
  }
  const char*  http_header =
      "GET tar HTTP/1.1\r\nZiSync-Remote-Tree-Uuid:%s\r\n"
      "ZiSync-Local-Tree-Uuid:%s\r\n"
      "ZiSync-Total-Size:%" PRId64 "\r\nZiSync-Total-Files:%d\r\n\r\n";
  int ret = StringFormat(&buffer, http_header, remote_tree_uuid_.c_str(), local_tree_uuid.c_str(),
                         total_size_, total_file_num_);
  assert(ret > 0);

  out->write(buffer.c_str(), buffer.length());

  return  ZISYNC_SUCCESS;
}

}  // namespace zs

