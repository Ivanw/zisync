#!/bin/bash
find . -name "*.orig" -exec rm {} \;
find . -name "*.bak.cc" -exec rm {} \;
find . -name "*.cc.bak" -exec rm {} \;
