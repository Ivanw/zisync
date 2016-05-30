#!/bin/bash
export CC="$TOOLCHAIN/gcc --sysroot=$SYSROOT"
export CXX="$TOOLCHAIN/g++ --sysroot=$SYSROOT"
export CXXFLAGS="-lstdc++ -lsupc++"
./configure \
 --host=arm-linux-androideabi \
 --prefix=$BUILD_PATH
make clean
make 
make install
