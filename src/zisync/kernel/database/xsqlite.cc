/* Copyright [2014] <zisync.com> */

#include <stdarg.h>
#include <string.h>
#include <string>

#include "zisync/kernel/zslog.h"

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/xsqlite.h"

namespace zs {

class CQueryBuilder {
 public:
  CQueryBuilder(
      const char* table,
      const char* projection[], int32_t project_count,
      const char* where, const char* sort_order)
      : sql_("SELECT ") {
    assert(table != NULL);
    assert(projection != NULL && project_count > 0);

    StringImplode(&sql_, projection, project_count, ", ");

    if (where != NULL) {
      if (sort_order != NULL) {
        StringAppendFormat(&sql_,
            " FROM %s WHERE %s ORDER BY %s ", table, where, sort_order);
      } else {
        StringAppendFormat(&sql_,
            " FROM %s WHERE %s ", table, where);
      }
    } else {
      if (sort_order != NULL) {
        StringAppendFormat(&sql_,
            " FROM %s ORDER BY %s ", table, sort_order);
      } else {
        StringAppendFormat(&sql_, " FROM %s ", table);
      }
    }
  }

  const char* GetSQL() {
    return sql_.c_str();
  }

 protected:
  std::string sql_;
};

/************************************************************/
XSQLite::XSQLite(void)
    : hsqlite_(NULL) {
}

XSQLite::~XSQLite(void) {
}

static const std::string new_password_prefix = "Zr^wWK5[h*!7J@)k]";

static bool TestKey(sqlite3 *hsqlite_) {
  int rv;
  const char *sql = "select * from whatevertable;";
  sqlite3_stmt *stmt;
  XSQLiteMutexAuto mutex(hsqlite_);
  std::string errmsg;

  rv = sqlite3_prepare_v2(hsqlite_, sql, -1, &stmt, NULL);
  if (rv == SQLITE_OK) {
    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      StringFormat(&errmsg, "sqlite3_step() Error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sql);
      assert(false);
    }
  }
  sqlite3_finalize(stmt);

  if (rv == SQLITE_NOTADB) {
    return false;
  }
  if (rv != SQLITE_DONE
      && rv != SQLITE_ERROR) {
    assert(false);
  }
  return true;
}

static void ChangePrefix(std::string &key, const char *newprefix) {
    key.replace(
          0, new_password_prefix.size(), newprefix);
}


static bool Rekey(sqlite3 *hsqlite_, const char *key, const char *oldkey) {
	assert(key && oldkey);

      if (sqlite3_key(hsqlite_, oldkey, strlen(oldkey)) != 0) {
        ZSLOG_ERROR("sqlite3_key failed");
        return ZISYNC_ERROR_SQLITE;
      }

      if (sqlite3_rekey(hsqlite_, (const void*)key, strlen(key)) != 0) {
        ZSLOG_ERROR("sqlite3_rekey failed");
        return ZISYNC_ERROR_SQLITE;
      }
  
  return true;
}

/************************************************************/
err_t XSQLite::Initialize(const char* db_path, const char *key) {
  CleanUp();

  sqlite_file_ = db_path;

  // Open SQLite3 DB
  if (sqlite3_open(db_path, &hsqlite_) != 0) {
    std::string errmsg;
    StringFormat(&errmsg, "sqlite3_open(%s) failed : %s", db_path,
        sqlite3_errmsg(hsqlite_));
    ZSLOG_ERROR("%s", errmsg.c_str());
    return ZISYNC_ERROR_SQLITE;
  }

  if (key) {
	  int keylen = strlen(key);

    if (sqlite3_key(hsqlite_, key, keylen) != 0) {
      ZSLOG_ERROR("sqlite3_key failed");
      return ZISYNC_ERROR_SQLITE;
    }

  std::string akey = key;
  ChangePrefix(akey, "ytsatsisiht");
  if (!TestKey(hsqlite_)) {
	  Rekey(hsqlite_, key, akey.c_str());
  }
  
  if (!TestKey(hsqlite_)) {
	  Rekey(hsqlite_, key, "test-key");
  }

  assert(TestKey(hsqlite_));


  }



  return ZISYNC_SUCCESS;
}


void XSQLite::CleanUp(bool delete_sqlite_file /* = false */) {
  // Close SQLite3 DB
  int ret;
  if (hsqlite_) {
    // do {
    //   sqlite3_stmt * stmt;
    //   while((stmt = sqlite3_next_stmt(hsqlite_, NULL)) != NULL) {
    //     sqlite3_finalize(stmt);
    //   }

    //   ret = sqlite3_close(hsqlite_);
    // } while (ret != SQLITE_OK);
    do {
    ret = sqlite3_close(hsqlite_);
    } while (ret != SQLITE_OK);
    assert(ret == SQLITE_OK);
    hsqlite_ = NULL;

    if (delete_sqlite_file) {
      OsDeleteFile(sqlite_file_.c_str(), false);
    }
  }
}

err_t XSQLite::ExecSQL(const char* sql) {
  int rv;
  sqlite3_stmt *stmt;
  XSQLiteMutexAuto mutex(hsqlite_);
  std::string errmsg;

  rv = sqlite3_prepare_v2(hsqlite_, sql, -1, &stmt, NULL);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                sqlite3_errmsg(hsqlite_), sql);
  } else {
    /*if (lpBinds) {
      DoBindField(stmt, lpBinds, nCntBinds);
      }*/

    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      StringFormat(&errmsg, "sqlite3_step() Error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sql);
    }
  }
  sqlite3_finalize(stmt);

  if (rv == SQLITE_DONE) {
    return ZISYNC_SUCCESS;
  } else {
    return ZISYNC_ERROR_SQLITE;
  }
}

