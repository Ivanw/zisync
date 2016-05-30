/* Copyright [2014] <zisync.com> */

#include <cassert>
#include <memory>
#include <set>
#include <unordered_set>
#include <functional>
#include <cstring>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/plain_config.h"

namespace zs {

using std::unique_ptr;

/*  define here to ensure then construct order */
static Mutex tree_provider_mutex;

// class XSQLiteMutexDealloctor : public ICursor2Deallocator {
//  public:
//   explicit XSQLiteMutexDealloctor(XSQLite* hsqlite):
//       mutex_(sqlite3_db_mutex(hsqlite->GetRawHandle())) {}
//   ~XSQLiteMutexDealloctor() {
//     sqlite3_mutex_leave(mutex_);
//   }
//  private:
//   sqlite3_mutex* mutex_;
// };

ContentProvider::ContentProvider(void):is_delete_on_cleanup_(false) {
  mutex_ = new OsMutex();
  mutex_->Initialize();
}


ContentProvider::~ContentProvider(void) {
  assert(mutex_);
      
  if (is_delete_on_cleanup_) {
    OnDelete();
  } else {
    OnCleanUp();
  }

  mutex_->CleanUp();
  delete mutex_;
}

const char ContentProvider::AUTHORITY[] = "ContentProvider";
const Uri ContentProvider::URI(SCHEMA_CONTENT, AUTHORITY, "");

err_t ContentProvider::OnCreate(const char* app_data, const char *key) {
  assert(app_data != NULL && app_data[0] != '\0');

  app_data_ = app_data;

  // if (OsPathAppend(app_data_, "ZiSync") != ZISYNC_SUCCESS) {
  //   LOG(ERROR) <<"Path app_data_ over follow: " <<app_data_;
  //   return ZISYNC_ERROR_BAD_PATH;
  // }

  bool bExists = OsFileExists(app_data_);

  err_t eno = hsqlite_.Initialize(app_data_, key);  
  if (eno != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("database init fail: %s", app_data_.c_str());
    return eno;
  }

  const char* szCreate = "CREATE TABLE IF NOT EXISTS Version(version int);";
  eno = hsqlite_.ExecSQL(szCreate);
  if (eno != ZISYNC_SUCCESS) {
	  assert(false);
  }


  ContentValues cv(1);
  cv.Put("version", DATABASE_VERSION);

  if (!bExists) {
    if (OnCreateDatabase()) {
      int32_t row_id = hsqlite_.Insert("Version", &cv, AOC_REPLACE);
      assert(row_id > 0);
    }
  } else {
    const char* projection[] = { "version" };
    bool has_version;
    int32_t version = 0;

    {
      std::unique_ptr<ICursor2> pCursor(hsqlite_.Query(
              "Version", projection, ARRAY_SIZE(projection), NULL));

      if (pCursor->MoveToNext())  {
        has_version = true;
        version = pCursor->GetInt32(0);
      } else {
        has_version = false;
      }
    }

    if (has_version) {
      if (version >= DATABASE_VERSION) {
        eno = ZISYNC_SUCCESS;
      }  else if (OnUpgradeDatabase(version)) {
        int num_row_affected = hsqlite_.Update("Version", &cv, NULL);
        assert(num_row_affected > 0);
        eno = num_row_affected > 0 ? ZISYNC_SUCCESS : ZISYNC_ERROR_SQLITE;
      }
    } else {
      if (OnCreateDatabase()) {
        int32_t row_id  = hsqlite_.Insert("Version", &cv, AOC_REPLACE);
        assert(row_id > 0);
        eno = row_id > 0 ? ZISYNC_SUCCESS : ZISYNC_ERROR_SQLITE;
      }
    }
  }
  if (eno != ZISYNC_SUCCESS) {
    return eno;
  }

  return eno;
}


err_t ContentProvider::OnCleanUp() {
  hsqlite_.CleanUp(false);
  return ZISYNC_SUCCESS;
}

err_t ContentProvider::Clear(const Uri& uri) {
  const char* szSQL[] = {
    TableDevice::DROP_SQL,
    TableDeviceIP::DROP_SQL,
    TableSync::DROP_SQL,
    TableTree::DROP_SQL,
    TableConfig::DROP_SQL,
    TableDHTPeer::DROP_SQL,
    TableSyncList::DROP_SQL,
    TableSyncMode::DROP_SQL,
    TableShareSync::DROP_SQL,
    TableStaticPeer::DROP_SQL,
  };

  err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
  if (eno != ZISYNC_SUCCESS) {
    assert(false);
    return eno;
  }
  if (!OnCreateDatabase()) {
    assert(false);
    return ZISYNC_ERROR_CONTENT;
  }
  
  PlainConfig::Clear();

  return ZISYNC_SUCCESS;
}

ICursor2* ContentProvider::Query(
    const Uri& uri,
    const char* projection[],
    int projection_count,
    const char* where)  {
  // Lock sqlite using RAII
  XSQLiteMutexAuto mutex(&hsqlite_);

  const char* szTable = GetTable(uri);
  ICursor2 *cursor = hsqlite_.Query(
      szTable, projection, projection_count, where);
  // cursor->AddDealloctor(new XSQLiteMutexDealloctor(&hsqlite_));
  return cursor;
}

int32_t ContentProvider::Insert(
    const Uri& uri,
    ContentValues* values,
    OpOnConflict on_conflict) {
  int32_t id = -1;
  const char* szTable = GetTable(uri);

  {
    // Lock sqlite using RAII
    XSQLiteMutexAuto mutex(&hsqlite_);
    id = hsqlite_.Insert(szTable, values, on_conflict);
  }

  if (id > 0) {
    NotifyChange(uri, NULL);
  }

  return id;
}

int32_t ContentProvider::Update(
    const Uri& uri,
    ContentValues* values,
    const char* where) {
  int32_t nAffectedRows = 0;
  const char* szTable = GetTable(uri);
  {
    // Lock sqlite using RAII
    XSQLiteMutexAuto mutex(&hsqlite_);
    nAffectedRows = hsqlite_.Update(szTable, values, where);
  }

  if (nAffectedRows > 0) {
    NotifyChange(uri, NULL);
  }

  return nAffectedRows;
}

int32_t ContentProvider::Delete(
    const Uri& uri,
    const char* where) {
  int32_t nAffectedRows = 0;
  const char* szTable = GetTable(uri);
  {
    // Lock sqlite using RAII
    XSQLiteMutexAuto mutex(&hsqlite_);
    nAffectedRows = hsqlite_.Delete(szTable, where);
  }

  if (nAffectedRows > 0) {
    NotifyChange(uri, NULL);
  }


  return nAffectedRows;
}

const char* ContentProvider::GetTable(const Uri& uri) {
  std::string first_segment;
  std::string path = uri.GetPath();
  size_t index = path.find('/', 1);
  if (index != std::string::npos) {
    first_segment = path.substr(1, index);
  } else {
    first_segment = path.substr(1);
  }

  if (first_segment == TableConfig::NAME) {
    return TableConfig::NAME;
  } else if (first_segment == TableDevice::NAME) {
    return TableDevice::NAME;
  }else if (first_segment == TableStaticPeer::NAME) {
    return TableStaticPeer::NAME;
  } else if (first_segment == TableSync::NAME) {
    return TableSync::NAME;
  } else if (first_segment == TableTree::NAME) {
    return TableTree::NAME;
  } else if (first_segment == TableDHTPeer::NAME) {
    return TableDHTPeer::NAME;
  } else if (first_segment == TableSyncList::NAME) {
    return TableSyncList::NAME;
  } else if (first_segment == TableDeviceIP::NAME) {
    return TableDeviceIP::NAME;
  } else if (first_segment == TableSyncMode::NAME) {
    return TableSyncMode::NAME;
  } else if (first_segment == TableShareSync::NAME) {
    return TableShareSync::NAME;
  } else if (first_segment == TableHistory::NAME) {
    return TableHistory::NAME;
  } else if (first_segment == TablePermission::NAME) {
    return TablePermission::NAME;
  } else if (first_segment == TableLicences::NAME) {
    return TableLicences::NAME;
  } else if (first_segment == TableMisc::NAME) {
    return TableMisc::NAME;
  } else {
    ZSLOG_ERROR("%s", first_segment.c_str());
    assert(false);
    return NULL;
  }
  // } else if (first_segment == VCLOCK_TABLE) {
  //   return VCLOCK_TABLE;
  // } else if (first_segment == SYNC_FILE_VIEW) {
  //   return SYNC_FILE_VIEW;
  // } else if (first_segment == SYNC_DIR_VIEW) {
  //   return SYNC_DIR_VIEW;
  // } else if (first_segment == VCUUID_TABLE) {
  //   return VCUUID_TABLE;
  // } else if (first_segment == DEVICE_TTL_TABLE) {
  //   return DEVICE_TTL_TABLE;
  // } else if (first_segment == USN_TABLE) {
  //   return USN_TABLE;
}

ICursor2* ContentProvider::sQuery(
    const Uri& uri,
    const char* projection[], int projection_count,
    Selection* selection, const char* sort_order ) {
  // Lock sqlite using RAII
  XSQLiteMutexAuto mutex(&hsqlite_);

  const char* szTable = GetTable(uri);
  ICursor2 *cursor = hsqlite_.sQuery(
      szTable, projection, projection_count, selection, sort_order);
  // cursor->AddDealloctor(new XSQLiteMutexDealloctor(&hsqlite_));
  return cursor;
}

int32_t ContentProvider::sUpdate(
    const Uri& uri, ContentValues* values, Selection* selection ) {
  int32_t nAffectedRows = 0;
  const char* szTable = GetTable(uri);
  {
    // Lock sqlite using RAII
    XSQLiteMutexAuto mutex(&hsqlite_);
    nAffectedRows = hsqlite_.sUpdate(szTable, values, selection);
  }

  if (nAffectedRows > 0) {
    NotifyChange(uri, NULL);
  }
  return nAffectedRows;
}

int32_t ContentProvider::sDelete(const Uri& uri, Selection* selection) {
  int32_t nAffectedRows = 0;
  const char* szTable = GetTable(uri);
  {
    // Lock sqlite using RAII
    XSQLiteMutexAuto mutex(&hsqlite_);
    nAffectedRows = hsqlite_.sDelete(szTable, selection);
  }

  if (nAffectedRows > 0) {
    NotifyChange(uri, NULL);
  }
  return nAffectedRows;
}

int32_t ContentProvider::BulkInsert(
    const Uri& uri,
    ContentValues* values[],
    int32_t value_count,
    OpOnConflict on_conflict) {
  int32_t nAffectedRows = 0;
  const char* szTable = GetTable(uri);
  {
    // Perform txn in local block
    XSQLiteTransation txn(&hsqlite_);
    for (int i = 0; i < value_count; i++) {
      if (hsqlite_.Insert(szTable, values[i], on_conflict) != -1) {
        ++nAffectedRows;
      }
    }
  }

  if (nAffectedRows > 0) {
    NotifyChange(uri, NULL);
  }

  return nAffectedRows;
}

class UriHasher{
 public :
  size_t operator()(const Uri &uri) const {
    const char* s = uri.AsString();
    size_t h = 0;
    for ( ; *s; ++s)
      h = 5 * h + *s;
    return h;
  }
};

int32_t ContentProvider::ApplyBatch(
    // const Uri& uri,
    const char* szAuthority,
    OperationList* op_list)  {
  //
  // fixme: since we has only constant uri,
  // storing poiter is ok. but if we variable
  // we should store Uri
  //
  std::unordered_set<Uri, UriHasher> m_vUri;
  int32_t nAffectedRows, nTotalAffectedRows = 0;
  {
    // Perform txn in local block
    XSQLiteTransation txn(&hsqlite_);
    int32_t nCount = op_list->GetCount();
    InsertOperation* pInsert;

    for (int i = 0; i < nCount; i++) {
      ContentOperation* operation = op_list->GetAt(i);
      OperationCondition* pCondition = operation->GetCondition();
      if (pCondition && pCondition->Evaluate(operation) == false) {
        continue;
      }

      const char* table = GetTable(*operation->GetUri());

      switch (operation->GetType())  {
        case AOT_INSERT:
          pInsert = static_cast<InsertOperation*>(operation);
          if (hsqlite_.Insert(table, pInsert->GetContentValues(),
                              pInsert->GetOnConflict()) != -1)  {
            nTotalAffectedRows += 1;
            m_vUri.insert(*operation->GetUri());
          }
          break;

        case AOT_UPDATE:
          nAffectedRows = hsqlite_.sUpdate(
              table, operation->GetContentValues(), operation->GetSelection());
          if (nAffectedRows > 0) {
            nTotalAffectedRows += nAffectedRows;
            m_vUri.insert(*operation->GetUri());
          }
          break;

        case AOT_DELETE:
          nAffectedRows = hsqlite_.sDelete(table, operation->GetSelection());
          if (nAffectedRows > 0) {
            nTotalAffectedRows += nAffectedRows;
            m_vUri.insert(*operation->GetUri());
          }
          break;
      }
      OperationPostProcess *process = operation->GetPostProcess();
      if (process) {
        process->Evaluate();
      }
    }
  }

  for (auto it = m_vUri.begin(); it != m_vUri.end(); it++) {
    NotifyChange(*it, NULL);
  }
  return nTotalAffectedRows;
}

bool ContentProvider::RegisterContentObserver(
    const Uri& uri,
    bool notify_for_decendents,
    ContentObserver* content_observer ) {
  MutexAuto mutex_auto(mutex_);

  const char* szPath = uri.GetPath();
  if (notify_for_decendents) {
    auto it = map_tree_observer.find(szPath);
    if (it != map_tree_observer.end()) {
      it->second.push_back(content_observer);
    } else {
      map_tree_observer[szPath].push_back(content_observer);
    }
  } else {
    auto it = map_node_observer.find(szPath);
    if (it != map_node_observer.end()) {
      it->second.push_back(content_observer);
    } else {
      map_node_observer[szPath].push_back(content_observer);
    }
  }

  return true;
}

bool ContentProvider::UnregisterContentObserver(
    const Uri& uri, ContentObserver* content_observer ) {
  MutexAuto mutex_auto(mutex_);

  const char* szPath = uri.GetPath();
  std::unordered_map<std::string, std::list<ContentObserver*>>*
      mapPtr[] = { &map_tree_observer, &map_node_observer };

  for (size_t i = 0; i < ARRAY_SIZE(mapPtr); i++) {
    auto map = mapPtr[i];
    auto it = map->find(szPath);
    if (it != map->end()) {
      std::list<ContentObserver*>& v = it->second;
      for (auto ii = v.begin(); ii != v.end(); ) {
        if ((*ii) == content_observer) {
          ii = v.erase(ii);
        } else {
          ++ii;
        }
      }
    }
  }

  return true;
}

bool ContentProvider::NotifyChange(
    const Uri& uri, ContentObserver* content_observer ) {
  MutexAuto mutex_auto(mutex_);

  const char* path = uri.GetPath();
  auto it = map_node_observer.find(path);
  if (it != map_node_observer.end()) {
    auto v = it->second;
    for (auto ii = v.begin(); ii != v.end(); ii++) {
      if ((*ii) == content_observer) {
        (*ii)->DispatchChange(true);
      } else {
        (*ii)->DispatchChange(false);
      }
    }
  }
  for (it = map_tree_observer.begin(); it != map_tree_observer.end(); it++) {
    if (StringStartsWith(path, it->first.c_str())) {
      auto v = it->second;
      for (auto ii = v.begin(); ii != v.end(); ++ii) {
        if ((*ii) == content_observer) {
          (*ii)->DispatchChange(true);
        } else {
          (*ii)->DispatchChange(false);
        }
      }
    }
  }

  return true;
}

bool ContentProvider::OnCreateDatabase() {
  // const char* szSyncFile = "CREATE VIEW IF NOT EXISTS SyncFile AS "
  //    " SELECT File.id AS id, path, type, status, mtime, length, "
  //    " usn, sha1, modifier, win_attr, unix_attr, "
  //    " tree_id, local_clock, other_clock "
  //    " FROM File, VClock "
  //    " WHERE File.id = VClock.file_id ";

  std::string schema_sync_dir_view;
  StringFormat(
      &schema_sync_dir_view,
      "CREATE VIEW IF NOT EXISTS SyncDir AS "
      " SELECT Sync.id AS id, tree_uuid, tree_root, "
      " sync_uuid, tree_id, clock, remote_device_uuid, remote_tree_uuid, "
      " remote_tree_root, remote_clock, status, creator, ctime, last_sync "
      " FROM Tree, Sync "
      " WHERE Sync.tree_id = Tree.id ");



  const char* sqlInitDB[] = {
    /* Create MetaRepoDB tables */
    "PRAGMA foreign_keys = ON;",
    TableDevice::CREATE_SQL, 
    TableDevice::CREATE_IS_MINE_INDEX_SQL,
    TableDevice::CREATE_STATUS_INDEX_SQL,
    TableDeviceIP::CREATE_SQL,
    TableDeviceIP::CREATE_EARLIEST_NO_RESP_TIME_INDEX_SQL,
    TableDeviceIP::CREATE_DEVICE_ID_INDEX_SQL,
    TableStaticPeer::CREATE_SQL,
    TableStaticPeer::CREATE_IP_INDEX_SQL,
    TableSync::CREATE_SQL, 
    TableSync::CREATE_STATUS_INDEX_SQL, 
    TableSync::CREATE_TYPE_INDEX_SQL, 
    TableSync::CREATE_DEVICE_ID_INDEX_SQL, 
    TableSync::CREATE_RESTORE_SHARE_PERM_INDEX_SQL,
    TableTree::CREATE_SQL,
    TableTree::CREATE_DEVICE_ID_INDEX_SQL,
    TableTree::CREATE_SYNC_ID_INDEX_SQL,
    TableTree::CREATE_STATUS_INDEX_SQL,
    TableConfig::CREATE_SQL,
    TableDHTPeer::SCHEMA,
    TableSyncList::SCHEMA, 
    TableSyncList::CREATE_TREE_ID_INDEX_SQL,
    TableSyncMode::CREATE_SQL,
    TableShareSync::CREATE_SQL,
    TablePermission::CREATE_SQL,
    TableLicences::CREATE_SQL,
  };


// -----------------------------  Init Database  --------------------------
  err_t eno = hsqlite_.ExecSQLs(sqlInitDB, ARRAY_SIZE(sqlInitDB));
  assert(eno == ZISYNC_SUCCESS);

return true;
}

bool ContentProvider::OnUpgradeDatabase(int pre_version) {
  if (pre_version <= 1) {
    const char* szSQL[] = {
      "CREATE TABLE DeviceIPBackup("
          " device_id   INTEGER NOT NULL,"
          " ip          TEXT NOT NULL,"
          " is_ipv6     INTEGER NOT NULL,"
          " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE,"
          " UNIQUE(ip))",
      "INSERT INTO DeviceIPBackup SELECT * FROM DeviceIP",
      "DROP TABLE DeviceIP",
      "CREATE TABLE DeviceIP ("
          " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
          " device_id   INTEGER NOT NULL,"
          " ip          TEXT NOT NULL,"
          " is_ipv6     INTEGER NOT NULL,"
          " earliest_no_response_time INTEGER NOT NULL DEFAULT -1,"
          " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE,"
          " UNIQUE(ip))",
      "INSERT INTO DeviceIP (device_id, ip, is_ipv6) SELECT * FROM DeviceIPBackup",
      "DROP TABLE DeviceIPBackup",
    };

    err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }
  if (pre_version <= 2) {
    const char* szSQL[] = {
      "CREATE TABLE TreeBackup("
        " id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        " uuid      VARCHAR(40) UNIQUE NOT NULL,"
        " root      TEXT,"
        " device_id INTEGER,"
        " sync_id   INTEGER NOT NULL,"
        " status    INTEGER NOT NULL,"
        " last_find INTEGER NOT NULL," // in s
        " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE, "
        " FOREIGN KEY(sync_id)   REFERENCES Sync(id)   ON DELETE CASCADE)",
      "INSERT INTO TreeBackup SELECT * FROM Tree",
      "DROP TABLE Tree",
    "CREATE TABLE Tree ("
        " id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        " uuid      VARCHAR(40) UNIQUE NOT NULL,"
        " root      TEXT,"
        " device_id INTEGER,"
        " sync_id   INTEGER NOT NULL,"
        " status    INTEGER NOT NULL,"
        " last_find INTEGER NOT NULL," // in s
        " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE, "
        " FOREIGN KEY(sync_id)   REFERENCES Sync(id)   ON DELETE CASCADE)",
      "INSERT INTO Tree SELECT * FROM TreeBackup",
      "DROP TABLE TreeBackup",
    };
    
    err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }

  if (pre_version <= 3) {
    const char* szSQL[] = {
      "ALTER TABLE Device ADD COLUMN is_backup INTEGER NOT NULL DEFAULT 1",
      "ALTER TABLE Device ADD COLUMN backup_root TEXT",
      "ALTER Table Sync ADD COLUMN device_id INTEGER REFERENCES "
          "Device(id) ON DELETE CASCADE",
      TableSync::CREATE_TYPE_INDEX_SQL,
      TableSync::CREATE_STATUS_INDEX_SQL,
      TableSync::CREATE_DEVICE_ID_INDEX_SQL,
    };
    err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }

  if (pre_version <= 4) {
    const char* szSQL[] = {
      "CREATE TABLE DeviceBackup("
          " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
          " uuid        VARCHAR(40) UNIQUE NOT NULL,"
          " name        TEXT NOT NULL,"
          " route_port  INTEGER NOT NULL,"
          " data_port   INTEGER NOT NULL,"
          " status      INTEGER NOT NULL,"
          " type        INTEGER NOT NULL,"
          " is_mine     INTEGER NOT NULL,"
          " backup_root TEXT)",
      "INSERT INTO DeviceBackup SELECT id, uuid, name, "
          "route_port, data_port, status, type, is_mine, "
          "backup_root FROM Device",
      "DROP TABLE Device",
      "CREATE TABLE Device ("
          " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
          " uuid        VARCHAR(40) UNIQUE NOT NULL,"
          " name        TEXT NOT NULL,"
          " route_port  INTEGER NOT NULL,"
          " data_port   INTEGER NOT NULL,"
          " status      INTEGER NOT NULL,"
          " type        INTEGER NOT NULL,"
          " is_mine     INTEGER NOT NULL,"
          " backup_root TEXT)",
      "INSERT INTO Device SELECT * FROM DeviceBackup",
      "DROP TABLE DeviceBackup",
      "ALTER TABLE Device ADD COLUMN backup_dst_root TEXT",
      "UPDATE Sync SET device_id = 0",
      "CREATE TABLE SyncBackup("
          " id        INTEGER PRIMARY KEY AUTOINCREMENT,"
          " uuid      VARCHAR(40) UNIQUE NOT NULL,"
          " name      TEXT NOT NULL,"
          " last_sync INTEGER NOT NULL,"
          " type      INTEGER NOT NULL,"
          " status    INTEGER NOT NULL,"
          " device_id INTEGER NOT NULL DEFAULT 0,"
          " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE)",
      "INSERT INTO SyncBackup SELECT * FROM Sync",
      "DROP TABLE Sync",
      "CREATE TABLE Sync("
          " id        INTEGER PRIMARY KEY AUTOINCREMENT,"
          " uuid      VARCHAR(40) UNIQUE NOT NULL,"
          " name      TEXT NOT NULL,"
          " last_sync INTEGER NOT NULL,"
          " type      INTEGER NOT NULL,"
          " status    INTEGER NOT NULL,"
          " device_id INTEGER NOT NULL DEFAULT 0,"
          " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE)",
      "INSERT INTO Sync SELECT * FROM SyncBackup",
      "DROP TABLE SyncBackup",
      "ALTER TABLE Tree ADD COLUMN backup_type INTEGER NOT NULL DEFAULT 0",
      "ALTER TABLE Tree ADD COLUMN is_enabled INTEGER NOT NULL DEFAULT 1",
      "ALTER TABLE Tree ADD COLUMN sync_mode INTEGER NOT NULL DEFAULT 0",
    };
    err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }
  
  if (pre_version <= 5) {
    const char* create_sql = TableSyncMode::CREATE_SQL;
    err_t eno = hsqlite_.ExecSQL(create_sql);
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }

    const char* tree_projs[] = { "id", "sync_id" };
    std::unique_ptr<ICursor2> tree_cursor(hsqlite_.Query(
            "Tree", tree_projs, ARRAY_SIZE(tree_projs),
            "sync_mode = 0 and backup_type = 1"));
    while (tree_cursor->MoveToNext()) {
      int32_t local_tree_id = tree_cursor->GetInt32(0);
      int32_t sync_id = tree_cursor->GetInt32(1);
      string where;
      StringFormat(&where, "sync_id = %d AND id != %d", 
                   sync_id, local_tree_id);
      std::unique_ptr<ICursor2> tree_cursor(hsqlite_.Query(
              "Tree", tree_projs, ARRAY_SIZE(tree_projs),
              where.c_str()));
      while (tree_cursor->MoveToNext()) {
        ContentValues cv(3);
        cv.Put("local_tree_id", local_tree_id);
        cv.Put("remote_tree_id", tree_cursor->GetInt32(0));
        cv.Put("sync_mode", 0);
        int32_t row_id = hsqlite_.Insert("SyncMode", &cv, AOC_REPLACE);
        assert(row_id >= 0);
        if (row_id < 0) {
          return false;
        }
      }
    }
    const char* szSQL[] = {
      "CREATE TABLE TreeBackup ("
          " id           INTEGER PRIMARY KEY AUTOINCREMENT,"
          " uuid         VARCHAR(40) UNIQUE NOT NULL,"
          " root         TEXT,"
          " device_id    INTEGER,"
          " sync_id      INTEGER NOT NULL,"
          " status       INTEGER NOT NULL,"
          " last_find    INTEGER NOT NULL," // in s
          " backup_type  INTEGER NOT NULL DEFAULT 0,"
          " is_enabled   INTEGER NOT NULL DEFAULT 1,"
          " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE, "
          " FOREIGN KEY(sync_id)   REFERENCES Sync(id)   ON DELETE CASCADE)",
      "INSERT INTO TreeBackup SELECT id, uuid, root, device_id, sync_id, "
          "status, last_find, backup_type, is_enabled FROM Tree",
      "DROP TABLE Tree",
      "CREATE TABLE Tree ("
          " id           INTEGER PRIMARY KEY AUTOINCREMENT,"
          " uuid         VARCHAR(40) UNIQUE NOT NULL,"
          " root         TEXT,"
          " device_id    INTEGER,"
          " sync_id      INTEGER NOT NULL,"
          " status       INTEGER NOT NULL,"
          " last_find    INTEGER NOT NULL," // in s
          " backup_type  INTEGER NOT NULL DEFAULT 0,"
          " is_enabled   INTEGER NOT NULL DEFAULT 1,"
          " FOREIGN KEY(device_id) REFERENCES Device(id) ON DELETE CASCADE, "
          " FOREIGN KEY(sync_id)   REFERENCES Sync(id)   ON DELETE CASCADE)",
      "INSERT INTO Tree SELECT * FROM TreeBackup",
      "DROP TABLE TreeBackup",
      "DROP TABLE BackupTarget",
    };
    eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }
  if (pre_version < 7) {
    const char* szSQL[] = {
      "UPDATE Device SET is_mine = 0 WHERE id != 0",
    };
    err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
    int32_t remote_device_id;
    {
      const char *device_projs[] = { "id" };
      unique_ptr<ICursor2> cursor(hsqlite_.Query(
              "Device", device_projs, ARRAY_SIZE(device_projs),
              "id != 0"));
      remote_device_id = cursor->MoveToNext() ? 
          cursor->GetInt32(0) : -1;
    }
    if (remote_device_id != -1) {
      ContentValues cv(1);
      cv.Put("device_id", remote_device_id);
      hsqlite_.Update(
          "Sync", &cv, "(type = 1 OR type = 3) AND device_id = 0");
    }
  }
  if (pre_version < 8) {
    const char* szSQLs[] = {
      "INSERT INTO Device (id, uuid, name, route_port, data_port, "
          "status, type, is_mine ) VALUES (-1, 'NULL', 'NULL', 0, 0, 1, 3, 1)",
      "ALTER TABLE Sync ADD COLUMN permission INTEGER NOT NULL DEFAULT 3",
      "ALTER TABLE Sync ADD COLUMN restore_share_perm INTEGER NOT NULL DEFAULT -1",
      "ALTER TABLE Device ADD COLUMN version INTEGER NOT NULL DEFAULT 0",
      TableSync::CREATE_RESTORE_SHARE_PERM_INDEX_SQL,
    };
    err_t eno = hsqlite_.ExecSQLs(szSQLs, ARRAY_SIZE(szSQLs));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
    ContentValues cv(1);
    cv.Put("device_id", -1);
    hsqlite_.Update("Sync", &cv, "type = 1 OR type = 3");
  }

