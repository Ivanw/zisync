/**
 * @file tar_upload_task.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar get task2.
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

#ifndef TAR_UPLOAD_TASK_H
#define TAR_UPLOAD_TASK_H

#include "zisync/kernel/libevent/transfer_task.h"

namespace zs {

class UploadTask2 : public TransferTask
                  , public IUploadTask
                  , public ITarWriterDelegate
                  , public ITarWriterDataSource {
 public:
  UploadTask2(ITaskMonitor* monitor,
              TransferServer2* server,
              int32_t local_tree_id,
              const std::string& remote_tree_uuid,
              const std::string& uri);
  virtual ~UploadTask2();
  //
  // Implment IPutTask
  //
  virtual int GetTaskId() {
    return task_id_;
  }

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path,
      int64_t size);

  virtual err_t Execute(); 

  //
  // Implement TransferTask
  //
  virtual void CreateResponseBodyParser(int32_t http_code);
  virtual void RequestHeadWriteAll(struct bufferevent* bev);
  virtual err_t RequestBodyWriteSome(struct bufferevent* bev);

  //
  // Implment ITarWriterDataSource
  //
  virtual bool EnumNext(
      std::string* real_path, std::string* encode_path, std::string* alias,
      int64_t* size);
  //
  // Implement ITarWriterDelegate
  //
  virtual void OnFileWillTransfer(
      const std::string& real_path,
      const std::string& encode_path);
  virtual void OnFileDidTransfered(
      const std::string& real_path,
      const std::string& encode_path);
  virtual void OnFileDidSkiped(
      const std::string& real_path,
      const std::string& encode_path);
  virtual void OnByteDidTransfered(int32_t nbytes);
  virtual void OnByteDidSkiped(int32_t nbytes);

  //
  // Override some TransferTask method
  //
  virtual void OnComplete(struct bufferevent* bev, err_t eno);

 private:
  int64_t total_size_;
  size_t current_index_;
  std::vector<int64_t> size_vector_;
  std::vector<std::string> real_path_vector_;
  std::vector<std::string> encode_path_vector_;
  std::unique_ptr<TarWriter> body_writer_;

  ITaskMonitor* monitor_;
};

}  // namespace zs


#endif