err_t XSQLite::ExecSQLs(const char* sqls[], int num_sqls) {
  int rv;
  sqlite3_stmt *stmt;
  XSQLiteTransation transaction(hsqlite_);

  for (int i = 0; i < num_sqls; i ++) {
    rv = sqlite3_prepare_v2(hsqlite_, sqls[i], -1, &stmt, NULL);
    if (rv != SQLITE_OK) {
      ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sqls[i]);
    } else {
      /*if (lpBinds) {
        DoBindField(stmt, lpBinds, nCntBinds);
        }*/

      do {
        rv = sqlite3_step(stmt);
      } while (rv == SQLITE_BUSY);

      if (rv != SQLITE_DONE) {
        ZSLOG_ERROR("sqlite3_step() Error : %s; SQL : %s",
                    sqlite3_errmsg(hsqlite_), sqls[i]);
      }
    }
    sqlite3_finalize(stmt);

    if (rv != SQLITE_DONE) {
      transaction.SetAbort(ZISYNC_ERROR_SQLITE);
      return ZISYNC_ERROR_SQLITE;
    }
  }
  return ZISYNC_SUCCESS;
}

bool XSQLite::BeginTransation() {
  int rv;
  char* errmsg;
  rv = sqlite3_exec(GetRawHandle(), "BEGIN TRANSACTION;", NULL, NULL, &errmsg);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("DB Error : %s; SQL: BEGIN TRANSACTION.", errmsg);
    sqlite3_free(errmsg);
    return false;
  }
  return true;
}

bool XSQLite::EndTransation(bool commit /* = true */) {
  int rv;
  char* errmsg;
  const char* sql = commit ? "COMMIT;" : "ROLLBACK;";

  rv = sqlite3_exec(GetRawHandle(), sql, NULL, NULL, &errmsg);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("DB Error : %s; SQL : %s", errmsg, sql);
    sqlite3_free(errmsg);
    sqlite3_exec(GetRawHandle(), "ROLLBACK;", NULL, NULL, NULL);
    return false;
  }
  return true;
}

