/**
 * @file test_xsqlite.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test cases for xsqlite.cc.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#include <stdio.h>
#include <UnitTest++/UnitTest++.h>

#include <memory>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/xsqlite.h"

using zs::err_t;
using zs::XSQLite;
using zs::ContentValues;
using zs::ICursor2;
using zs::Selection;

TEST(test_XSQLite) {
  const char* sqlite_file_path = "test_xsqlite.db";

  XSQLite sqlite;
  err_t eno = sqlite.Initialize(sqlite_file_path);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  // Test database init
  const char* sql_init_db[] = {
    /* Create MetaRepoDB tables */
    "PRAGMA foreign_keys = ON;",

    "CREATE TABLE IF NOT EXISTS Device ("                 \
      " id        INTEGER PRIMARY KEY AUTOINCREMENT,"     \
      " uuid      VARCHAR(40) UNIQUE,"                    \
      " name      VARCHAR(32),"                           \
      " type      INTEGER,"                               \
      " class     INTEGER,"                               \
      " online    INTEGER,"                               \
      " route_uri VARCHAR(255),"                          \
      " pub_uri   VARCHAR(255),"                          \
      " trust     INTEGER,"                               \
      " pubkey    TEXT"                                   \
    ");",

    "CREATE TABLE IF NOT EXISTS DeviceTTL("                               \
       "id INTEGER PRIMARY KEY REFERENCES Device(id) ON DELETE CASCADE, " \
       "ttl INTEGER"                                                      \
    ");",

    "CREATE TABLE IF NOT EXISTS Tree ("              \
       " id          INTEGER PRIMARY KEY AUTOINCREMENT,"   \
       " device_uuid VARCHAR(40),"                         \
       " tree_uuid   VARCHAR(40) UNIQUE,"                  \
       " tree_root   VARCHAR(255) UNIQUE  COLLATE NOCASE," \
       " sync_uuid   VARCHAR(40)"                          \
    ");",

    "CREATE TABLE IF NOT EXISTS File ("                        \
      " id          INTEGER PRIMARY KEY AUTOINCREMENT,"        \
      " type        INTEGER,"                                  \
      " status      INTEGER,"                                  \
      " mtime       INT64,"                                    \
      " length      INT64,"                                    \
      " usn         INT64,"                                    \
      " sha1        VARCHAR(40),"                              \
      " modifier    INTEGER,"                                  \
      " path        VARCHAR(255),"                             \
      " win_attr    INTEGER, "                                 \
      " unix_attr   INTEGER, "                                 \
      " andr_attr   INTEGER, "                                 \
      " tree_id     INTEGER, "                                 \
      " local_clock INTEGER,"                                  \
      " other_clock BLOB,"                                     \
      "UNIQUE(tree_id, path)"                                  \
    ");",

    "CREATE TABLE IF NOT EXISTS VCUuid ("               \
      " tree_id     INTEGER,"                            \
      " clock_uuid  VARCHAR(40) ,"                       \
      " clock_index INTEGER,"                            \
    "UNIQUE(tree_id, clock_uuid)" \
    ");",

    "CREATE INDEX IF NOT EXISTS PathIndex ON File(path DESC);",

    "CREATE TRIGGER IF NOT EXISTS DevTTLInsert AFTER INSERT ON Device" \
       " BEGIN INSERT INTO DeviceTTL(id, ttl) VALUES(new.id, 3); END;",

    "CREATE TRIGGER IF NOT EXISTS TreeDelete AFTER DELETE ON Tree " \
      " BEGIN DELETE FROM File WHERE tree = old.id; END;",
  };

  for (size_t i = 0; i < ARRAY_SIZE(sql_init_db); i++) {
    err_t eno = sqlite.ExecSQL(sql_init_db[i]);
    // printf("[%lu] %s\n", i, sql_init_db[i]);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  }

  // Test Insert()
  ContentValues cv(8);

  cv.Put("uuid", "uuid1");
  cv.Put("name", "name1");
  cv.Put("type", 0);
  cv.Put("class", 0);
  cv.Put("online", 0);
  cv.Put("route_uri", "tcp://localhost:9527");
  cv.Put("pub_uri", "tcp://localhost:9526");
  cv.Put("trust", 1);
  cv.Put("pubkey", "pubkey1");

  int64_t rowid = sqlite.Insert("Device", &cv, zs::AOC_REPLACE);
  // printf("rowid: %" PRId64 "\n", rowid);
  CHECK(rowid > 0);

  ContentValues cv2(8);

  cv2.Put("uuid", "uuid2");
  cv2.Put("name", "name2");
  cv2.Put("type", 0);
  cv2.Put("class", 0);
  cv2.Put("online", 0);
  cv2.Put("route_uri", "tcp://localhost:9527");
  cv2.Put("pub_uri", "tcp://localhost:9526");
  cv2.Put("trust", 1);
  cv2.Put("pubkey", "pubkey2");

  rowid = sqlite.Insert("Device", &cv2, zs::AOC_REPLACE);
  // printf("rowid: %" PRId64 "\n", rowid);
  CHECK(rowid > 0);

  // Test Query()
  const char* projection[] = {
    "id", "uuid", "name", "type", "class", "online",
    "route_uri", "pub_uri", "trust", "pubkey"
  };
  std::unique_ptr<ICursor2> cursor(
      sqlite.Query("Device", projection, ARRAY_SIZE(projection),
                   "uuid = 'uuid1'"));

  CHECK(cursor->MoveToNext());

  // printf("id: %" PRId64 "\n", cursor->GetInt64(0));
  CHECK(cursor->GetInt64(0) > 0);
  CHECK_EQUAL(cursor->GetString(1), std::string("uuid1"));
  CHECK_EQUAL(cursor->GetString(2), std::string("name1"));
  CHECK_EQUAL(cursor->GetInt32(3), 0);
  CHECK_EQUAL(cursor->GetInt32(4), 0);
  CHECK_EQUAL(cursor->GetInt32(5), 0);
  CHECK_EQUAL(cursor->GetString(6), std::string("tcp://localhost:9527"));
  CHECK_EQUAL(cursor->GetString(7), std::string("tcp://localhost:9526"));
  CHECK_EQUAL(cursor->GetInt32(8), 1);
  CHECK_EQUAL(cursor->GetString(9), std::string("pubkey1"));
  cursor.reset(NULL);

  // Test sQuery()
  Selection select("id > %d AND name = ? ", 0);
  select.Add("name2");
  std::unique_ptr<ICursor2> cursor2(
      sqlite.sQuery("Device", projection,
                    ARRAY_SIZE(projection), &select, NULL));

  CHECK(cursor2->MoveToNext());

  // printf("id: %" PRId64 "\n", cursor2->GetInt64(0));
  CHECK(cursor2->GetInt64(0) > 0);
  CHECK_EQUAL(cursor2->GetString(1), std::string("uuid2"));
  CHECK_EQUAL(cursor2->GetString(2), std::string("name2"));
  CHECK_EQUAL(cursor2->GetInt32(3), 0);
  CHECK_EQUAL(cursor2->GetInt32(4), 0);
  CHECK_EQUAL(cursor2->GetInt32(5), 0);
  CHECK_EQUAL(cursor2->GetString(6), std::string("tcp://localhost:9527"));
  CHECK_EQUAL(cursor2->GetString(7), std::string("tcp://localhost:9526"));
  CHECK_EQUAL(cursor2->GetInt32(8), 1);
  CHECK_EQUAL(cursor2->GetString(9), std::string("pubkey2"));
  cursor2.reset(NULL);

  // Test Update
  ContentValues cv3(2);
  cv3.Put("name", "name3");
  cv3.Put("pubkey", "pubkey3");

  int num_affected_row = sqlite.Update("Device", &cv3, "uuid = 'uuid1'");
  CHECK_EQUAL(1, num_affected_row);

  const char* projection3[] = {
    "uuid", "name", "pubkey"
  };
  std::unique_ptr<ICursor2> cursor3(
      sqlite.Query("Device", projection3, ARRAY_SIZE(projection3),
                   "uuid = 'uuid1'"));

  CHECK(cursor3->MoveToNext());
  CHECK_EQUAL(cursor3->GetString(0), std::string("uuid1"));
  CHECK_EQUAL(cursor3->GetString(1), std::string("name3"));
  CHECK_EQUAL(cursor3->GetString(2), std::string("pubkey3"));
  cursor3.reset(NULL);

  // Test sUpdate
  ContentValues cv4(2);
  cv4.Put("name", "name4");
  cv4.Put("pubkey", "pubkey4");

  Selection select4("uuid = ? ");
  select4.Add("uuid2");
  num_affected_row = sqlite.sUpdate("Device", &cv4, &select4);
  CHECK_EQUAL(1, num_affected_row);

  const char* projection4[] = {
    "uuid", "name", "pubkey"
  };
  std::unique_ptr<ICursor2> cursor4(
      sqlite.Query("Device", projection4, ARRAY_SIZE(projection4),
                   "uuid = 'uuid2'"));

  CHECK(cursor4->MoveToNext());
  CHECK_EQUAL(cursor4->GetString(0), std::string("uuid2"));
  CHECK_EQUAL(cursor4->GetString(1), std::string("name4"));
  CHECK_EQUAL(cursor4->GetString(2), std::string("pubkey4"));
  cursor4.reset(NULL);

  // Test Delete()
  num_affected_row = sqlite.Delete("Device", "uuid = 'uuid1'");
  CHECK_EQUAL(1, num_affected_row);

  // Test sDelete()
  Selection selection6("uuid = 'uuid2'");
  num_affected_row = sqlite.sDelete("Device", &selection6);
  CHECK_EQUAL(1, num_affected_row);

  // Test CleanUp()
  sqlite.CleanUp(true);
}

