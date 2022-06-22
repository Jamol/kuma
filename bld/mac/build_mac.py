#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os

def get_sdkversion():
    output = os.popen('xcrun -sdk macosx --show-sdk-version')
    version = output.readline().strip('\n')
    output.close()
    return version

def get_xcode_root():
    output = os.popen('xcode-select -print-path')
    path = output.readline().strip('\n')
    output.close()
    return path

def mkdir_p(dir):
    if not os.path.exists(dir):
        os.makedirs(dir)

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_one_arch(workingPath, buildtype, arch, xcodePath):
    buildPath = workingPath + '/' + arch + '/' + buildtype
    if not os.path.exists(buildPath):
        os.makedirs(buildPath)
    os.chdir(buildPath)
    os.environ['DEVROOT'] = xcodePath + '/Platforms/MacOSX.platform/Developer'
    os.environ['SDKROOT'] = os.environ['DEVROOT'] + '/SDKs/MacOSX' + '.sdk'
    os.environ['BUILD_TOOLS'] = xcodePath
    buildArchs = 'x86_64;arm64' if (arch == 'all') else arch
    cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_OSX_SYSROOT=$SDKROOT',
                   '-DCMAKE_SYSROOT=$SDKROOT',
                   '-DCMAKE_OSX_ARCHITECTURES=\"' + buildArchs +'\"',
                   '-DCMAKE_TARGET_SYSTEM=mac']
    run_and_check_error('cmake ../../../../.. ' + ' '.join(cmakeConfig))
    run_and_check_error('make')

def build_mac(workingPath):
    xcodePath = get_xcode_root()
    arch = 'all'
    build_one_arch(workingPath, 'Debug', arch, xcodePath)
    
    build_one_arch(workingPath, 'Release', arch, xcodePath)


def mac_main(argv):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.makedirs(workingPath)
    os.chdir(workingPath)

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    run_and_check_error('python '+libkevPath+'/bld/mac/build_mac.py')
    
    build_mac(workingPath)

if __name__ == '__main__':
    mac_main(sys.argv)
