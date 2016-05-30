// Copyright 2015, zisync.com
#ifndef  ZISYNC_KERNENL_UTILS_SYNC_GET_HANDLER_H_
#define  ZISYNC_KERNENL_UTILS_SYNC_GET_HANDLER_H_

#include <map>
#include <vector>
#include <tuple>
#include <memory>

#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/worker/sync_file.h"

namespace zs {

using std::string;
using std::map;
using std::vector;
using std::unique_ptr;

class LocalFileConsistentHandler;

class SyncGetHandler : public IGetHandler {
 public:
  SyncGetHandler(LocalFileConsistentHandler *local_file_consistent_handler,
                 const string &get_tmp_path, 
                 const string &local_file_authority):
      local_file_consistent_handler_(local_file_consistent_handler),
      get_tmp_path_(get_tmp_path), local_file_authority_(local_file_authority), 
      downlaod_num_(0), error_code_(ZISYNC_SUCCESS) {}
  ~SyncGetHandler() {}
  virtual void OnHandleFile(const string &relative_path, const string &sha1);
  void AppendSyncFile(SyncFile *sync_file);
  int32_t download_num() { return downlaod_num_; }
  err_t error_code() { return error_code_; }
  void HandleGetFiles();
 private:
  SyncGetHandler(SyncGetHandler&);
  void operator=(SyncGetHandler&);

  vector<unique_ptr<SyncFile>> wait_handle_files_;
  map<string /* relative path */, unique_ptr<SyncFile>> sync_files_;
  LocalFileConsistentHandler *local_file_consistent_handler_;
  const string &get_tmp_path_, &local_file_authority_;
  int32_t downlaod_num_;
  err_t error_code_;
};

}  // namespace zs

#endif   // ZISYNC_KERNENL_UTILS_SYNC_GET_HANDLER_H_
