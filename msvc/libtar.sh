#!/bin/bash
set -x
cp ../third-part/libtar-windows/lib/libtar.h include/libtar.h
cp ../third-part/libtar-windows/Debug/libtar_d.lib lib/Win32/libtar_d.lib
cp ../third-part/libtar-windows/Debug/libtar_d.pdb lib/Win32/libtar_d.pdb
cp ../third-part/libtar-windows/Release/libtar.pdb lib/Win32/libtar.pdb
cp ../third-part/libtar-windows/Release/libtar.lib lib/Win32/libtar.lib
cp ../third-part/libtar-windows/x64/Debug/libtar_d.lib lib/x64/libtar_d.lib
cp ../third-part/libtar-windows/x64/Debug/libtar_d.pdb lib/x64/libtar_d.pdb
cp ../third-part/libtar-windows/x64/Release/libtar.pdb lib/x64/libtar.pdb
cp ../third-part/libtar-windows/x64/Release/libtar.lib lib/x64/libtar.lib
