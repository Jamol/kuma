#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os
import shutil
import subprocess

def mkdir_p(dir):
    if not os.path.exists(dir):
        os.makedirs(dir)

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_one_arch(workingPath, buildtype, arch):
    buildPath = workingPath + '/' + arch + '/' + buildtype
    if not os.path.exists(buildPath):
        os.makedirs(buildPath)
    os.chdir(buildPath)
    if arch == 'x86':
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux',
                   '-DCMAKE_C_FLAGS=-m32',
                   '-DCMAKE_CXX_FLAGS=-m32']
    else:
        cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_SYSTEM_NAME=Linux',
                   '-DCMAKE_C_FLAGS=-m64',
                   '-DCMAKE_CXX_FLAGS=-m64']
    run_and_check_error('cmake ../../../../.. ' + ' '.join(cmakeConfig))
    run_and_check_error('make')

def build_linux(workingPath):
    arch = 'x86_64'
    build_one_arch(workingPath, 'Debug', arch)
    
    build_one_arch(workingPath, 'Release', arch)


def linux_main(argv):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.makedirs(workingPath)
    os.chdir(workingPath)

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    run_and_check_error('python '+libkevPath+'/bld/linux/build_linux.py')

    build_linux(workingPath)

if __name__ == '__main__':
    linux_main(sys.argv)
