#!/bin/bash
#########################################################################
# Author: Wang Wencan
# Created Time: Fri 08 Nov 2013 09:03:51 AM CST
# File Name: build_third_part.sh
# Description: 
#########################################################################
dir=`dirname $0`

set -e

SCRIPT_PATH=$dir # `readlink -m $dir`

cd $SCRIPT_PATH
source sh-libs.sh

usage_print() {
    echo "Options:"
    echo "    --init"
}
opts=$(getopt -l init,help h $@)
set -- $opts

is_init=""
while [ $# -gt 0 ]; do
    case $1 in 
        --init)
            is_init="y"
            shift
            break
            ;;
        --help|-h)
            usage_print
            exit 0
            ;;
        --)
            shift
            break
            ;;
    esac
done

THIRD_PART_PATH=$WORK_PATH/third-part

GLOG_PATH=$THIRD_PART_PATH/glog-0.3.3
PROTOBUF_PATH=$THIRD_PART_PATH/protobuf-2.5.0
ZEROMQ_PATH=$THIRD_PART_PATH/zeromq-4.0.4
UNITTEST_PATH=$THIRD_PART_PATH/UnitTest++-1.4
SQLITE3_PATH=$THIRD_PART_PATH/sqlite-3.7.7
GTEST_PATH=$THIRD_PART_PATH/gtest-1.7.0
SSL_PATH=$THIRD_PART_PATH/openssl-1.0.1i
LIBTAR_PATH=$THIRD_PART_PATH/libtar-1.2.16
LIBUUID_PATH=$THIRD_PART_PATH/libuuid-2.25.1-rc1

BUILD_PATH=$THIRD_PART_PATH/build

# Build glog
##if [ ! -f "$BUILD_PATH/lib/libglog.la" ]; then
##    cd $GLOG_PATH
##    ./configure --prefix=$BUILD_PATH \
##        --enable-shared=no && make && make install
##fi

# Build protobuf
if [ ! -f "$BUILD_PATH/lib/libprotobuf.la" ]; then
    cd $PROTOBUF_PATH
#    autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    ./build_android.sh
fi

# Build zeromq
if [ ! -f "$BUILD_PATH/lib/libzmq.la" ]; then
    cd $ZEROMQ_PATH
#    ./configure --prefix=$BUILD_PATH && make && make install
    ./build_android.sh
fi

# Build sqlite3
if [ ! -f "$BUILD_PATH/lib/libsqlite3.la" ]; then
    cd $SQLITE3_PATH
    #autoreconf --install --force && ./configure --prefix=$BUILD_PATH && make && make install
    ./build_android.sh
fi

# Build UnitTest++
if [ ! -f "$BUILD_PATH/lib/libUnitTest++.la" ]; then
    cd $UNITTEST_PATH
#    autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    ./build_android.sh
fi

if [ ! -f "$BUILD_PATH/lib/libcrypto.a" ]; then
    cd $SSL_PATH
    #./config --prefix=$BUILD_PATH && make && make install
    ./build_android.sh
fi
#Build libtar
if [ ! -f "$BUILD_PATH/lib/libtar.la" ]; then
    cd $LIBTAR_PATH
    ./build_android.sh
    cp lib/libtar.h $BUILD_PATH/include/
    cp tar.h $BUILD_PATH/include/
fi

if [ ! -f "$BUILD_PATH/lib/libuuid.la" ]; then
    cd $LIBUUID_PATH
    ./build_android.sh
fi
