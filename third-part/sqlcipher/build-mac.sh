#!/bin/bash

BUILD_PATH="`pwd`/../build"
export CPPFLAGS="-I$BUILD_PATH/include"
export CFLAGS="-DSQLITE_HAS_CODEC"

PREFIX=$BUILD_PATH 
if [ $# -gt 0 ] ; then 
  echo "using prefix $1"
  PREFIX=$1
else 
  echo "using default prefix $PREFIX"
fi

#rm old version sqlite3.h, which affects build new version
if [ -f $BUILD_PATH/include/sqlite3.h  ] ; then
  rm $BUILD_PATH/include/sqlite3.h
fi

autoreconf -fi && ./configure LDFLAGS="-L$BUILD_PATH/lib/libcrypto.a" --enable-tempstore=yes  --enable-shared=no  --prefix=$PREFIX --disable-tcl && make && make install