  if (pre_version < 11) {
    const char *szSQLs[] = {
      TableStaticPeer::CREATE_SQL,
      TableStaticPeer::CREATE_IP_INDEX_SQL,
    };
    err_t eno = hsqlite_.ExecSQLs(szSQLs, ARRAY_SIZE(szSQLs));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }

  if (pre_version < 13) {
    std::string alter_table_tree;
    StringFormat(&alter_table_tree
        , "ALTER TABLE Tree ADD COLUMN %s INTEGER NOT NULL DEFAULT %" PRId32
        , TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_NORMAL);
    const char* szSQL[] = {
      alter_table_tree.c_str(),
    };
    err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }
  return OnCreateDatabase();
}

void ContentProvider::GetTables(XSQLite *sqlite
    , std::vector<std::string> *table_names) {
//  static const char* tables_[] = {
//    /* Create MetaRepoDB tables */
//    TableDevice::NAME,
//    TableDeviceIP::NAME,
//    TableStaticPeer::NAME,
//    TableSync::NAME,
//    TableTree::NAME,
//    TableConfig::NAME,
//    TableDHTPeer::NAME,
//    TableSyncList::NAME, 
//    TableSyncMode::NAME,
//    TableShareSync::NAME,
//  };
//  *tables = tables_;
//  *n = ARRAY_SIZE(tables_);

  const char *projs[] = {
    "name",
  };
  const char *where = "type = 'table'";
  unique_ptr<ICursor2> cursor(sqlite->Query("sqlite_master"
        , projs, ARRAY_SIZE(projs), where));
  while (cursor ->MoveToNext()) {
    table_names->push_back(cursor->GetString(0));
  }
}

