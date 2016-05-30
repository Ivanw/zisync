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

#GLOG_PATH=$THIRD_PART_PATH/glog-0.3.3
PROTOBUF_PATH=$THIRD_PART_PATH/protobuf-2.5.0
ZEROMQ_PATH=$THIRD_PART_PATH/zeromq-4.0.4
UNITTEST_PATH=$THIRD_PART_PATH/UnitTest++-1.4
SQLITE3_PATH=$THIRD_PART_PATH/sqlcipher
GTEST_PATH=$THIRD_PART_PATH/gtest-1.7.0
SSL_PATH=$THIRD_PART_PATH/openssl-1.0.1i
LIBTAR_PATH=$THIRD_PART_PATH/libtar-1.2.16
LIBUUID_PATH=$THIRD_PART_PATH/libuuid-2.25.1-rc1
LIBEVENT2_PATH=$THIRD_PART_PATH/libevent-2.1.4-alpha

BUILD_PATH=$THIRD_PART_PATH/build

# Build glog
# if [ ! -f "$BUILD_PATH/lib/libglog.la" ]; then
#     cd $GLOG_PATH
#     ./configure --prefix=$BUILD_PATH \
#         --enable-shared=no && make && make install
# fi

# Build protobuf
echo "Build protobuf ...."
if [ ! -f "$BUILD_PATH/lib/libprotobuf.a" ]; then
    cd $PROTOBUF_PATH
    # autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    autoreconf --install --force && ./build.sh verbose
fi

# Build zeromq
echo "Build zeromq ...."
if [ ! -f "$BUILD_PATH/lib/libzmq.a" ]; then
    cd $ZEROMQ_PATH
    # ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    autoreconf -fi && ./build.sh verbose
fi

# Build UnitTest++
echo "Build UnitTest++ ...."
if [ ! -f "$BUILD_PATH/lib/libUnitTest++.a" ]; then
    cd $UNITTEST_PATH
    # autoreconf --install --force && ./configure --prefix=$BUILD_PATH && make && make install
    autoreconf --install --force && ./build.sh verbose
fi

echo "Build ssl ...."
if [ ! -f "$BUILD_PATH/lib/libcrypto.a" ]; then
    cd $SSL_PATH
    # ./config --prefix=$BUILD_PATH && make && make install
    # ./Configure darwin64-x86_64-cc --prefix=$BUILD_PATH && make && make installs
    ./build.sh verbose

fi

# Build sqlite3
echo "Build sqlite3 ...."
if [ ! -f "$BUILD_PATH/lib/libsqlcipher.a" ]; then
    cd $SQLITE3_PATH
    # autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    autoreconf --install --force && ./build-ios.sh verbose
fi

#Build libtar
echo "Build libtar ...."
if [ ! -f "$BUILD_PATH/lib/libtar.a" ]; then
    cd $LIBTAR_PATH
    autoreconf --install
    # ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    ./build.sh verbose
    cp lib/libtar.h $BUILD_PATH/include/
fi

echo "Build libuuid ...."
if [ ! -f "$BUILD_PATH/lib/libuuid.a" ]; then
    cd $LIBUUID_PATH
    # autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    autoreconf --install --force && ./build.sh verbose
fi

if [ ! -f "$BUILD_PATH/lib/libevent.la" -o -n "$libevent" -o -n "$is_init" -o ! -f "$BUILD_PATH/lib/libevent_pthreads.a" -o ! -f "$BUILD_PATH/lib/libevent_openssl.a" ]; then    
    cd $LIBEVENT2_PATH
    autoreconf -fi && ./build-ios.sh
fi

# Build gtest
#cd $GTEST_PATH
#./configure --prefix=$BUILD_PATH && make && make install


# Build curl
#if [ ! -f "$BUILD_PATH/lib/libcurl.a" ]; then
#    cd $CURL_PATH
#    autoreconf --install --force && ./configure --prefix=$BUILD_PATH \
    #        --enable-shared=no --enable-static=yes --without-ssl \
    #        --disable-ldap --without-librtmp --without-zlib --without-libidn \
    #        && make && make install
#fi

# Build pyzmq
#if [ -n "$is_init" ]; then
#    cd $PYZMQ_PATH
#
#    python setup.py configure --zmq=$BUILD_PATH
#    sudo python setup.py install
#    sudo python setup.py build_ext --inplace
#    python setup.py test
#fi
#
