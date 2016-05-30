#!/bin/sh

#  Automatic build script for libssl and libcrypto 
#  for iPhoneOS and iPhoneSimulator
#
#  Created by Felix Schulze on 16.12.10.
#  Copyright 2010 Felix Schulze. All rights reserved.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
###########################################################################
#  Change values here													  #
#				
VERSION="1.0.1i"													      #
SDKVERSION="7.1"														  #
#																		  #
###########################################################################
#																		  #
# Don't change anything under this line!								  #
#																		  #
###########################################################################


CURRENTPATH=`pwd`
ARCHS="armv7 armv7s arm64"
DEVELOPER=`xcode-select -print-path`
VERSION="1.0.1i"													      #
sdkdir=$DEVELOPER/Platforms/iPhoneOS.platform/Developer/SDKs
SDKVERSION="8.1"														  #
if [ ! -d $sdkdir/iPhoneOS8.1.sdk   ] ; then
    SDKVERSION="7.1";
fi

while [[ $# -ge 1  ]]
do
    key="$1"
    shift
    case $key in
        verbose)
            verbose="yes"
            ;;
        8.1)
            SDKVERSION="8.1"
            ;;
        7.1)
            SDKVERSION="7.1"
            ;;
        *)
            # unknown option
            ;;
    esac
done
echo will link: $sdkdir/iPhoneOS$SDKVERSION.sdk

if [ ! -d "$DEVELOPER" ]; then
  echo "xcode path is not set correctly $DEVELOPER does not exist (most likely because of xcode > 4.3)"
  echo "run"
  echo "sudo xcode-select -switch <xcode path>"
  echo "for default installation:"
  echo "sudo xcode-select -switch /Applications/Xcode.app/Contents/Developer"
  exit 1
fi

case $DEVELOPER in  
     *\ * )
           echo "Your Xcode path contains whitespaces, which is not supported."
           exit 1
          ;;
esac

case $CURRENTPATH in  
     *\ * )
           echo "Your path contains whitespaces, which is not supported by 'make install'."
           exit 1
          ;;
esac
uniqueName=`pwd`
uniqueName=`basename $uniqueName`
theBinDir="${CURRENTPATH}/../build/$uniqueName/bin"
theLibDir="${CURRENTPATH}/../build/lib"
mkdir -p $theBinDir
mkdir -p $theLibDir 

for ARCH in ${ARCHS}
do
	if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]];
	then
		PLATFORM="iPhoneSimulator"
	else
	#	sed -ie "s!static volatile sig_atomic_t intr_signal;!static volatile intr_signal;!" "crypto/ui/ui_openssl.c"
		PLATFORM="iPhoneOS"
	fi

    DEVROOT="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
    SDKROOT="${DEVROOT}/SDKs/${PLATFORM}${SDKVERSION}.sdk"
    #only for openssl
#	export CROSS_TOP="${DEVELOPER}/Platforms/${PLATFORM}.platform/Developer"
#	export CROSS_SDK="${PLATFORM}${SDKVERSION}.sdk"
#	export BUILD_TOOLS="${DEVELOPER}"

    echo "Building `pwd` for ${PLATFORM} ${SDKVERSION} ${ARCH}"
    echo "Please stand by..."

    export CC="${BUILD_TOOLS}/usr/bin/gcc"
    export CXX="${BUILD_TOOLS}/usr/bin/g++"
    export CPPFLAGS="-arch $ARCH"
    export LDFLAGS="-arch $ARCH"

    PREFIX=$theBinDir/${PLATFORM}${SDKVERSION}-${ARCH}.sdk
    if [[ "${ARCH}" == "i386" || "${ARCH}" == "x86_64" ]]
    then
        ARCHFLAG=" --host=$ARCH-apple-darwin "
    elif [[ "${ARCH}" == "armv7" || "${ARCH}" == "armv7s"  ]]
    then
        ARCHFLAG=" --host=$ARCH-apple-darwin "
    else
        ARCHFLAG=" --host=aarch64-apple-darwin "
    fi

    if [[ "${ARCH}" != "i386" && "${ARCH}" != "x86_64" ]]
    then
        export CPP="$CC -E"
        export LD="/usr/bin/ld"
        #export PKG_CONFIG_PATH="$SDKROOT/usr/lib/pkgconfig":"$PREFIX/lib/pkgconfig"
        export CPPFLAGS="-arch $ARCH -isysroot $SDKROOT"
        export CFLAGS="$CPPFLAGS -miphoneos-version-min=7.0 -std=c99 -pipe -no-cpp-precomp"
        export CXXFLAGS="$CPPFLAGS -miphoneos-version-min=7.0 -I$SDKROOT/usr/include/c++/4.2.1/$ARCH -pipe -no-cpp-precomp"
        export LDFLAGS="-arch $ARCH -isysroot $SDKROOT"
    fi

    mkdir -p "$theBinDir/${PLATFORM}${SDKVERSION}-${ARCH}.sdk"
    LOG="$theBinDir/${PLATFORM}${SDKVERSION}-${ARCH}.sdk/build-openssl-${VERSION}.log"
#    export CPPFLAGS="-isysroot $SDKROOT"
#    export CFLAGS="$CPPFLAGS -std=c99 -pipe -no-cpp-precomp"
#    export CXXFLAGS="$CPPFLAGS -I$SDKROOT/usr/include/c++/4.2.1/$ARCH -pipe -no-cpp-precomp"
#    export LDFLAGS="-isysroot $SDKROOT"

	set +e
    ./configure $ARCHFLAG --prefix=$PREFIX --enable-shared=no > "${LOG}" 2>&1
    
    if [ $? != 0 ];
    then 
    	echo "Problem while configure - Please check ${LOG}"
    	exit 1
    fi
	# add -isysroot to CC=
	#sed -ie "s!^CFLAG=!CFLAG=-isysroot ${CROSS_TOP}/SDKs/${CROSS_SDK} -miphoneos-version-min=7.0 !" "Makefile"

	make clean >> "${LOG}" 2>&1
	if [ -n $verbose ];
	then
		make
	else
		make >> "${LOG}" 2>&1
	fi
	
	if [ $? != 0 ];
    then 
    	echo "Problem while make - Please check ${LOG}"
    	exit 1
    fi
    
    set -e
	make install >> "${LOG}" 2>&1
done

echo "Build library..."

theLibsToAdd=""
for  onelib in `find $theBinDir -iname *.a`
do
    if [ ! -h $onelib  ]
    then
        theLibsToAdd="$theLibsToAdd $onelib"
    fi
done

lipo -create $theLibsToAdd -output $theLibDir/libzmq.a
#mkdir -p ${CURRENTPATH}/../build/include

incDirToCp=""
for incDirToCp in `find $theBinDir -iname include`
do
    break
done
cp -R $incDirToCp/  ${CURRENTPATH}/../build/include
echo "Building done."
echo "Cleaning up..."
# rm -rf ${CURRENTPATH}/src/openssl-${VERSION}
echo "Done."
