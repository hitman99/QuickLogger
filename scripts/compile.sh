#!/bin/bash
   #-m64
g++  -Wl,--no-as-needed -c -O2 -s -std=c++11  -o QuickLogger.o QuickLogger.cpp
ar -rv libquicklogger.a QuickLogger.o