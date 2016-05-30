// Copyright 2015 zisync.com
//
#ifndef ZISYNC_KERNEL_HISTORY_HISTORY_MANAGER_H
#define ZISYNC_KERNEL_HISTORY_HISTORY_MANAGER_H

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/history/history_data_source.h"

namespace zs {

  class HistoryManager : public IHistoryManager{
    public:
    friend IHistoryManager *GetHistoryManager();
    HistoryManager();
    virtual ~HistoryManager(){}

    err_t Initialize(unique_ptr<IHistoryDataSource> data_source);
    err_t CleanUp();

    //
    //Implement ILibEventVirtualServer
    //
    virtual err_t Startup(ILibEventBase *base);
    virtual err_t Shutdown(ILibEventBase *base);

    //
    //Implement IHistoryManager
    //
    err_t QueryHistories(QueryHistoryResult *query_history_
        , int32_t offset, int32_t limit, int32_t backup_type);  

    err_t AppendHistory(const string &device_name,
                        int32_t tree_id, int32_t backup_type, int64_t time_stamp,
                        const string &from, int error,
                        int type, const char *to, bool isdir = false);

    void ScheduleSaveCacheIfNeeded();
    void ClearSaveCacheSchedule();
#ifdef ZS_TEST
    void ChangeDataSource(IHistoryDataSource *data_source_new);
#endif

   private:
    static void LambdaSaveCache(void *ctx);
    static err_t LambdaAppendHistory(void *ctx);
    static err_t LambdaQueryHistory(void *ctx);


   private:
    static HistoryManager s_history_manager;

    vector<unique_ptr<History> > histories_;
    unique_ptr<IHistoryDataSource> data_source_;

    bool save_cache_scheduled_;
    struct timeval save_cache_interval_;
  };

  IHistoryManager *GetHistoryManager();


} //namespace zs

#endif