XSQLiteTransation::XSQLiteTransation(XSQLite* hsqlite)
  : hsqlite_(hsqlite->GetRawHandle()), status_(ZISYNC_SUCCESS) {
    int rv;
    char* errmsg;
    sqlite3* sqlite = hsqlite_;

    mutex_ = sqlite3_db_mutex(sqlite);
    sqlite3_mutex_enter(mutex_);

    rv = sqlite3_exec(sqlite, "BEGIN TRANSACTION;", NULL, NULL, &errmsg);
    if (rv != SQLITE_OK) {
      ZSLOG_ERROR("DB Error : %s; SQL : BEGIN TRANSACTION", errmsg);
      sqlite3_free(errmsg);
      status_ = ZISYNC_ERROR_SQLITE;
    }
  }

XSQLiteTransation::XSQLiteTransation(sqlite3* hsqlite)
  : hsqlite_(hsqlite), status_(ZISYNC_SUCCESS) {
    int rv;
    char* errmsg;
    sqlite3* sqlite = hsqlite;

    mutex_ = sqlite3_db_mutex(sqlite);
    sqlite3_mutex_enter(mutex_);

    rv = sqlite3_exec(sqlite, "BEGIN TRANSACTION;", NULL, NULL, &errmsg);
    if (rv != SQLITE_OK) {
      ZSLOG_ERROR("DB Error : %s; SQL : BEGIN TRANSACTION", errmsg);
      sqlite3_free(errmsg);
      status_ = ZISYNC_ERROR_SQLITE;
    }
  }

XSQLiteTransation::~XSQLiteTransation() {
  int rv;
  char* errmsg;
  const char* sql = (status_ == ZISYNC_SUCCESS) ? "COMMIT;" : "ROLLBACK;";
  sqlite3* sqlite = hsqlite_;

  rv = sqlite3_exec(sqlite, sql, NULL, NULL, &errmsg);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("DB Error : %s; SQL : %s", errmsg, sql);
    sqlite3_free(errmsg);
    sqlite3_exec(sqlite, "ROLLBACK;", NULL, NULL, NULL);
    status_ = ZISYNC_ERROR_SQLITE;
  }

  sqlite3_mutex_leave(mutex_);
}

//
//
//
// ===========================================================================
//
//
//
static void SQLiteBind(
    sqlite3_stmt* stmt, ContentValues* values, Selection* selection) {
  int32_t index = 1;  // SQLite bind index starts from 1
  if (values) {
    int32_t count = values->GetCount();

    for (int i = 0; i < count; i++) {
      ValueType eType = values->GetType(i);
      switch (eType) {
        case AVT_INT32:
          sqlite3_bind_int(stmt, index++, values->GetInt32(i));
          break;

        case AVT_INT64:
          sqlite3_bind_int64(stmt, index++, values->GetInt64(i));
          break;

        case AVT_TEXT:
          sqlite3_bind_text(
              stmt, index++, values->GetString(i), -1, SQLITE_STATIC);
          break;

        case AVT_BLOB:
          sqlite3_bind_blob(
              stmt, index++, values->GetBlobBase(i),
              values->GetBlobSize(i), SQLITE_STATIC);
          break;
        case AVT_LITERAL:
          break;
      }
    }
  }

  if (selection) {
    int32_t count = selection->GetCount();

    for (int i = 0; i < count; i++) {
      ValueType eType = selection->GetType(i);
      switch (eType) {
        case AVT_INT32:
          sqlite3_bind_int(stmt, index++, selection->GetInt32(i));
          break;

        case AVT_INT64:
          sqlite3_bind_int64(stmt, index++, selection->GetInt64(i));
          break;

        case AVT_TEXT:
          sqlite3_bind_text(
              stmt, index++, selection->GetString(i), -1, SQLITE_STATIC);
          break;

        case AVT_BLOB:
          sqlite3_bind_blob(
              stmt, index++, selection->GetBlobBase(i),
              selection->GetBlobSize(i), SQLITE_STATIC);
          break;
        case AVT_LITERAL:
          break;
      }
    }
  }
}

class XSQLiteCursor : public ICursor2 {
 public:
  explicit XSQLiteCursor(sqlite3_stmt* stmt)
      : stmt_(stmt), is_after_last_(false) {
      }
  XSQLiteCursor() : stmt_(NULL), is_after_last_(true) {

  }

