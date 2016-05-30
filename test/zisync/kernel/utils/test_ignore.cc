// Copyright 2014, zisync.com

#include <UnitTest++/UnitTest++.h>

#include "zisync/kernel/utils/ignore.h"

using std::string;

using zs::IsIgnoreDir;
using zs::IsIgnoreFile;
using zs::IsInIgnoreDir;

TEST(IsIgnoreFile) {
  CHECK_EQUAL(true, IsIgnoreFile("/.zisync.meta"));
  CHECK_EQUAL(true, IsIgnoreFile("/test/.zisync.meta"));
  CHECK_EQUAL(false, IsIgnoreFile("/test/.zisync.meta3"));
}

TEST(IsIgnoreDir) {
  CHECK_EQUAL(true, IsIgnoreDir("/.zstm"));
  CHECK_EQUAL(true, IsIgnoreDir("/test/.zstm"));
  CHECK_EQUAL(true, IsIgnoreDir("/.zstm123"));
  CHECK_EQUAL(true, IsIgnoreDir("/test/.zstm234"));
  
  CHECK_EQUAL(false, IsIgnoreDir("/.zst"));
  CHECK_EQUAL(false, IsIgnoreDir("/test/.zst"));
  CHECK_EQUAL(false, IsIgnoreDir("/.zst123"));
  CHECK_EQUAL(false, IsIgnoreDir("/test/.zst234"));
  
  CHECK_EQUAL(true, IsIgnoreDir("/$RECYCLE.BIN"));
  CHECK_EQUAL(true, IsIgnoreDir("/test/$RECYCLE.BIN"));
  CHECK_EQUAL(true, IsIgnoreDir("E:/$RECYCLE.BIN"));
  CHECK_EQUAL(false, IsIgnoreDir("/$RECYCLE.BIN3"));
  CHECK_EQUAL(false, IsIgnoreDir("/test/$RECYCLE.BIN3"));
  CHECK_EQUAL(false, IsIgnoreDir("E:/$RECYCLE.BIN3"));
}

TEST(IsInIgnoreDir) {
  CHECK_EQUAL(true, IsInIgnoreDir("/.zstm"));
  CHECK_EQUAL(true, IsInIgnoreDir("/.zstm123"));
  CHECK_EQUAL(false, IsInIgnoreDir("/test/.zstm"));
  
  CHECK_EQUAL(true, IsInIgnoreDir("/.zstm/test"));
  CHECK_EQUAL(true, IsInIgnoreDir("/.zstm123/test"));
  
  CHECK_EQUAL(false, IsInIgnoreDir("/.zst"));
  CHECK_EQUAL(false, IsInIgnoreDir("/.zst123"));
  
  CHECK_EQUAL(false, IsInIgnoreDir("/.zst/test"));
  CHECK_EQUAL(false, IsInIgnoreDir("/.zst123/test"));
  
  CHECK_EQUAL(true, IsInIgnoreDir("/$RECYCLE.BIN"));
  
  CHECK_EQUAL(true, IsInIgnoreDir("/$RECYCLE.BIN/test"));

  CHECK_EQUAL(false, IsInIgnoreDir("/$RECYCLE.BIN3"));
  
  CHECK_EQUAL(false, IsInIgnoreDir("/$RECYCLE.BIN3/test"));
  
  CHECK_EQUAL(true, IsInIgnoreDir("/RECYCLER"));
  
  CHECK_EQUAL(true, IsInIgnoreDir("/RECYCLER/test"));

  CHECK_EQUAL(false, IsInIgnoreDir("/RECYCLER3"));
  
  CHECK_EQUAL(false, IsInIgnoreDir("/RECYCLER3/test"));
}
