/**
 * @file fdbuf.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Create stream buffer base on fd.
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

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/transfer/fdbuf.h"

#include <stdio.h>
#include <algorithm>
#include <iterator>
#include <streambuf>
#include <cstddef>

namespace zs {

fdbuf::fdbuf(OsTcpSocket* socket)
    : socket_(NULL) {
      this->open(socket);
    }

fdbuf::~fdbuf() {
  this->close();
}

void fdbuf::open(OsTcpSocket* socket) {
  //  this->close();
  if (this->socket_ != NULL) {
    this->sync();
    //    ::close(socket_->fd());
    socket_->Shutdown("rw");
  }

  this->socket_ = socket;
  this->setg(this->inbuf_, this->inbuf_, this->inbuf_);
  this->setp(this->outbuf_, this->outbuf_ + bufsize - 1);
}

void fdbuf::close() {
  if (this->socket_ != NULL) {
    this->sync();
    //    ::close(this->fd_);
  }
}

int fdbuf::overflow(int c) {
  if (!traits_type::eq_int_type(c, traits_type::eof())) {
    *this->pptr() = traits_type::to_char_type(c);
    this->pbump(1);
  }
  return this->sync() == -1 ? traits_type::eof() : traits_type::not_eof(c);
}

int fdbuf::sync() {
  if (this->pbase() != this->pptr()) {
    std::streamsize size(this->pptr() - this->pbase());
    std::streamsize done(this->socket_->Send(
            this->outbuf_, static_cast<int>(size), MSG_NOSIGNAL));

    if (done < 0) { return -1; }

    // The code below assumes that it is success if the stream made
    // some progress. Depending on the needs it may be more
    // reasonable to consider it a success only if it managed to
    // write the entire buffer and, e.g., loop a couple of times
    // to try achieving this success.
    if (done > 0) {
      std::copy(this->pbase() + done, this->pptr(), this->pbase());
      this->setp(this->pbase(), this->epptr());
      this->pbump(static_cast<int>(size - done));
    }
  }
  return this->pptr() != this->epptr()? 0 : -1;
}

int fdbuf::underflow() {
  if (this->gptr() == this->egptr()) {
    // add () around std::min to prevent vc++ to expand min as macro,
    // since windows.h define MACRO for both min and max(yes, exactly
    // in the lowwer case).
    std::streamsize pback((std::min)(this->gptr() - this->eback(),
                                     std::ptrdiff_t(16 - sizeof(int))));
    std::copy(this->egptr() - pback, this->egptr(), this->eback());
    int done(this->socket_->Recv(this->eback() + pback, bufsize, 0));
    this->setg(this->eback(),
               this->eback() + pback,
               this->eback() + pback + (std::max)(0, done));
  }
  return this->gptr() == this->egptr()
      ? traits_type::eof() : traits_type::to_int_type(*this->gptr());
}

// int main()
// {
//   fdbuf        inbuf(0);
//   std::istream in(&inbuf);
//   fdbuf        outbuf(1);
//   std::ostream out(&outbuf);

//   std::copy(std::istreambuf_iterator<char>(in),
//             std::istreambuf_iterator<char>(),
//             std::ostreambuf_iterator<char>(out));
// }

}  // namespace zs
