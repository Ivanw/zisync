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
WORK_PATH=`readlink -m $SCRIPT_PATH/..`

cd $SCRIPT_PATH
#source sh-libs.sh

usage_print() {
    echo "Options:"
    echo "    --init"
    echo "    --protobuf"
    echo "    --zeromq"
    echo "    --unittest"
    echo "    --sqlite"
    echo "    --ssl"
    echo "    --libtar"
    echo "    --libuuid"
    echo "    --libevent"
}
opts=$(getopt -l init,help,protobuf,zeromq,unittest,sqlite,ssl,libtar,libuuid,libevent h $@)
set -- $opts

is_init=""
protobuf=""
zeromq=""
unittest=""
sqlite=""
ssl=""
libtar=""
libuuid=""
libevent=""

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
        --protobuf)
            protobuf="y"
            break
            ;;
        --zeromq)
            zeromq="y"
            break
            ;;
        --unittest)
            unittest="y"
            break
            ;;
        --sqlite)
            sqlite="y"
            break
            ;;
        --ssl)
            ssl="y"
            break
            ;;
        --libtar)
            libtar="y"
            break
            ;;
        --libuuid)
            libuuid="y"
            break
            ;;
        --libevent)
            libevent="y"
            break
            ;;
        --)
            shift
            break
            ;;
    esac
done

THIRD_PART_PATH=$WORK_PATH/third-part

PROTOBUF_PATH=$THIRD_PART_PATH/protobuf-2.5.0
ZEROMQ_PATH=$THIRD_PART_PATH/zeromq-4.0.4
UNITTEST_PATH=$THIRD_PART_PATH/UnitTest++-1.4
SQLITE3_PATH=$THIRD_PART_PATH/sqlcipher
SSL_PATH=$THIRD_PART_PATH/openssl-1.0.1i
LIBTAR_PATH=$THIRD_PART_PATH/libtar-1.2.16
LIBUUID_PATH=$THIRD_PART_PATH/libuuid-2.25.1-rc1
LIBEVENT_PATH=$THIRD_PART_PATH/libevent-2.0.21-stable
LIBEVENT2_PATH=$THIRD_PART_PATH/libevent-2.1.4-alpha

BUILD_PATH=$THIRD_PART_PATH/build

# Build glog
# if [ ! -f "$BUILD_PATH/lib/libglog.la" ]; then
#     cd $GLOG_PATH
#     ./configure --prefix=$BUILD_PATH \
#         --enable-shared=no && make && make install
# fi

# Build protobuf
if [ ! -f "$BUILD_PATH/lib/libprotobuf.la" -o -n "$protobuf" -o -n "$is_init" ]; then
    echo "---------Compile libprotobuf---------"
    cd $PROTOBUF_PATH
    autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
fi

# Build zeromq
if [ ! -f "$BUILD_PATH/lib/libzmq.la" -o -n "$zeromq" -o -n "$is_init" ]; then
    echo "---------Compile libzmq---------"
    cd $ZEROMQ_PATH
    autoreconf -fi && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
fi

# Build UnitTest++
if [ ! -f "$BUILD_PATH/lib/libUnitTest++.la" -o -n "$unittest" -o -n "$is_init" ]; then
    echo "---------Compile Unittest---------"
    cd $UNITTEST_PATH
    autoreconf --install --force && ./configure --prefix=$BUILD_PATH && make && make install
    # if [ ! -e "$BUILD_PATH/include/UnitTest++/" ]; then
    #     mkdir $BUILD_PATH/include/UnitTest++/
    # fi

    # if [ ! -e "$BUILD_PATH/include/UnitTest++/Posix" ]; then
    #     mkdir $BUILD_PATH/include/UnitTest++/Posix
    # fi

    # if [ ! -e "$BUILD_PATH/include/UnitTest++/Win32" ]; then
    #     mkdir $BUILD_PATH/include/UnitTest++/Win32
    # fi

    # make -k
    # cp libUnitTest++.a $BUILD_PATH/lib/libUnitTest++.a
    # cp src/*.h $BUILD_PATH/include/UnitTest++/
    # cp src/Posix/*.h $BUILD_PATH/include/UnitTest++/Posix
    # cp src/Win32/*.h $BUILD_PATH/include/UnitTest++/Win32
fi

if [ ! -f "$BUILD_PATH/lib/libcrypto.a" -o -n "$ssl" -o -n "$is_init" ]; then
    echo "---------Compile openssl---------"
    cd $SSL_PATH
    ./config --prefix=$BUILD_PATH && make && make install
fi

# Build sqlite3
if [ ! -f "$BUILD_PATH/lib/libsqlcipher.la" -o -n "$sqlite" -o -n "$is_init" ]; then
    echo "---------Compile sqlcipher---------"
    cd $SQLITE3_PATH
    ./build-linux.sh
fi

#Build libtar
if [ ! -f "$BUILD_PATH/lib/libtar.la" -o -n "$libtar" -o -n "$is_init" ]; then
    echo "---------Compile libtar---------"
    cd $LIBTAR_PATH
    autoreconf --install
    ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
    cp lib/libtar.h $BUILD_PATH/include/
fi

if [ ! -f "$BUILD_PATH/lib/libuuid.la" -o -n "$libuuid" -o -n "$is_init" ]; then
    echo "---------Compile libuuid---------"
    cd $LIBUUID_PATH
    autoreconf --install --force && ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
fi

#if [ ! -f "$BUILD_PATH/lib/libevent.la" -o -n "$libevent" -o -n "$is_init" ]; then
#    if [ ! -d $LIBEVENT_PATH ] ; then 
#        tar zxvf $THIRD_PART_PATH/libevent-2.0.21-stable.tar.gz -C $THIRD_PART_PATH 
#    fi
#    cd $LIBEVENT_PATH
#    ./configure --prefix=$BUILD_PATH --enable-shared=no && make && make install
#fi
    echo "---------Compile libevent before---------"
if [ ! -f "$BUILD_PATH/lib/libevent.la" -o -n "$libevent" -o -n "$is_init" -o ! -f "$BUILD_PATH/lib/libevent_pthreads.a" -o ! -f "$BUILD_PATH/lib/libevent_openssl.a" ]; then
    echo "---------Compile libevent---------"
    cd $LIBEVENT2_PATH
    export LDFLAGS="-L$BUILD_PATH/lib/"
    export LIBS="-lcrypto -lssl"
    export CPPFLAGS="-I/$BUILD_PATH/include"
    if test -z $PKG_CONFIG_PATH ; then
      export PKG_CONFIG_PATH="$BUILD_PATH/lib/pkgconfig"
    else
      export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$BUILD_PATH/lib/pkgconfig"
    fi
    set OPENDIR
    autoreconf --install && ./configure --enable-shared=no --prefix=$BUILD_PATH && make && make install
fi

# Build gtest
#cd $GTEST_PATH
#./configure --prefix=$BUILD_PATH && make && make install


# Build curl
#if [ ! -f "$BUILD_PATH/lib/libcurl.la" ]; then
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