static int GetColums(XSQLite *sqlite
                     , const std::string &table
                     , std::string *colums) {
  
  assert(colums);
  *colums = " ";

  static std::string sql;
  StringFormat(&sql, "PRAGMA table_info('%s');", table.c_str());

  unique_ptr<ICursor2> cursor(sqlite->ExecQuery(sql.c_str()));
  bool first = true;
  while (cursor ->MoveToNext()) {
    if (!first) {
      *colums += ',';
    }else {
      first = false;
    }
    const char * pcol_name = cursor->GetString(1);
    assert(pcol_name);
    *colums += pcol_name;
  }
  if (*colums == " ") {
    return -1;
  }
  *colums += ' ';
  return 0;
}

  err_t ContentProvider::CopyFromDatabase(ContentProvider &old) {
  XSQLite *old_sqlite = &old.hsqlite_;
  std::string attach, copy, detach;

  std::string passphrase;
    if (PlainConfig::GetSqlcipherPassword(&passphrase) != ZISYNC_SUCCESS) {
      assert(false);
    }

  StringFormat(&attach, "ATTACH DATABASE '%s' AS encrypted KEY '%s';"
	  , app_data_.c_str(), passphrase.c_str());
    
    err_t ret = ZISYNC_SUCCESS;
    
      ret = old_sqlite->ExecSQL(attach.c_str());
      if (ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("Failed sql: %s", attach.c_str());
        return ret;
      }
    
    std::vector<std::string> table_names;
    GetTables(old_sqlite, &table_names);
    
    std::vector<std::string> new_table_names;
    GetTables(&hsqlite_, &new_table_names);
    std::string colums;
    std::vector<std::string> copy_sqls;
    
    const char **sqls = new const char*[table_names.size()];
    int sqls_cnt = 0;
    
  for (auto it = table_names.begin(); it != table_names.end(); ++it) {
    
    if (*it == "sqlite_sequence")continue;
    if (find(new_table_names.begin(), new_table_names.end(), *it )
        == new_table_names.end()) {
     continue;
    }
    
    if (GetColums(&(old.hsqlite_), *it, &colums) != 0) {
      ZSLOG_ERROR("Cannot get colums of table: %s", it->c_str());
      ret =  ZISYNC_ERROR_GENERAL;
      sqls_cnt = -1;
      break;
    }
    StringFormat(&copy, "Insert Into encrypted.%s (%s) Select %s from %s"
        , it->c_str(), colums.c_str(), colums.c_str(), it->c_str());
    copy_sqls.push_back(copy);
    
    sqls[sqls_cnt++] = copy_sqls.back().c_str();
    
//    ret = old_sqlite->ExecSQL(copy.c_str());
//    if (ret != ZISYNC_SUCCESS) {
//      ZSLOG_ERROR("Failed sql: %s", copy.c_str());
//      return ret;
//    }
  }
    
    if (sqls_cnt > 0) {
      ret = old_sqlite->ExecSQLs(sqls, sqls_cnt);
      if (ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("Failed sql: %s", detach.c_str());
      }
    }
    
    delete []sqls;
    
  StringFormat(&detach, "DETACH DATABASE encrypted;");

      ret = old_sqlite->ExecSQL(detach.c_str());
      if (ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("Failed sql: %s", detach.c_str());
        return ret;
      }

  return ret;
  }

