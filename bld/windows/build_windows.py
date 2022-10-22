#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os
import shutil
sys.path.append(os.path.split(os.path.realpath(__file__))[0]+'/..')
from build_option import *

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def get_generator(msvc, arch):
    opt = ' -A x64 ' if arch == 'x64' else ' -A Win32 '
    if msvc == 'vs2022' or msvc == '2022':
        return ' "Visual Studio 17 2022" ' + opt
    if msvc == 'vs2019' or msvc == '2019':
        return ' "Visual Studio 16 2019" ' + opt
    return ' "Visual Studio 15 2017" ' + opt

def build_one_arch(workingPath, arch, option):
    archPath = workingPath + '/' + arch
    if not os.path.exists(archPath):
        os.mkdir(archPath)
    os.chdir(archPath)

    run_and_check_error('cmake -G ' + get_generator(option['msvc'], arch) + ' ../../../..')
    patch = ' /t:Rebuild ' if option['rebuild'] else ''
    if option['debug']:
        run_and_check_error('MSBuild.exe kuma.vcxproj /p:Configuration=Debug /p:Platform=' + arch + patch)
    if option['release']:
        run_and_check_error('MSBuild.exe kuma.vcxproj /p:Configuration=Release /p:Platform=' + arch + patch)

def build_windows(workingPath, option):
    for arch in option['archs']:
        build_one_arch(workingPath, arch, option)

def windows_main(option):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.mkdir(workingPath)
    os.chdir(workingPath)

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    kevOption = '--msvc ' + option['msvc']
    run_and_check_error('python '+libkevPath+'/bld/windows/build_windows.py '+kevOption)

    build_windows(workingPath, option)

if __name__ == '__main__':
    option = get_option(sys.argv, 'windows')
    windows_main(option)
    