/* Copyright [2014] <zisync.com> */
#ifndef ZISYNC_KERNEL_DATABASE_XSQLITE_H_
#define ZISYNC_KERNEL_DATABASE_XSQLITE_H_

#include <stdint.h>
#include <sqlcipher/sqlite3.h>
#include <cassert>

#include <string>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class XSQLite {
 public:
  XSQLite(void);
  ~XSQLite(void);

  err_t Initialize(const char* db_path, const char *key = NULL);
  err_t Initialize(const std::string& db_path, const char *key = NULL) {
    return Initialize(db_path.c_str(), key);
  }
  void    CleanUp(bool delete_sqlite_file = false);

  sqlite3* GetRawHandle() { return hsqlite_; }

  // Interfaces
 public:
  err_t ExecSQL(const char* sql);
  err_t ExecSQLs(const char* sqls[], int num_sqls);
  ICursor2* ExecQuery(const char* sql);

  bool BeginTransation();
  bool EndTransation(bool commit = true);

  virtual ICursor2* Query(
      const char* table,
      const char* projection[],
      int project_count,
      const char* where);

  virtual ICursor2* sQuery(
      const char* table,
      const char* projection[], int project_count,
      Selection* selection, const char* sort_order);

  virtual int32_t Insert(
      const char* table,
      ContentValues* values,
      OpOnConflict on_conflict);

  virtual int32_t Update(
      const char* table,
      ContentValues* values,
      const char* where);

  virtual int32_t sUpdate(
      const char* table,
      ContentValues* values,
      Selection* selection);

  virtual int32_t Delete(
      const char* table,
      const char* where);

  virtual int32_t sDelete(
      const char* table,
      Selection* selection);

  const std::string& sqlite_file() { return sqlite_file_; }

 protected:
  sqlite3 *hsqlite_;
  std::string sqlite_file_;

};


class XSQLiteMutexAuto {
  sqlite3_mutex* mutex_;

 public:
  explicit XSQLiteMutexAuto(sqlite3* hsqlite) {
    mutex_ = sqlite3_db_mutex(hsqlite);
    sqlite3_mutex_enter(mutex_);
  }

  explicit XSQLiteMutexAuto(XSQLite* hsqlite) {
    assert(hsqlite->GetRawHandle() != NULL);
    mutex_ = sqlite3_db_mutex(hsqlite->GetRawHandle());
    sqlite3_mutex_enter(mutex_);
  }

  ~XSQLiteMutexAuto() {
    sqlite3_mutex_leave(mutex_);
  }
};

class XSQLiteMutexEnter {
  sqlite3_mutex* mutex_;

 public:
  explicit XSQLiteMutexEnter(XSQLite* hsqlite) {
    assert(hsqlite->GetRawHandle() != NULL);
    mutex_ = sqlite3_db_mutex(hsqlite->GetRawHandle());
    sqlite3_mutex_enter(mutex_);
  }

  ~XSQLiteMutexEnter() {}
};

class XSQLiteTransation {
  sqlite3*       hsqlite_;
  sqlite3_mutex* mutex_;
  int          status_;

 public:
  explicit XSQLiteTransation(sqlite3* hsqlite);
  explicit XSQLiteTransation(XSQLite* hsqlite);
  virtual ~XSQLiteTransation();

  void SetAbort(int status) { status_ = status; }
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_DATABASE_XSQLITE_H_