const char TreeProvider::AUTHORITY[] = "TreeProvider";

TreeProvider::TreeProvider(const char* tree_uuid):tree_uuid_(tree_uuid),
    authority_(TableFile::GenAuthority(tree_uuid_.c_str())) {
    }

bool TreeProvider::UriMatch(const Uri& uri) {
  return (authority_ == uri.GetAuthority());
}

bool TreeProvider::AuthorityMatch(const char* authority) {
  return (authority_ == authority);
}

err_t TreeProvider::OnCreate(const char *app_data, const char *key) {
  assert(app_data != NULL && app_data[0] != '\0');

  app_data_ = app_data;
  std::string tmp = app_data_;
  assert(tmp != "");
  zs::OsPathAppend(&tmp, tree_uuid_ + ".db");

  bool bExists = OsFileExists(tmp);

  err_t eno = hsqlite_.Initialize(tmp);
  if (eno != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("database init fail: %s", tmp.c_str());
    return eno;
  }

  const char* szCreate =
      "CREATE TABLE IF NOT EXISTS Version(version int);";
  eno = hsqlite_.ExecSQL(szCreate);
  if (eno != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Create Version table fail");
    return ZISYNC_ERROR_CONTENT;
  }

  ContentValues cv(1);
  cv.Put("version", DATABASE_VERSION);

  if (!bExists) {
    if (OnCreateDatabase()) {
      int32_t row_id = hsqlite_.Insert("Version", &cv, AOC_REPLACE);
      assert(row_id > 0);
    }
  } else {
    const char* projection[] = { "version" };

    bool has_version;
    int32_t version = 0;

    {
      std::unique_ptr<ICursor2> pCursor(hsqlite_.Query(
              "Version", projection, ARRAY_SIZE(projection), NULL));

      if (pCursor->MoveToNext())  {
        has_version = true;
        version = pCursor->GetInt32(0);
      } else {
        has_version = false;
      }
    }

    if (has_version) {
      if (version >= DATABASE_VERSION) {
        eno = ZISYNC_SUCCESS;
      }  else if (OnUpgradeDatabase(version)) {
        int num_row_affected = hsqlite_.Update("Version", &cv, NULL);
        assert(num_row_affected > 0);
        eno = num_row_affected > 0 ? ZISYNC_SUCCESS : ZISYNC_ERROR_SQLITE;
      }
    } else {
      if (OnCreateDatabase()) {
        int32_t row_id  = hsqlite_.Insert("Version", &cv, AOC_REPLACE);
        assert(row_id > 0);
        eno = row_id > 0 ? ZISYNC_SUCCESS : ZISYNC_ERROR_SQLITE;
      }
    }
  }

  return eno;
}

