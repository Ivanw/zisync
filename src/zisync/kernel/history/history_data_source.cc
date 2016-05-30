 // Copyright 2015 zisync.com
 //

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/history/history_data_source.h"
#include "zisync/kernel/history/history.h"

namespace zs {
static const unsigned APPLY_BATCH_MAX = 500;
static const int64_t DATA_SOURCE_RELOAD_INTERVAL = 1000;
static const int MAX_HISTORY_ITEMS = 1000;
static const int TOERABLE_HISTORY_ITEM_OVERFLOW = 1000;


err_t HistoryDataSource::Initialize(ILibEventBase *evbase) {
  //init cache
  ReloadDataSource(MAX_HISTORY_ITEMS);
  
  //register db
  bool res = GetContentResolver()->RegisterContentObserver(TableHistory::URI,
                                                false, this);
  assert(res);
  return res ? ZISYNC_SUCCESS : ZISYNC_ERROR_GENERAL;
}

err_t HistoryDataSource::CleanUp() {
  //UnRegister
  bool res = GetContentResolver()->UnregisterContentObserver(TableHistory::URI,
                                                  this);
  return res ? ZISYNC_SUCCESS : ZISYNC_ERROR_GENERAL;
}

err_t HistoryDataSource::GetHistoryItems(vector<History> *histories
    , int32_t offset, int32_t limit, int32_t backup_type) {
  assert(histories);
  assert(limit == -1 || limit > 0);

  histories->clear();
  for(auto it = cache_.begin(); it != cache_.end(); ++it) {
    if (backup_type & it->backup_type) {
      if (offset > 0) {
        --offset;
      }else{
        if (limit == -1 || (int32_t)histories->size() < limit) {
          histories->push_back(*it);
        }else{
          break;
        }
      }
    }
  }
  return ZISYNC_SUCCESS;
}

err_t HistoryDataSource::StoreHistoryItems(vector<unique_ptr<History> > *histories) {

  OperationList op_list;
  const Uri &uri = TableHistory::URI;
  IContentResolver *resolver = GetContentResolver();

  for (vector<unique_ptr<History> >::iterator it = histories->begin();
       it != histories->end(); ++it) {
    ContentOperation *cp = op_list.NewInsert(uri, AOC_ABORT);
    ContentValues *cv = cp->GetContentValues();
    cv->Put(TableHistory::COLUMN_MODIFIER, (*it)->modifier, true);
    cv->Put(TableHistory::COLUMN_TREE_ID, (*it)->tree_id);
    cv->Put(TableHistory::COLUMN_TIME_STAMP, (*it)->time_stamp);
    cv->Put(TableHistory::COLUMN_SRCPATH, (*it)->frompath, true);
    cv->Put(TableHistory::COLUMN_ERROR, (*it)->error);
    cv->Put(TableHistory::COLUMN_CODE, (*it)->code);
    cv->Put(TableHistory::COLUMN_DSTPATH, (*it)->topath, true);
    cv->Put(TableHistory::COLUMN_BACKUP_TYPE, (*it)->backup_type);

    if ((unsigned)op_list.GetCount() >= APPLY_BATCH_MAX 
        || it + 1 == histories->end()) {
      int n = resolver->ApplyBatch(HistoryProvider::AUTHORITY, &op_list);
      if (n != op_list.GetCount()) {
        ZSLOG_ERROR("ApplyBatch into TableHistory failed");
        return ZISYNC_ERROR_GENERAL;
      }
      op_list.Clear();
    }
  }

  if (GetDatabaseSize() > MAX_HISTORY_ITEMS + TOERABLE_HISTORY_ITEM_OVERFLOW) {
    op_list.NewDelete(
        TableHistory::URI, "%s <= %d", 
        TableHistory::COLUMN_ID, GetDeleteThreshold());

    int n = resolver->ApplyBatch(HistoryProvider::AUTHORITY, &op_list);
    op_list.Clear();
    ZSLOG_INFO("%d history items purged.", n);
  }


  return ZISYNC_SUCCESS;
}

int HistoryDataSource::GetDatabaseSize() {
  IContentResolver *resolver = GetContentResolver();
  const char *cnt_projs[] = {"COUNT(*)"};
  std::unique_ptr<ICursor2> cnt_cursor(resolver->Query(
          TableHistory::URI, cnt_projs, ARRAY_SIZE(cnt_projs), NULL));
  if (cnt_cursor->MoveToNext()) {
    return cnt_cursor->GetInt32(0);
  }else {
    assert(false);
    ZSLOG_ERROR("Cant trim TableHistory: failed to get total count.");
    return -1;
  }
}

int HistoryDataSource::GetDeleteThreshold() {
  IContentResolver *resolver = GetContentResolver();
  const char *proj_id[] = {
    TableHistory::COLUMN_ID,
  };

  string order_limit;
  StringFormat(&order_limit, " %s DESC LIMIT %d,1"
               , TableHistory::COLUMN_ID, MAX_HISTORY_ITEMS);

  std::unique_ptr<ICursor2> cursor(resolver->sQuery(
          TableHistory::URI, proj_id, 
          ARRAY_SIZE(proj_id), NULL, order_limit.c_str()));
  if (cursor->MoveToNext()) {
    return cursor->GetInt32(0);
  }else {
    return -1;
  }
}

void HistoryDataSource::OnHandleChange(void *change) {
  ReloadDataSource(MAX_HISTORY_ITEMS);
}

void HistoryDataSource::ReloadDataSource(int reload_max) {
  //todo::order by, limit
  const char *all_projs[] = {
    TableHistory::COLUMN_ID, TableHistory::COLUMN_MODIFIER
      , TableHistory::COLUMN_TREE_ID, TableHistory::COLUMN_BACKUP_TYPE
      , TableHistory::COLUMN_SRCPATH, TableHistory::COLUMN_DSTPATH
      , TableHistory::COLUMN_TIME_STAMP, TableHistory::COLUMN_CODE
      , TableHistory::COLUMN_ERROR
  };

  Selection filter("%s != %d", TableHistory::COLUMN_ID, -1);
  string order_limit;
  StringFormat(&order_limit, "%s DESC LIMIT %" PRId32,
               TableHistory::COLUMN_TIME_STAMP, reload_max);
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> cursor(resolver->sQuery(TableHistory::URI, all_projs
                                               , ARRAY_SIZE(all_projs)
                                               , &filter, order_limit.c_str()));
  vector<History> tmp;
  while (cursor->MoveToNext() ) {
    const char *mod_ch = cursor->GetString(1);
    const char *from_ch = cursor->GetString(4);
    const char *topath_ch  = cursor->GetString(5);
    string mod, from;
    if (mod_ch) mod = mod_ch;
    if (from_ch) from = from_ch;
    History item;
    int32_t backup_type = cursor->GetInt32(3);
    if (backup_type == TableTree::BACKUP_NONE) {
      backup_type = BACKUP_TYPE_NONE;
    }else if (backup_type == TableTree::BACKUP_SRC) {
      backup_type = BACKUP_TYPE_SRC;
    }else {
      backup_type = BACKUP_TYPE_DST;
    }
    InitHistory(item, mod, cursor->GetInt32(2), backup_type
        , from, topath_ch, cursor->GetInt64(6), cursor->GetInt32(7)
        , cursor->GetInt32(8));

    tmp.emplace_back(std::move(item));
  }

  cache_.swap(tmp);
}

History *CreateHistory(const string &modifier, int32_t tree_id, int32_t backup_type
    , const string &frompath, const char *topath, int64_t time_stamp
    , int type, int error) {
  History * new_item = new History;
  if (new_item) {
    InitHistory(*new_item, modifier, tree_id, backup_type, frompath, topath, time_stamp
        , type, error);
  }
  return new_item;
}

void InitHistory(History &new_item, const string &modifier, int32_t tree_id, int32_t backup_type
    , const string &frompath, const char *topath, int64_t time_stamp
    , int type, int error) {

  new_item.modifier = modifier;
  new_item.tree_id = tree_id;
  new_item.backup_type = backup_type;
  new_item.frompath = frompath;
  if (topath) {
    new_item.topath = topath;
  }
  new_item.time_stamp = time_stamp;
  new_item.code = type;
  new_item.error = error; 
}

}//namespace zs
