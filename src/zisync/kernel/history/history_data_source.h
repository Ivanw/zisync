// Copyright 2015 zisync.com
//
#ifndef ZISYNC_KERNEL_HISTORY_HISTORY_DATA_SOURCE_H
#define ZISYNC_KERNEL_HISTORY_HISTORY_DATA_SOURCE_H
#include <vector>
#include <memory>

#include "zisync_kernel.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/history/history.h"


namespace zs {

using std::vector;
using std::unique_ptr;

class HistoryDataSource : public IHistoryDataSource , public ContentObserver{
 public:
  HistoryDataSource(){}
  virtual ~HistoryDataSource(){}

  //
  //Implement IHistoryDataSource
  //
  err_t GetHistoryItems(vector<History> *histories
      , int32_t offset, int32_t limit, int32_t backup_type);
  err_t StoreHistoryItems(vector<unique_ptr<History> > *histories);
  err_t Initialize(ILibEventBase *evbase);//RegisterContentObserver, initial read db
  err_t CleanUp();//UnRegister

  //
  //Implement ContentObserver
  //
  virtual void* OnQueryChange() { return NULL; }
  virtual void  OnHandleChange(void* lpChanges);
  //
  //Implement DataSourceReloadDelegate
  //
  virtual void ReloadDataSource(int limit);
  virtual unsigned GetCnt(){return cache_.size();};

 private:
  err_t CheckTrimBeforeReload(int max_items_left);
  int GetDeleteThreshold();
  int GetDatabaseSize();
 private:
  vector<History> cache_;
};

}//namespace zs


#endif