bool TreeProvider::OnCreateDatabase() {
  const char* sqlInitDB[] = {
    /* Create MetaRepoDB tables */
    "PRAGMA foreign_keys = ON;",
    TableFile::CREATE_SQL,
    TableFile::CREATE_USN_INDEX_SQL,
  };

  // -----------------------------  Init Database  --------------------------
  err_t eno = hsqlite_.ExecSQLs(sqlInitDB, ARRAY_SIZE(sqlInitDB));
  assert(eno == ZISYNC_SUCCESS);

  return true;
}

bool TreeProvider::OnUpgradeDatabase(int pre_version) {
  if (pre_version < 9) {
    const char *szSQLs[] = {
      "ALTER TABLE File Add COLUMN alias TEXT",
    };
    err_t eno = hsqlite_.ExecSQLs(szSQLs, ARRAY_SIZE(szSQLs));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }
  
  if (pre_version < 10) {
    const char *szSQLs[] = {
      "ALTER TABLE File Add COLUMN modifier TEXT",
      "ALTER TABLE File Add COLUMN time_stamp BIGINT",
    };
    err_t eno = hsqlite_.ExecSQLs(szSQLs, ARRAY_SIZE(szSQLs));
    assert(eno == ZISYNC_SUCCESS);
    if (eno != ZISYNC_SUCCESS) {
      return false;
    }
  }
  
  return true;
}

