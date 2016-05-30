// Copyright 2015 zisync.com
//
#ifndef ZISYNC_KERNEL_HISTORY_HISTORY_H
#define ZISYNC_KERNEL_HISTORY_HISTORY_H
#include <string>
#include <vector>
#include <memory>

#include "zisync/kernel/database/table.h"
#include "zisync/kernel/libevent/libevent++.h"
namespace zs {
using std::string;
using std::vector;
using std::unique_ptr;
class QueryHistoryResult;


class IHistoryDataSource {
 public:
  virtual err_t GetHistoryItems(vector<History> *histories
      , int32_t offset, int32_t limit, int32_t backup_type) = 0;
  virtual err_t StoreHistoryItems(vector<unique_ptr<History> > *histories) = 0;

  virtual err_t Initialize(ILibEventBase *evbase) = 0;
  virtual err_t CleanUp() = 0;
};

class IHistoryManager : public ILibEventVirtualServer {
 public:

  virtual err_t Initialize(unique_ptr<IHistoryDataSource> data_source) = 0;
  virtual err_t CleanUp() = 0;

  virtual err_t Startup(ILibEventBase *base) = 0;
  virtual err_t Shutdown(ILibEventBase *base) = 0;
  
  virtual err_t QueryHistories(QueryHistoryResult *query_history_
      , int32_t offset, int32_t limit, int32_t backup_type) = 0;  

  virtual err_t AppendHistory(const string &device_name
      , int32_t tree_id, int32_t backup_type, int64_t time_stamp
      , const string &from, int error
      , int type, const char *to, bool isdir = false) = 0;

#ifdef ZS_TEST
  virtual void ChangeDataSource(IHistoryDataSource*) = 0;
#endif

 protected:
  ILibEventBase* evbase_;

};

void InitHistory(History &new_item, const string &modifier, int32_t tree_id, int32_t backup_type
    , const string &frompath, const char *topath, int64_t time_stamp
    , int type, int error);
History *CreateHistory(const string &modifier, int32_t tree_id, int32_t backup_type, const string &frompath
    , const char *topath, int64_t time_stamp, int type, int error);

}

#endif
