/****************************************************************************
 *       Filename:  test_tar_writer.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  02/05/15 10:50:50
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  PangHai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/
#include <UnitTest++/UnitTest++.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <memory>
#include <iostream>

#include "zisync/kernel/test_tool_func.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/libevent/tar_writer.h"
#include "zisync/kernel/zslog.h"
#include "zisync_kernel.h"

using zs::FileTarWriter;
using zs::FileHeadTarReader;
using zs::FileDataTarReader;
using zs::err_t;
using std::string;

FileTarWriter *writer = NULL;
FileHeadTarReader *head_reader = NULL;
FileDataTarReader *data_reader = NULL;
struct event_base *event_base = NULL;
struct bufferevent *pair[2];

err_t WriterReset(std::string real_path, std::string encode_path) {
  return writer->Reset(real_path, string(), encode_path);
}

TEST(test_FileTarWriterEmptyfile) {
  std::cout << "----------empty file----------" << std::endl;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
              WriterReset("libevent/files/empty_file", "empty_file"));
  writer->WriteHead(pair[0]);
  int ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
  assert(ret >= 0);
  head_reader->Reset();
  err_t zisync_reader = zs::ZISYNC_SUCCESS;
  err_t zisync_writer = zs::ZISYNC_SUCCESS;
  do {
    zisync_reader = head_reader->ParseMore(pair[1]);
  } while (zisync_reader == zs::ZISYNC_ERROR_AGAIN);

  std::string real_path("libevent/tmp_dir/empty_file");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, data_reader->Reset(*head_reader, real_path, string()));

  do {
    zisync_writer = writer->WriteMore(pair[0], NULL);
    ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
    assert(ret >= 0);
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  } while (zisync_writer == zs::ZISYNC_ERROR_AGAIN);

  assert(zisync_writer == zs::ZISYNC_SUCCESS);

  while (zisync_reader == zs::ZISYNC_ERROR_AGAIN) {
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  }

  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_reader);
  CHECK(zs::FileIsEqual("libevent/files/empty_file", "libevent/tmp_dir/empty_file"));
  std::string sha11;
  std::string path("libevent/files/empty_file");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(path, string(), &sha11));
  CHECK(sha11 == data_reader->sha1());
}

TEST(test_FileTarWriterNormalfile) {
  std::cout << "----------normal file----------" << std::endl;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
              WriterReset("libevent/files/file1", "file1"));
  writer->WriteHead(pair[0]);
  int ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
  assert(ret >= 0);
  head_reader->Reset();
  err_t zisync_reader = zs::ZISYNC_SUCCESS;
  err_t zisync_writer = zs::ZISYNC_SUCCESS;
  do {
    zisync_reader = head_reader->ParseMore(pair[1]);
  } while (zisync_reader == zs::ZISYNC_ERROR_AGAIN);

  std::string real_path("libevent/tmp_dir/file1");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, data_reader->Reset(*head_reader, real_path, string()));

  do {
    zisync_writer = writer->WriteMore(pair[0], NULL);
    ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
    assert(ret >= 0);
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  } while (zisync_writer == zs::ZISYNC_ERROR_AGAIN);

  while (zisync_reader == zs::ZISYNC_ERROR_AGAIN) {
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  }

  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_reader);
  CHECK(zs::FileIsEqual("libevent/files/file1", "libevent/tmp_dir/file1"));
  std::string sha11;
  std::string path("libevent/files/file1");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(path, string(), &sha11));
  CHECK(sha11 == data_reader->sha1());
}
TEST(test_FileTarWriterLongname) {
  std::string longname="1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111";
  std::cout << "----------longname file----------" << std::endl;
  std::string longpath;
  longpath = "libevent/files/" + longname;

  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
              WriterReset(longpath, longname));
  writer->WriteHead(pair[0]);
  int ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
  assert(ret >= 0);
  head_reader->Reset();
  err_t zisync_reader = zs::ZISYNC_SUCCESS;
  err_t zisync_writer = zs::ZISYNC_SUCCESS;
  do {
    zisync_reader = head_reader->ParseMore(pair[1]);
  } while (zisync_reader == zs::ZISYNC_ERROR_AGAIN);

  std::string destpath;
  destpath = "libevent/tmp_dir/" + longname;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, data_reader->Reset(*head_reader, destpath, string()));

  do {
    zisync_writer = writer->WriteMore(pair[0], NULL);
    ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
    assert(ret >= 0);
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  } while (zisync_writer == zs::ZISYNC_ERROR_AGAIN);

  while (zisync_reader == zs::ZISYNC_ERROR_AGAIN) {
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  }

  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_reader);

  std::string srcpath;
  srcpath = "libevent/files/" + longname;
  CHECK(zs::FileIsEqual(srcpath.c_str(), destpath.c_str()));
  std::string sha11;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(srcpath, string(), &sha11));
  CHECK(sha11 == data_reader->sha1());
}

TEST(test_FileTarWriterLargefile) {
  std::cout << "----------large file----------" << std::endl;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
              WriterReset("libevent/files/file_4G", "file_4G"));
  writer->WriteHead(pair[0]);
  int ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
  assert(ret >= 0);
  head_reader->Reset();
  err_t zisync_reader = zs::ZISYNC_SUCCESS;
  err_t zisync_writer = zs::ZISYNC_SUCCESS;
  do {
    zisync_reader = head_reader->ParseMore(pair[1]);
  } while (zisync_reader == zs::ZISYNC_ERROR_AGAIN);

  std::string real_path("libevent/tmp_dir/file_4G");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, data_reader->Reset(*head_reader, real_path, string()));

  do {
    zisync_writer = writer->WriteMore(pair[0], NULL);
    ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
    assert(ret >= 0);
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  } while (zisync_writer == zs::ZISYNC_ERROR_AGAIN);

  while (zisync_reader == zs::ZISYNC_ERROR_AGAIN) {
    zisync_reader = data_reader->ParseMore(pair[1], NULL);
  }

  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_reader);
  CHECK(zs::FileIsEqual("libevent/files/file_4G", "libevent/tmp_dir/file_4G"));
  std::string sha11;
  std::string path("libevent/files/file_4G");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(path, string(), &sha11));
  CHECK(sha11 == data_reader->sha1());
}


static zs::DefaultLogger logger("./Log");

int main(int /* argc */, char** /* argv */) {
  system("libevent/parse_file.sh");

  event_base = event_base_new();
  assert(event_base != NULL);

  int ret = bufferevent_pair_new(event_base, 0, pair);
  assert(ret == 0);

  writer = new FileTarWriter;
  head_reader = new FileHeadTarReader;
  data_reader = new FileDataTarReader();

  logger.error_to_stderr = false;
  logger.info_to_stdout = false;
  logger.warning_to_stdout = false;
  logger.Initialize();
  LogInitialize(&logger);

  UnitTest::RunAllTests();

  delete writer;
  delete head_reader;
  delete data_reader;

  return 0;
}