const char* TreeProvider::GetTable(const Uri& uri) {
  return TableFile::NAME;
}

err_t ContentProvider::OnDelete() {
  hsqlite_.CleanUp(true);
  return ZISYNC_SUCCESS;
}

const char HistoryProvider::AUTHORITY[] = "PlainProvider";
const Uri HistoryProvider::URI(SCHEMA_CONTENT, AUTHORITY, "");

HistoryProvider::HistoryProvider():ContentProvider(){}

bool HistoryProvider::OnCreateDatabase() {
  const char* sqlInitDB[] = {
    /* Create MetaRepoDB tables */
    "PRAGMA foreign_keys = ON;",
    TableHistory::CREATE_SQL,
    TableHistory::CREATE_TIME_INDEX_SQL,
    TableMisc::CREATE_SQL,
  };

// -----------------------------  Init Database  --------------------------
  err_t eno = hsqlite_.ExecSQLs(sqlInitDB, ARRAY_SIZE(sqlInitDB));
  assert(eno == ZISYNC_SUCCESS);
return true;
}

bool HistoryProvider::OnUpgradeDatabase(int pre_version) {
  return OnCreateDatabase();
}

err_t HistoryProvider::Clear(const Uri& uri) {
  const char* szSQL[] = {
    TableHistory::DROP_SQL,
    TableMisc::DROP_SQL,
  };

  err_t eno = hsqlite_.ExecSQLs(szSQL, ARRAY_SIZE(szSQL));
  if (eno != ZISYNC_SUCCESS) {
    assert(false);
    return eno;
  }
  if (!OnCreateDatabase()) {
    assert(false);
    return ZISYNC_ERROR_CONTENT;
  }

  return ZISYNC_SUCCESS;
}

}  // namespace zs