  virtual ~XSQLiteCursor() {
    /* virtual destructor */
    if (stmt_ != NULL) {
      sqlite3_finalize(stmt_);
    }
  }

  virtual bool MoveToNext() {
    int rv;
    if (is_after_last_) {
      return false;
    }

    do {
      rv = sqlite3_step(stmt_);
    } while (rv == SQLITE_BUSY);

    if (rv == SQLITE_ROW) {
      return true;
    } else if (rv == SQLITE_DONE) {
      is_after_last_ = true;
      return false;
    } else {
      is_after_last_ = true;
    }

    return false;
  }

  virtual bool IsAfterLast() {
    return is_after_last_;
  }

  virtual bool GetBool(int32_t iCol) {
	  assert(stmt_ != NULL);
	  int value = sqlite3_column_int(stmt_, iCol);
	  assert (value == 0 || value == 1);
	  return value != 0 ? true : false;
  }

  virtual int32_t GetInt32(int32_t iCol) {
    assert(stmt_ != NULL);
    return sqlite3_column_int(stmt_, iCol);
  }

  virtual int64_t GetInt64(int32_t iCol) {
    assert(stmt_ != NULL);
    return sqlite3_column_int64(stmt_, iCol);
  }

  virtual const char*  GetString(int32_t iCol) {
    assert(stmt_ != NULL);
    return (const char*)sqlite3_column_text(stmt_, iCol);
  }

  virtual const void*  GetBlobBase(int32_t iCol) {
    assert(stmt_ != NULL);
    return sqlite3_column_blob(stmt_, iCol);
  }

  virtual int32_t GetBlobSize(int32_t iCol) {
    assert(stmt_ != NULL);
    return sqlite3_column_bytes(stmt_, iCol);
  }

 protected:
  sqlite3_stmt* stmt_;
  bool   is_after_last_;
};

ICursor2* XSQLite::Query(
    const char* table,
    const char* projection[],
    int project_count,
    const char* where) {
  int rv;

  assert(table != NULL);
  assert(projection != NULL && project_count > 0);

  CQueryBuilder QueryBuilder(
      table, projection, project_count, where, NULL);

  sqlite3_stmt* pStmt;

#ifdef ZS_TEST
  if (hsqlite_ == NULL) {
    return new XSQLiteCursor();
  }
#endif

  assert(QueryBuilder.GetSQL() != NULL);
  assert(hsqlite_ != NULL);
  rv = sqlite3_prepare_v2(
      hsqlite_, QueryBuilder.GetSQL(), -1, &pStmt, NULL);
  if (rv != SQLITE_OK) {
    const char* str_error = sqlite3_errmsg(hsqlite_);
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                str_error, QueryBuilder.GetSQL());
    return new XSQLiteCursor();
  }

  return new XSQLiteCursor(pStmt);
}

ICursor2* XSQLite::sQuery(
    const char* table,
    const char* projection[], int project_count,
    Selection* selection, const char* sort_order ) {
  int rv;

  assert(table != NULL);
  assert(projection != NULL && project_count > 0);

  const char* where = NULL;
  if (selection && selection->GetWhere()) {
    where = selection->GetWhere();
  }

  CQueryBuilder QueryBuilder(
      table, projection, project_count, where, sort_order);

  sqlite3_stmt* pStmt;

  rv = sqlite3_prepare_v2(
      hsqlite_, QueryBuilder.GetSQL(), -1, &pStmt, NULL);
  if (rv != SQLITE_OK) {
    const char* str_error = sqlite3_errmsg(hsqlite_);
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                str_error, QueryBuilder.GetSQL());
    return new XSQLiteCursor();
  }

  if (selection && selection->GetWhere()) {
    SQLiteBind(pStmt, NULL, selection);
  }

  return new XSQLiteCursor(pStmt);
}

