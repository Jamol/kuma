#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_one_arch(arch, workingPath):
    buildPath = workingPath + '/' + arch
    if not os.path.exists(buildPath):
        os.makedirs(buildPath)
    os.chdir(buildPath)
    
    version = '21' if (arch == 'arm64-v8a' or arch == 'x86_64') else '19'
    cmakeConfig = ['-DCMAKE_SYSTEM_NAME=Android',
                   '-DCMAKE_SYSTEM_VERSION='+version,
                   '-DCMAKE_ANDROID_ARCH_ABI='+arch,
                   '-DCMAKE_ANDROID_STL_TYPE=c++_static',
                   '-DCMAKE_ANDROID_NDK=${ANDROID_NDK_HOME}',
                   '-DCMAKE_BUILD_TYPE=Release', #Debug support?
                   '-DCMAKE_CXX_STANDARD=14',
                   '-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang']
    run_and_check_error('cmake ../../../.. ' + ' '.join(cmakeConfig))
    run_and_check_error('make')

def android_main(argv):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.mkdir(workingPath)
    os.chdir(workingPath)

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    run_and_check_error('python '+libkevPath+'/bld/android/build_android.py')

    archs = ["armeabi-v7a", "arm64-v8a", "x86", "x86_64"]
    for arch in archs:
        build_one_arch(arch, workingPath)

if __name__ == '__main__':
    android_main(sys.argv)

