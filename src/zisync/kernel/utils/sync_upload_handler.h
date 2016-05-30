// Copyright 2015, zisync.com
#ifndef  ZISYNC_KERNEL_UTILS_SYNC_UPLOAD_HANDLER_H_
#define  ZISYNC_KERNEL_UTILS_SYNC_UPLOAD_HANDLER_H_

#include "zisync_kernel.h"
#include "zisync/kernel/libevent/transfer.h"

namespace zs {

class SyncUploadHandler : public IPutHandler {
 public:
  SyncUploadHandler(
      const std::string &tree_uuid, const std::string &tmp_root);
  virtual ~SyncUploadHandler();

  virtual bool OnHandleFile(
      const std::string& relative_path, 
      const std::string& real_path, const std::string& sha1);

 private: 
  SyncUploadHandler(SyncUploadHandler&);
  void operator=(SyncUploadHandler&); 

  std::string tree_uuid_, tmp_root_, tree_root_;
};

}  // namespace zs

#endif   // ZISYNC_KERNEL_UTILS_SYNC_UPLOAD_HANDLER_H_
