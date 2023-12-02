#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os
import shutil
import platform
sys.path.append(os.path.split(os.path.realpath(__file__))[0]+'/..')
from build_option import *

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_android(workingPath, option):
    os.chdir(workingPath)
    
    if platform.system() == 'Windows':
        stripCmd = 'set ANDROID_STRIP_SYMBOLS=ON' if option['strip'] else 'set ANDROID_STRIP_SYMBOLS=OFF'
        buildCmd = 'gradlew'
    else:
        stripCmd = 'export ANDROID_STRIP_SYMBOLS=ON' if option['strip'] else 'export ANDROID_STRIP_SYMBOLS=OFF'
        buildCmd = './gradlew'

    if option['rebuild']:
        run_and_check_error(buildCmd + ' clean')
    if option['debug']:
        #run_and_check_error(' && '.join([stripCmd, buildCmd + ' assembleDebug']))
        run_and_check_error(buildCmd + ' assembleDebug')
    if option['release']:
        #run_and_check_error(' && '.join([stripCmd, buildCmd + ' assembleRelease']))
        run_and_check_error(buildCmd + ' assembleRelease')

def android_main(option):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.mkdir(workingPath)
    os.chdir(workingPath)

    kuamPath = workingPath+'/../../../android/kuma'

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    run_and_check_error('python '+libkevPath+'/bld/android/build_android.py')

    if option['rebuild']:
        if os.path.exists(kuamPath+'/kuma/.cxx'):
            shutil.rmtree(kuamPath+'/kuma/.cxx')

    build_android(kuamPath, option)

if __name__ == '__main__':
    option = get_option(sys.argv, 'android')
    android_main(option)

