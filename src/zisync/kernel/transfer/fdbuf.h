/**
 * @file fdbuf.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief fdbuf.
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

#ifndef ZISYNC_KERNEL_TRANSFER_FDBUF_H_
#define ZISYNC_KERNEL_TRANSFER_FDBUF_H_

#include <streambuf>
#include "zisync/kernel/platform/platform.h"
namespace zs {

class fdbuf
    : public std::streambuf {
 private:
  enum { bufsize = 4096 };
  char outbuf_[bufsize];
  char inbuf_[bufsize + 16 - sizeof(int)];
  OsTcpSocket* socket_;
 public:
  typedef std::streambuf::traits_type traits_type;

  explicit fdbuf(OsTcpSocket* socket);
  ~fdbuf();
  void open(OsTcpSocket* socket);
  void close();

 protected:
  int overflow(int c);
  int underflow();
  int sync();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_FDBUF_H_
