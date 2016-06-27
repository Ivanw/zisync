/**
 * @file test_content_provider.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test cases for content_provider.cc.
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
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"

using zs::err_t;
using zs::ContentValues;
using zs::ICursor2;
using zs::Selection;
using zs::ContentProvider;
using zs::IContentProvider;
using zs::TableDevice;

TEST(test_ContentProvider) {
  const char* app_path = ".";
  zs::OsDeleteFile("ZiSync.db", false);

  IContentProvider* provider = new ContentProvider();

  err_t eno = provider->OnCreate(app_path);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  // Test Insert()
  ContentValues cv(8);

  cv.Put("uuid", "uuid1");
  cv.Put("name", "name1");
  cv.Put("route_port", 9527);
  cv.Put("data_port", 9526);
  cv.Put(TableDevice::COLUMN_TYPE, 1);
  cv.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE);
  cv.Put(TableDevice::COLUMN_IS_MINE, true);

  int64_t rowid = provider->Insert(TableDevice::URI, &cv, zs::AOC_REPLACE);
  // printf("rowid: %" PRId64 "\n", rowid);
  CHECK(rowid > 0);

  ContentValues cv2(8);

  cv2.Put("uuid", "uuid2");
  cv2.Put("name", "name2");
  cv2.Put("route_port", 9527);
  cv2.Put("data_port", 9526);
  cv2.Put(TableDevice::COLUMN_TYPE, 1);
  cv2.Put(TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE);
  cv2.Put(TableDevice::COLUMN_IS_MINE, true);

  rowid = provider->Insert(TableDevice::URI, &cv2, zs::AOC_REPLACE);
  // printf("rowid: %" PRId64 "\n", rowid);
  CHECK(rowid > 0);

  // Test Query()
  const char* projection[] = {
    "id", "uuid", "name", "route_port", "data_port",
  };
  std::unique_ptr<ICursor2> cursor(
      provider->Query(TableDevice::URI, projection, ARRAY_SIZE(projection),
                   "uuid = 'uuid1'"));

  CHECK(cursor->MoveToNext());

  // printf("id: %" PRId64 "\n", cursor->GetInt64(0));
  CHECK(cursor->GetInt64(0) > 0);
  CHECK_EQUAL(cursor->GetString(1), std::string("uuid1"));
  CHECK_EQUAL(cursor->GetString(2), std::string("name1"));
  CHECK_EQUAL(cursor->GetInt32(3), 9527);
  CHECK_EQUAL(cursor->GetInt32(4), 9526);
  cursor.reset(NULL);

  // Test sQuery()
  Selection select("id > %d AND name = ? ", 0);
  select.Add("name2");
  std::unique_ptr<ICursor2> cursor2(
      provider->sQuery(TableDevice::URI, projection,
                    ARRAY_SIZE(projection), &select, NULL));

  CHECK(cursor2->MoveToNext());

  // printf("id: %" PRId64 "\n", cursor2->GetInt64(0));
  CHECK(cursor2->GetInt64(0) > 0);
  CHECK_EQUAL(cursor2->GetString(1), std::string("uuid2"));
  CHECK_EQUAL(cursor2->GetString(2), std::string("name2"));
  CHECK_EQUAL(cursor2->GetInt32(3), 9527);
  CHECK_EQUAL(cursor2->GetInt32(4), 9526);
  cursor2.reset(NULL);

  // Test Update
  ContentValues cv3(2);
  cv3.Put("name", "name3");

  int num_affected_row = provider->Update(TableDevice::URI, &cv3, "uuid = 'uuid1'");
  CHECK_EQUAL(1, num_affected_row);

  const char* projection3[] = {
    "uuid", "name"
  };
  std::unique_ptr<ICursor2> cursor3(
      provider->Query(TableDevice::URI, projection3, ARRAY_SIZE(projection3),
                   "uuid = 'uuid1'"));

  CHECK(cursor3->MoveToNext());
  CHECK_EQUAL(cursor3->GetString(0), std::string("uuid1"));
  CHECK_EQUAL(cursor3->GetString(1), std::string("name3"));
  cursor3.reset(NULL);

  // Test sUpdate
  ContentValues cv4(2);
  cv4.Put("name", "name4");

  Selection select4("uuid = ? ");
  select4.Add("uuid2");
  num_affected_row = provider->sUpdate(TableDevice::URI, &cv4, &select4);
  CHECK_EQUAL(1, num_affected_row);

  const char* projection4[] = {
    "uuid", "name"
  };
  std::unique_ptr<ICursor2> cursor4(
      provider->Query(TableDevice::URI, projection4, ARRAY_SIZE(projection4),
                   "uuid = 'uuid2'"));

  CHECK(cursor4->MoveToNext());
  CHECK_EQUAL(cursor4->GetString(0), std::string("uuid2"));
  CHECK_EQUAL(cursor4->GetString(1), std::string("name4"));
  cursor4.reset(NULL);

  // Test Delete()
  num_affected_row = provider->Delete(TableDevice::URI, "uuid = 'uuid1'");
  CHECK_EQUAL(1, num_affected_row);

  // Test sDelete()
  Selection selection6("uuid = 'uuid2'");
  num_affected_row = provider->sDelete(TableDevice::URI, &selection6);
  CHECK_EQUAL(1, num_affected_row);

}
