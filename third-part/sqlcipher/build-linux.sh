#!/bin/bash

#BUILD_PATH="`pwd`/../"
BUILD_PATH=`readlink -m ../`
export CPPFLAGS="-I$BUILD_PATH/include"
export CFLAGS="-DSQLITE_HAS_CODEC"
export LDFLAGS="-L$BUILD_PATH/lib"
export LIBS="-lcrypto -ldl"

PREFIX=$BUILD_PATH/build
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

autoreconf -fi && ./configure --enable-tempstore=yes  --enable-shared=no  --prefix=$PREFIX --disable-tcl && make && make install
