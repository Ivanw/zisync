// Copyright 2015 zisync.com
//

#include <memory>
#include <iostream>
#include <algorithm>
#include <tuple>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/history/history.h"
#include "zisync/kernel/history/history_manager.h"

namespace zs {
using std::unique_ptr;

HistoryManager HistoryManager::s_history_manager;

HistoryManager::HistoryManager() : save_cache_scheduled_(false) {
  save_cache_interval_.tv_sec = 2;
  save_cache_interval_.tv_usec = 0;
}

err_t HistoryManager::Initialize(unique_ptr<IHistoryDataSource> data_source) {
  data_source_.reset(data_source.release());
  return ZISYNC_SUCCESS;
}

err_t HistoryManager::CleanUp() {
  if (data_source_) {
    data_source_->CleanUp();
  }
  return ZISYNC_SUCCESS;
}

err_t HistoryManager::Startup(ILibEventBase *base) {
  evbase_ = base;
  data_source_->Initialize(base);
  return ZISYNC_SUCCESS;
}

err_t HistoryManager::Shutdown(ILibEventBase *base) {
  return ZISYNC_SUCCESS;
}

err_t HistoryManager::AppendHistory(
    const string &device_name, int32_t tree_id
    , int32_t backup_type, int64_t time_stamp
    , const string &from, int error, int type
    , const char *to, bool isdir) {

  if (type == FILE_OPERATION_CODE_ADD && isdir) {
    type = FILE_OPERATION_CODE_MKDIR;
  }assert(type != FILE_OPERATION_CODE_ATTRIB);
  History * new_item = new History;
  if (!new_item) {
    return ZISYNC_ERROR_GENERAL;
  }

  new_item->modifier = device_name;
  new_item->tree_id = tree_id;
  new_item->frompath = from;
  if (to) {
    new_item->topath = to;
  }
  new_item->time_stamp = time_stamp;
  new_item->code = type;
  new_item->error = error;
  new_item->backup_type = backup_type;

  auto ctx = new std::tuple<vector<unique_ptr<History> >*, History*, IHistoryManager*>(
      &histories_, new_item, this);
  return evbase_->DispatchSync(LambdaAppendHistory, ctx);
}

err_t HistoryManager::QueryHistories(
    QueryHistoryResult *query_history_result
    , int32_t offset, int32_t limit, int32_t backup_type) {
  assert(data_source_);
  assert(query_history_result);
  auto ctx = new std::tuple<IHistoryDataSource*
    , vector<History>*, int32_t, int32_t, int32_t>(
        data_source_.get(), &query_history_result->histories
        , offset, limit, backup_type);
  return evbase_->DispatchSync(LambdaQueryHistory, ctx);
}

void HistoryManager::ClearSaveCacheSchedule() {
  save_cache_scheduled_ = false;
}

void HistoryManager::ScheduleSaveCacheIfNeeded() {
  if (!save_cache_scheduled_) {
    save_cache_scheduled_ = true;
    auto ctx = new std::tuple<IHistoryDataSource*, vector<unique_ptr<History> >*,
                              IHistoryManager*>(data_source_.get(), &histories_, this);
    evbase_->DispatchAsync(LambdaSaveCache, ctx, &save_cache_interval_);
  }
}

void HistoryManager::LambdaSaveCache(void *ctx) {
  IHistoryDataSource *data_source;
  vector<unique_ptr<History> > *histories;
  IHistoryManager *manager;
  std::tie(data_source, histories, manager) =
      *reinterpret_cast<std::tuple<IHistoryDataSource*, vector<unique_ptr<History> >*, IHistoryManager*>* > (ctx);
  data_source->StoreHistoryItems(histories);
  histories->clear();
  ((HistoryManager*)manager)->ClearSaveCacheSchedule();
  delete reinterpret_cast<std::tuple<IHistoryDataSource*, vector<unique_ptr<History> >*, IHistoryManager*>* > (ctx);
}

err_t HistoryManager::LambdaAppendHistory(void *ctx) {
  vector<unique_ptr<History> > *items_;
  History *item;
  IHistoryManager *manager;
  std::tie(items_, item, manager) = *reinterpret_cast<std::tuple<vector<unique_ptr<History> >*, History*, IHistoryManager*>* >(ctx);
  items_->emplace_back(item);
  ((HistoryManager*)manager)->ScheduleSaveCacheIfNeeded();
  delete reinterpret_cast<std::tuple<vector<unique_ptr<History> >*, History*, IHistoryManager*>* >(ctx);
  return ZISYNC_SUCCESS;
}

err_t HistoryManager::LambdaQueryHistory(void *ctx) {
  IHistoryDataSource *data_source;
  vector<History> *histories;
  int offset;
  int limit;
  int backup_type;
  std::tie(data_source, histories, offset, limit, backup_type) = *reinterpret_cast<std::tuple<IHistoryDataSource*
    , vector<History>*, int32_t, int32_t, int32_t>* >(ctx);
  err_t ret = data_source->GetHistoryItems(histories, offset, limit, backup_type);
  delete reinterpret_cast<std::tuple<IHistoryDataSource*
    , vector<History>*, int32_t, int32_t, int32_t>* >(ctx);
  return ret;
}

#ifdef ZS_TEST
static err_t LambdChangeDataSource(void *ctx) {
  unique_ptr<IHistoryDataSource> *old;
  IHistoryDataSource *_new;
  ILibEventBase *evb;
  std::tie(old, _new, evb) = *reinterpret_cast<std::tuple<unique_ptr<IHistoryDataSource>*,
                                                          IHistoryDataSource*, ILibEventBase*> *>(ctx);
  assert(old && _new);
  (*old)->CleanUp();
  _new->Initialize(evb);
  old->reset(_new);
  return ZISYNC_SUCCESS;
}

void HistoryManager::ChangeDataSource(IHistoryDataSource *data_source_new) {
  auto ctx = new std::tuple<unique_ptr<IHistoryDataSource>*, IHistoryDataSource*,
                            ILibEventBase*>(
                                &data_source_, data_source_new, evbase_);
  evbase_->DispatchSync(LambdChangeDataSource, ctx);
}
#endif

IHistoryManager *GetHistoryManager() {
  return &HistoryManager::s_history_manager;
}
}//namespace zs
