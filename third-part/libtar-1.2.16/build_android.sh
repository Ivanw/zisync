#!/bin/bash
export CC="$TOOLCHAIN/gcc --sysroot=$SYSROOT"
#export CXX="$TOOLCHAIN/g++ --sysroot=$SYSROOT"
#export CXXFLAGS="-lstdc++ -lsupc++"

autoreconf -fvi

./configure \
 --host=arm-linux-androideabi \
 --enable-shared=no \
 --prefix=$BUILD_PATH
make clean
make 
make install
cp tar.h $BUILD_PATH/include
