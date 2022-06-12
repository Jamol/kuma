#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os
import platform
sys.path.append(os.path.split(os.path.realpath(__file__))[0]+'/..')
from build_option import *

def mkdir_p(dir):
    if not os.path.exists(dir):
        os.makedirs(dir)

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_one_arch(workingPath, buildtype, arch, option):
    buildPath = workingPath + '/' + arch + '/' + buildtype
    if not os.path.exists(buildPath):
        os.makedirs(buildPath)
    os.chdir(buildPath)
    hostArch = platform.machine()
    if hostArch == 'aarch64':
        hostArch = 'arm64'

    print("********** Compiling one ARCH, target:%s, host:%s, buildtype:%s **********" % (arch, hostArch, buildtype))

    if hostArch != arch and hostArch != 'x86_64': # we should only cross platform compile on x86_64 linux
        print('wrong arch, target:%s, host arch:%s' % (arch, hostArch))
        return

    toolchains_path = workingPath + '/../../cmake/toolchains'
    if arch == 'arm' and hostArch == 'x86_64':
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux',
                   '-DCMAKE_TOOLCHAIN_FILE='+toolchains_path+'/arm-linux.cmake']
    elif arch == 'arm' and hostArch == arch:
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
            '-DCMAKE_SYSTEM_NAME=Linux']
    elif arch == 'arm64' and hostArch == 'x86_64':
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux',
                   '-DCMAKE_TOOLCHAIN_FILE='+toolchains_path+'/arm64-linux.cmake']
    elif arch == 'arm64' and hostArch == arch:
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux']
    elif arch == 'x86' and hostArch == arch:
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux',
                   '-DCMAKE_C_FLAGS=-m32',
                   '-DCMAKE_CXX_FLAGS=-m32']
    elif arch == 'x86' and hostArch != arch:
        print('wrong arch, target:%s, host arch:%s' % (arch, hostArch))
        return
    else:
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux',
                   '-DCMAKE_C_FLAGS=-m64',
                   '-DCMAKE_CXX_FLAGS=-m64']
    if option['rebuild'] and os.path.exists('Makefile'):
        run_and_check_error('make clean')
    if option['memcheck']:
        cmakeConfig.append('-DENABLE_ASAN=1')
    run_and_check_error('cmake ../../../../.. ' + ' '.join(cmakeConfig))
    run_and_check_error('make')

def build_linux(workingPath, option):
    for arch in option['archs']:
        if arch == 'x86_64':
            if option['debug']:
                build_one_arch(workingPath, 'Debug', arch, option)
            if option['release']:
                build_one_arch(workingPath, 'Release', arch, option)
        else:
            build_one_arch(workingPath, 'Release', arch, option)

def linux_main(option):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.makedirs(workingPath)
    os.chdir(workingPath)

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    run_and_check_error('python '+libkevPath+'/bld/linux/build_linux.py')

    build_linux(workingPath, option)

if __name__ == '__main__':
    option = get_option(sys.argv, 'linux')
    linux_main(option)
