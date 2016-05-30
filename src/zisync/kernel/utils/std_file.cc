/****************************************************************************
 *       Filename:  std_file.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/20/14 14:09:56
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Pang Hai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#include "zisync/kernel/utils/std_file.h"

namespace zs {

StdFile::~StdFile() {
  if (fp_ != NULL) {
    fclose(fp_);
  }
}

int StdFile::Open(const char* path, const char* mode) {
  fp_ = OsFopen(path, mode);
  if (fp_ == NULL) {
    return -1;
  }

  return 0;
}

int StdFile::Open(const std::string& path, const char* mode) {
  fp_ = OsFopen(path.c_str(), mode);
  if (fp_ == NULL) {
    return -1;
  }

  return 0;
}

int StdFile::Read(void* ptr, int size, int nmemb) {
  assert(fp_ != NULL);
  size_t nbytes = fread(
    ptr, static_cast<size_t>(size), static_cast<size_t>(nmemb), fp_);
  return static_cast<int>(nbytes);
}

size_t StdFile::Read(std::string* buffer) {
  assert(fp_ != NULL);
  void* ptr = &(*buffer->begin());
  size_t nmemb = buffer->size();
  size_t nbytes =  fread(ptr, 1, nmemb, fp_);
  return static_cast<int>(nbytes);
}

int StdFile::ReadWholeFile(std::string* buffer) {
  assert(fp_ != NULL);
  if (fp_ == NULL) {
    return -1;
  }

  int ret = fseek(fp_, 0, SEEK_END);
  assert(ret != -1);
  if (ret == -1) {
    return -1;
  }

  int offset = static_cast<int>(ftell(fp_));

  ret = fseek(fp_, 0, SEEK_SET);
  assert(ret != -1);

  buffer->resize(offset);
  ret = (int)fread(&*buffer->begin(), 1, offset, fp_);
  assert(ret == offset);

  return static_cast<int>(ret);
}

int StdFile::Write(const void* ptr, int size, int nmemb) {
  assert(fp_ != NULL);
  size_t nbytes =  fwrite(
    ptr, static_cast<size_t>(size), static_cast<size_t>(nmemb), fp_);
  return static_cast<int>(nbytes);
}


}  // namespace zs
