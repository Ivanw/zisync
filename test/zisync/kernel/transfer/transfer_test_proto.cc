/****************************************************************************
 *       Filename:  proto.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/01/14 14:55:37
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Wang Wencan 
 *	    Email:  never.wencan@gmail.com
 *        Company:  
 ***************************************************************************/
#include <stdio.h>
#include <assert.h>
#include <iostream>

#include "zisync/kernel/transfer/transfer.pb.h"

int main(int argc, char *argv[])
{
  FILE *fp = NULL;
  assert(argc > 2);
  zs::TarGetFileList file_list;
  for (int i = 1; i < argc - 1; i++) {
    file_list.add_relative_paths(argv[i]);
  }

  std::string message;
  bool ret = file_list.SerializeToString(&message);
  assert(ret == true);

  fp = fopen(argv[argc - 1], "a");
  assert(fp != NULL);

  ssize_t nbytes = fwrite(message.c_str(), 1, message.length(), fp);
  ssize_t len = static_cast<ssize_t>(message.length());
  assert(len == nbytes);

  if (fp != NULL) {
    fclose(fp);
  }

  return 0;
}
