#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os
import shutil

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_x86_version(workingPath):
    platformPath = workingPath + '/x86'
    if not os.path.exists(platformPath):
        os.mkdir(platformPath)
    os.chdir(platformPath)
    run_and_check_error('cmake -G "Visual Studio 15 2017" ../../../..')

    #patch = ' /t:Rebuild'
    run_and_check_error('MSBuild.exe kuma.vcxproj /p:Configuration=Debug /p:Platform=x86')
    run_and_check_error('MSBuild.exe kuma.vcxproj /p:Configuration=Release /p:Platform=x86')
    
def build_x64_version(workingPath):
    platformPath = workingPath + '/x64'
    if not os.path.exists(platformPath):
        os.mkdir(platformPath)
    os.chdir(platformPath)
    run_and_check_error('cmake -G "Visual Studio 15 2017 Win64" ../../../..')

    #patch = ' /t:Rebuild'
    run_and_check_error('MSBuild.exe kuma.vcxproj /p:Configuration=Debug /p:Platform=x64')
    run_and_check_error('MSBuild.exe kuma.vcxproj /p:Configuration=Release /p:Platform=x64')

def build_windows(workingPath):
    build_x86_version(workingPath)
    build_x64_version(workingPath)

def windows_main(argv):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.mkdir(workingPath)
    os.chdir(workingPath)

    build_windows(workingPath)

if __name__ == '__main__':
    windows_main(sys.argv)
    
