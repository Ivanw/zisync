/****************************************************************************
 *       Filename:  test_create_file.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/30/14 18:08:34
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Pang Hai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#include "zisync/kernel/platform/platform.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "zisync/kernel/database/icore.h"

int main(int argc, char *argv[])
{
  assert(argc == 3);
  FILE* fp = zs::OsFopen(argv[1], "w");
  assert(fp != NULL);
  std::string str;

  int length = strlen(argv[2]);
  int64_t size = 0;
  if (argv[2][length - 1] == 'B') {
    size = atoi(argv[2]);
  } else if (argv[2][length - 1] == 'K') {
    size = atoi(argv[2]) * 1024;
  } else if (argv[2][length - 1] == 'M') {
    size = atoi(argv[2]) * 1024 * 1024;
  } else if (argv[2][length - 1] == 'G') {
    size = int64_t(atoi(argv[2])) * 1024 * 1024 * 1024;
  }

  while (size > 0) {
    zs::StringFormat(&str, "%d", rand());
    fwrite(str.c_str(), 1, size > 1024 ? 1024 : size, fp);
    size = (size > 1024 ? size - 1024 : 0);
  }

  fclose(fp);

  return 0;
}
