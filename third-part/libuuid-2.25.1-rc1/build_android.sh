#!/bin/bash
export CC="$TOOLCHAIN/gcc --sysroot=$SYSROOT"
export CXX="$TOOLCHAIN/g++ --sysroot=$SYSROOT"

autoreconf -fvi

./configure \
 --host=arm-linux-androideabi \
 --prefix=$BUILD_PATH \
 --enable-shared=no

make clean
make 
make install
