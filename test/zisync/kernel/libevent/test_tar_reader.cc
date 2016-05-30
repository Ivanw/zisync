/****************************************************************************
 *       Filename:  test_libevent_parse_file.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  02/03/15 11:02:47
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  PangHai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/
#include "zisync/kernel/platform/platform.h"

#include <UnitTest++/UnitTest++.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <memory>

#include "zisync/kernel/test_tool_func.h"
#include "zisync/kernel/libevent/libtar++.h"
#include "zisync/kernel/libevent/tar_reader.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/zslog.h"
#include "zisync_kernel.h"

using zs::FileHeadTarReader;
using zs::FileDataTarReader;
using zs::err_t;
using zs::tar_header;
using std::string;

struct bufferevent *bev = NULL;
struct event_base *event_base = NULL;
FileHeadTarReader *file_head = NULL;
FileDataTarReader *file_data = NULL;
tar_header file_header;
std::string longname= \
    "111111111111111111111111111111111111111111111111111111111" \
    "111111111111111111111111111111111111111111111111111111111" \
    "1111111111111111111111111111111111111111111";
void SetFileHeader(const char* real_path, const char* encode_path) {
  if (file_header.gnu_longname != NULL) {
    free(file_header.gnu_longname);
    file_header.gnu_longname = NULL;
  }

  memset(&file_header, 0, sizeof(struct tar_header));
  zs::OsFileStat stat;
  int ret = zs::OsStat(real_path, string(), &stat);
  assert(ret == 0);

  assert(stat.type == zs::OS_FILE_TYPE_REG);
  file_header.typeflag = REGTYPE;
  zs::th_set_mode(&file_header, stat.attr);
  zs::th_set_size(&file_header, stat.length);
  zs::th_set_path(&file_header, const_cast<char*>(encode_path));
}

err_t FileHeadParseMore(const char *data, int data_length) {
  struct evbuffer *input = bufferevent_get_input(bev);
  assert(input != NULL);

  evbuffer_unfreeze(input, 0);
  int ret = evbuffer_add(input, data, data_length);
  assert(ret == 0);
  evbuffer_freeze(input, 0);

  return file_head->ParseMore(bev);
}

err_t FileDataParseMore(const char *data, int data_length) {
  struct evbuffer *input = bufferevent_get_input(bev);
  assert(input != NULL);

  evbuffer_unfreeze(input, 0);
  int ret = evbuffer_add(input, data, data_length);
  assert(ret == 0);
  evbuffer_freeze(input, 0);

  return file_data->ParseMore(bev, NULL);
}


void ParseFileHead(const char *real_path, const char *encode_path) {
  SetFileHeader(real_path, encode_path);
  if (file_header.gnu_longname != NULL) {
    char temp_type = file_header.typeflag;
    int64_t file_size = zs::th_get_size(&file_header);

    file_header.typeflag = GNU_LONGNAME_TYPE;
    int longname_length = strlen(file_header.gnu_longname);
    zs::th_set_size(&file_header, longname_length);
    zs::th_finish(&file_header);

    char *ptr = reinterpret_cast<char*>(&file_header);
    CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, FileHeadParseMore(ptr, 256));
    CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, FileHeadParseMore(ptr + 256, 256));

    ptr = file_header.gnu_longname;
    int append_bytes = T_BLOCKSIZE - longname_length % T_BLOCKSIZE;
    do {
      int length = longname_length > 256 ? 256 : longname_length;
      CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, FileHeadParseMore(ptr, length));
      ptr += length;
      longname_length -= length;
    } while (longname_length);

    char buffer[T_BLOCKSIZE] = {0};
    CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, FileHeadParseMore(buffer, append_bytes));

    file_header.typeflag = temp_type;
    zs::th_set_size(&file_header, file_size);
  }
  zs::th_finish(&file_header);
  char *ptr = reinterpret_cast<char*>(&file_header);
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, FileHeadParseMore(ptr, 256));
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, FileHeadParseMore(ptr + 256, 256));
}

void ParseFileData(const char *real_path) {
  int64_t size = zs::th_get_size(&file_header);
  char buffer[2048] = {0};
  FILE *fp = zs::OsFopen(real_path, "rb");
  assert(fp != NULL);

  int length = 0;
  if (size == 0) {
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, FileDataParseMore(buffer, 0));
  } else {
    do {
      length = size > 2048 ? 2048 : size;
      size_t n = fread(buffer, length, 1, fp);
      assert(n == 1);

      size -= length;
      if (size != 0) {
        CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, FileDataParseMore(buffer, length));
      } else {
        CHECK_EQUAL(zs::ZISYNC_SUCCESS, FileDataParseMore(buffer, length));
      }
    } while (size);
  }

  int ret = fclose(fp);
  assert(ret == 0);
}

TEST(test_FileDataTarReaderEmptyfile) {
  // test empty file
  std::cout << "--------------empty file-------------" << std::endl;
  file_head->Reset();
  ParseFileHead("libevent/files/empty_file", "empty_file");

  std::string real_path("libevent/tmp_dir/empty_file");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
              file_data->Reset(*file_head, real_path, std::string()));
  ParseFileData("libevent/files/empty_file");
  CHECK_EQUAL(true, zs::FileIsEqual("libevent/files/empty_file",
                                    "libevent/tmp_dir/empty_file"));
  std::string sha11;
  std::string path("libevent/files/empty_file");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(path, string(), &sha11));
  CHECK(sha11 == file_data->sha1());
}

TEST(test_FileDataTarReaderLargeFile) {
  // test large file
  std::cout << "--------------large file-------------" << std::endl;
  file_head->Reset();
  ParseFileHead("libevent/files/file_4G", "file_4G");

  std::string real_path("libevent/tmp_dir/file_4G");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, file_data->Reset(*file_head, real_path, std::string()));
  ParseFileData("libevent/files/file_4G");
  CHECK(zs::FileIsEqual("libevent/files/file_4G", "libevent/tmp_dir/file_4G"));
  std::string sha11;
  std::string path("libevent/files/file_4G");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(path, string(), &sha11));
  CHECK(sha11 == file_data->sha1());
}

TEST(test_FileDataTarReaderNormalfile) {
  // test normal file
  std::cout << "--------------normal file-------------" << std::endl;
  file_head->Reset();
  ParseFileHead("libevent/files/file1", "file1");

  std::string real_path("libevent/tmp_dir/file1");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
              file_data->Reset(*file_head, real_path, std::string()));
  ParseFileData("libevent/files/file1");
  CHECK(zs::FileIsEqual("libevent/files/file1", "libevent/tmp_dir/file1"));
  std::string sha11;
  std::string path("libevent/files/file1");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(path, string(), &sha11));
  CHECK(sha11 == file_data->sha1());
}

TEST(test_FileDataTarReaderLongname) {
  // test longname file
  std::cout << "--------------longname file-------------" << std::endl;
  std::string longpath;
  longpath = "libevent/files/" + longname;
  file_head->Reset();
  ParseFileHead(longpath.c_str(), longname.c_str());

  // file_data->Reset(*file_head, string(), longpath.c_str());
  // ParseFileData(longpath.c_str());
  std::string tmp_path;
  tmp_path = "libevent/tmp_dir/" + longname;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, file_data->Reset(*file_head, tmp_path, std::string()));
  ParseFileData(longpath.c_str());
  CHECK(zs::FileIsEqual(longpath.c_str(), tmp_path.c_str()));
  std::string sha11;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zs::FileSha1(longpath, string(), &sha11));
  CHECK(sha11 == file_data->sha1());
}

static zs::DefaultLogger logger("./Log");

int main(int /* argc */, char** /* argv */) {
  system("libevent/parse_file.sh");
  event_base = event_base_new();
  assert(event_base != NULL);

  /* bufferevent */
  bev = bufferevent_socket_new(event_base, -1, 0);
  assert(bev != NULL);

  memset(&file_header, 0, sizeof(struct tar_header));

  file_head = new FileHeadTarReader();
  file_data = new FileDataTarReader();

  logger.error_to_stderr = false;
  logger.info_to_stdout = false;
  logger.warning_to_stdout = false;
  logger.Initialize();
  LogInitialize(&logger);

  UnitTest::RunAllTests();

  event_base_free(event_base);
  delete file_head;
  delete file_data;

  return 0;
}
