#!/bin/bash
currenPath="$PWD"
cd ../../
./waf
cd "$currenPath"
libPath="$currenPath/lib"
if [ ! -d "$libPath" ]
then
    mkdir "$libPath"
fi
cp -f ../../build/libnorm.so "$libPath/norm.so"
dotnet pack . -c Release-linux-x64