ICursor2* XSQLite::ExecQuery(const char *sql) {

  int rv;
  XSQLiteMutexAuto mutex(hsqlite_);
  sqlite3_stmt* pStmt;

#ifdef ZS_TEST
  if (hsqlite_ == NULL) {
    return new XSQLiteCursor();
  }
#endif
  assert(sql);
  assert(hsqlite_ != NULL);
  rv = sqlite3_prepare_v2(
      hsqlite_, sql, -1, &pStmt, NULL);
  if (rv != SQLITE_OK) {
    const char* str_error = sqlite3_errmsg(hsqlite_);
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                str_error, sql);
    return new XSQLiteCursor();
  }

  return new XSQLiteCursor(pStmt);
}

int32_t XSQLite::Insert(
    const char* table, ContentValues* values, OpOnConflict on_conflict) {
  int rv;
  sqlite3_stmt *stmt;
  bool ret_code = false;

  std::string ks, vs;

  int32_t count = values->GetCount();
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      ks.append(", ");
      vs.append(", ");
    }

    if (values->GetType(i) == AVT_LITERAL) {
      ks.append(values->GetKey(i));
      vs.append(values->GetString(i));
    } else {
      ks.append(values->GetKey(i));
      vs.append(1, '@');
      vs.append(values->GetKey(i));
    }
  }

  std::string sql;
  switch (on_conflict) {
    case  AOC_ABORT:
      StringFormat(&sql,
                   "INSERT INTO %s (%s) VALUES (%s);",
                   table, ks.c_str(), vs.c_str());
      break;

    case AOC_IGNORE:
      StringFormat(&sql,
                   "INSERT OR IGNORE INTO %s (%s) VALUES (%s);",
                   table, ks.c_str(), vs.c_str());
      break;

    case AOC_REPLACE:
      StringFormat(&sql,
                   "INSERT OR REPLACE INTO %s (%s) VALUES (%s);",
                   table, ks.c_str(), vs.c_str());
      break;

    default:
      assert(false);
  }

  rv = sqlite3_prepare_v2(hsqlite_, sql.c_str(), -1, &stmt, NULL);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                sqlite3_errmsg(hsqlite_), sql.c_str());
  } else {
    if (values) {
      SQLiteBind(stmt, values, NULL);
    }

    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      if (rv == SQLITE_CONSTRAINT  && on_conflict == AOC_ABORT) {
        ZSLOG_WARNING("abort insert due to constraint: SQL: %s", sql.c_str());
      } else {
        ZSLOG_ERROR("sqlite3_step() Error : %s; SQL : %s",
                    sqlite3_errmsg(hsqlite_), sql.c_str());
      }
    } else {
      ret_code = true;
    }
  }
  sqlite3_finalize(stmt);


  if (ret_code) {
    return static_cast<int>(sqlite3_last_insert_rowid(hsqlite_));
  } else {
    return -1L;
  }
}

int32_t XSQLite::Update(
    const char* table, ContentValues* values, const char* where) {
  int rv;
  sqlite3_stmt *stmt;
  bool ret_code = false;

  std::string os;
  int32_t count = values->GetCount();
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      os.append(", ");
    }
    if (values->GetType(i) == AVT_LITERAL) {
      os.append(values->GetKey(i));
      os.append("=");
      os.append(values->GetString(i));
    } else {
      os.append(values->GetKey(i));
      os.append("=@");
      os.append(values->GetKey(i));
    }
  }

  std::string sql;
  if (where != NULL) {
    StringFormat(&sql,
                 "UPDATE %s SET %s WHERE %s;", table, os.c_str(), where);
  } else {
    StringFormat(&sql, "UPDATE %s SET %s;", table, os.c_str());
  }

  rv = sqlite3_prepare_v2(hsqlite_, sql.c_str(), -1, &stmt, NULL);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                sqlite3_errmsg(hsqlite_), sql.c_str());
  } else {
    if (values) {
      SQLiteBind(stmt, values, NULL);
    }

    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      ZSLOG_ERROR("sqlite3_step() Error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sql.c_str());
    } else {
      ret_code = true;
    }
  }

  sqlite3_finalize(stmt);

  if (ret_code) {
    return sqlite3_changes(hsqlite_);
  } else {
    return -1L;
  }
}

