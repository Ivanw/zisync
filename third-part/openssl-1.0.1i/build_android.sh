#!/bin/bash
cdir=`pwd`
make clean

./config -no-ssl2 -no-ssl3 -no-comp -no-hw -no-engine --openssldir=$BUILD_PATH
make depend
make all
make install CC=$TOOLCHAIN/gcc RANLIB=$TOOLCHAIN/ranlib
