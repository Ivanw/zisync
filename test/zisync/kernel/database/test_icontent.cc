/**
 * @file test_icontent.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test case for icontent.cc.
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

#include <cstring>
#include <UnitTest++/UnitTest++.h>  // NOLINT

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"


#ifdef _MSC_VER
/*MSVC*/
#endif
#ifdef __GNUC__
/*GCC*/
using zs::err_t;
#endif

using zs::ContentValues;
using zs::Selection;
using zs::Uri;

TEST(test_ContentValues) {
  int32_t id = 1;
  int64_t length = 0xFFFFFFFFFFFF;
  const char* name = "hello";
  char data[512] = {
    '\0',
  };
  const char* literal = "count+1";

  ContentValues cv(5);

  cv.Put("id", id);
  cv.Put("length", length);
  cv.Put("uuid", name, true);
  cv.Put("data", data, 512, true);
  cv.PutLiteral("count", literal, true);

  CHECK_EQUAL(5, cv.GetCount());
  CHECK_EQUAL(0, strcmp(cv.GetKey(0), "id"));
  CHECK_EQUAL(zs::AVT_INT32, cv.GetType(0));
  CHECK_EQUAL(id, cv.GetInt32(0));

  CHECK_EQUAL(0, strcmp(cv.GetKey(1), "length"));
  CHECK_EQUAL(zs::AVT_INT64, cv.GetType(1));
  CHECK_EQUAL(length, cv.GetInt64(1));

  CHECK_EQUAL(0, strcmp(cv.GetKey(2), "uuid"));
  CHECK_EQUAL(zs::AVT_TEXT, cv.GetType(2));
  CHECK_EQUAL(0, strcmp(cv.GetString(2), name) );

  CHECK_EQUAL(0, strcmp(cv.GetKey(3), "data"));
  CHECK_EQUAL(zs::AVT_BLOB, cv.GetType(3));
  CHECK_EQUAL(512, cv.GetBlobSize(3));
  CHECK_EQUAL(0, memcmp(cv.GetBlobBase(3), data, 512));

  CHECK_EQUAL(0, strcmp(cv.GetKey(4), "count"));
  CHECK_EQUAL(zs::AVT_LITERAL, cv.GetType(4));
  CHECK_EQUAL(0, strcmp(cv.GetString(4), literal));
}

TEST(test_Selection) {
  int32_t id = 1;
  int64_t length = 0xFFFFFFFFFFFF;
  const char* name = "hello";
  char data[512] = {
    '\0',
  };
  const char* fmt =
      "a = %d AND id = ? AND length = ? AND name = ? And data = ?";

  Selection selection(fmt, 123);

  selection.Add(id);
  selection.Add(length);
  selection.Add(name, true);
  selection.Add(data, 512, true);

  CHECK_EQUAL(4, selection.GetCount());
  CHECK_EQUAL(zs::AVT_INT32, selection.GetType(0));
  CHECK_EQUAL(id, selection.GetInt32(0));

  CHECK_EQUAL(zs::AVT_INT64, selection.GetType(1));
  CHECK_EQUAL(length, selection.GetInt64(1));

  CHECK_EQUAL(zs::AVT_TEXT, selection.GetType(2));
  CHECK_EQUAL(0, strcmp(selection.GetString(2), name) );

  CHECK_EQUAL(zs::AVT_BLOB, selection.GetType(3));
  CHECK_EQUAL(512, selection.GetBlobSize(3));
  CHECK_EQUAL(0, memcmp(selection.GetBlobBase(3), data, 512));

  CHECK(strcmp(
      selection.GetWhere(),
      "a = 123 AND id = ? AND length = ? AND name = ? And data = ?") == 0);
}

TEST(test_Uri) {
  Uri uri("content://zisync/%s", "file");

  CHECK_EQUAL(0, strcmp(uri.AsString(), "content://zisync/file"));
  CHECK_EQUAL(0, strcmp(uri.GetSchema(), "content"));
  CHECK_EQUAL(0, strcmp(uri.GetAuthority(), "zisync"));
  CHECK_EQUAL(0, strcmp(uri.GetPath(), "/file"));
  std::string last_segment = uri.GetLastPathSegment();
  CHECK_EQUAL("file", last_segment);

  Uri uri2("content", "zisync", "/file");

  CHECK_EQUAL(0, strcmp(uri2.AsString(), "content://zisync/file"));
  CHECK_EQUAL(0, strcmp(uri2.GetSchema(), "content"));
  CHECK_EQUAL(0, strcmp(uri2.GetAuthority(), "zisync"));
  CHECK_EQUAL(0, strcmp(uri2.GetPath(), "/file"));
  std::string last_segment2 = uri2.GetLastPathSegment();
  CHECK_EQUAL("file", last_segment2);

  CHECK(uri2 == uri);
}