int32_t XSQLite::sUpdate(
    const char* table, ContentValues* values, Selection* selection ) {
  int rv;
  sqlite3_stmt *stmt;
  bool ret_code = false;

  std::string os;
  int32_t count = values->GetCount();
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      os.append(", ");
    }
    if (values->GetType(i) == AVT_LITERAL) {
      os.append(values->GetKey(i));
      os.append("=");
      os.append(values->GetString(i));
    } else {
      os.append(values->GetKey(i));
      os.append("=@");
      os.append(values->GetKey(i));
    }
  }

  std::string sql;
  if (selection != NULL && selection->GetWhere()) {
    StringFormat(&sql, "UPDATE %s SET %s WHERE %s;",
                 table, os.c_str(), selection->GetWhere());
  } else {
    StringFormat(&sql, "UPDATE %s SET %s;", table, os.c_str());
  }

  rv = sqlite3_prepare_v2(hsqlite_, sql.c_str(), -1, &stmt, NULL);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                sqlite3_errmsg(hsqlite_), sql.c_str());
  } else {
    if (values || (selection && selection->GetWhere())) {
      SQLiteBind(stmt, values, selection);
    }

    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      ZSLOG_ERROR("sqlite3_step() Error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sql.c_str());
    } else {
      ret_code = true;
    }
  }

  sqlite3_finalize(stmt);

  if (ret_code) {
    return sqlite3_changes(hsqlite_);
  } else {
    return -1L;
  }
}


int32_t XSQLite::Delete(const char* table, const char* where) {
  int rv;
  sqlite3_stmt *stmt;
  bool ret_code = false;

  std::string sql;
  if (where != NULL) {
    StringFormat(&sql, "DELETE FROM %s WHERE %s;", table, where);
  } else {
    StringFormat(&sql, "DELETE FROM %s;", table);
  }

  rv = sqlite3_prepare_v2(hsqlite_, sql.c_str(), -1, &stmt, NULL);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("sqlite3_prepare_v2() eror : %s; SQL : %s",
                sqlite3_errmsg(hsqlite_), sql.c_str());
  } else {
    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      ZSLOG_ERROR("sqlite3_step() Error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sql.c_str());
    } else {
      ret_code = true;
    }
  }

  sqlite3_finalize(stmt);

  if (ret_code) {
    return sqlite3_changes(hsqlite_);
  } else {
    return -1L;
  }
}

int32_t XSQLite::sDelete(const char* table, Selection* selection) {
  int rv;
  sqlite3_stmt *stmt;
  bool ret_code = false;

  std::string sql;
  if (selection != NULL && selection->GetWhere()) {
    StringFormat(&sql,
                 "DELETE FROM %s WHERE %s;", table, selection->GetWhere());
  } else {
    StringFormat(&sql, "DELETE FROM %s;", table);
  }

  rv = sqlite3_prepare_v2(hsqlite_, sql.c_str(), -1, &stmt, NULL);
  if (rv != SQLITE_OK) {
    ZSLOG_ERROR("sqlite3_prepare_v2() error : %s; SQL : %s",
                sqlite3_errmsg(hsqlite_), sql.c_str());
  } else {
    if (selection && selection->GetWhere()) {
      SQLiteBind(stmt, NULL, selection);
    }

    do {
      rv = sqlite3_step(stmt);
    } while (rv == SQLITE_BUSY);

    if (rv != SQLITE_DONE) {
      ZSLOG_ERROR("sqlite3_step() Error : %s; SQL : %s",
                  sqlite3_errmsg(hsqlite_), sql.c_str());
    } else {
      ret_code = true;
    }
  }

  sqlite3_finalize(stmt);

  if (ret_code) {
    return sqlite3_changes(hsqlite_);
  } else {
    return -1L;
  }
}

}  // namespace zs
