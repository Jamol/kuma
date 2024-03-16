#!/usr/bin/python
#coding:utf-8
from __future__ import print_function
import sys
import os

def get_sdkversion():
    output = os.popen('xcrun -sdk iphoneos --show-sdk-version')
    version = output.readline().strip('\n')
    output.close()
    return version

def get_xcode_root():
    output = os.popen('xcode-select -print-path')
    path = output.readline().strip('\n')
    output.close()
    return path

def run_and_check_error(command):
    if os.system(command) != 0:
        print('failed to execute: ', command)
        exit(-1)

def build_one_arch(workingPath, buildtype, arch, xcodePath):
    buildPath = workingPath + '/' + arch + '/' + buildtype
    if not os.path.exists(buildPath):
        os.makedirs(buildPath)
    os.chdir(buildPath)
    platform = 'iPhoneOS' if (arch == 'armv7' or arch == 'arm64') else 'iPhoneSimulator'
    os.environ['DEVROOT'] = '/'.join([xcodePath, 'Platforms', platform + '.platform', 'Developer'])
    os.environ['SDKROOT'] = os.environ['DEVROOT'] + '/SDKs/' + platform + '.sdk'
    os.environ['BUILD_TOOLS'] = xcodePath
    if arch == 'arm64_sim':
        arch = 'arm64'
    cmakeConfig = ['-DCMAKE_BUILD_TYPE='+buildtype,
                   '-DCMAKE_OSX_SYSROOT=$SDKROOT',
                   '-DCMAKE_SYSROOT=$SDKROOT',
                   '-DCMAKE_OSX_ARCHITECTURES='+arch,
                   '-DCMAKE_TARGET_SYSTEM=ios',
                   '-DXCODE_IOS_PLATFORM='+platform.lower()]
    run_and_check_error('cmake ../../../../.. ' + ' '.join(cmakeConfig))
    run_and_check_error('make')
    return buildPath+'/lib'

def build_ios(workingPath, outdir):
    xcodePath = get_xcode_root()

    os.system('rm -f '+outdir+'/Debug-iphonesimulator/libkuma.a*')
    os.system('rm -f '+outdir+'/Release-iphonesimulator/libkuma.a*')

    archs = ['arm64', 'arm64_sim', 'x86_64']
    for arch in archs:
        build_one_arch(workingPath, 'Debug', arch, xcodePath)
        build_one_arch(workingPath, 'Release', arch, xcodePath)
        if arch != 'arm64':
            os.system('mv '+outdir+'/Debug-iphonesimulator/libkuma.a '+
                            outdir+'/Debug-iphonesimulator/libkuma.a.'+arch)
            os.system('mv '+outdir+'/Release-iphonesimulator/libkuma.a '+
                            outdir+'/Release-iphonesimulator/libkuma.a.'+arch)
    
    os.system('lipo -create '+outdir+'/Debug-iphonesimulator/libkuma.a.arm64_sim '+
                              outdir+'/Debug-iphonesimulator/libkuma.a.x86_64 -output '+
                              outdir+'/Debug-iphonesimulator/libkuma.a')
    os.system('lipo -create '+outdir+'/Release-iphonesimulator/libkuma.a.arm64_sim '+
                              outdir+'/Release-iphonesimulator/libkuma.a.x86_64 -output '+
                              outdir+'/Release-iphonesimulator/libkuma.a')
    os.system('rm -f '+outdir+'/Debug-iphonesimulator/libkuma.a.*')
    os.system('rm -f '+outdir+'/Release-iphonesimulator/libkuma.a.*')


def ios_main(argv):
    workingPath = os.path.split(os.path.realpath(__file__))[0] + '/out'
    if not os.path.exists(workingPath):
        os.makedirs(workingPath)
    os.chdir(workingPath)

    #build libkev
    libkevPath = workingPath+'/../../../third_party/libkev'
    run_and_check_error('python '+libkevPath+'/bld/ios/build_ios.py')

    outdir = workingPath + '/../../../lib/ios'
    build_ios(workingPath, outdir)

if __name__ == '__main__':
    ios_main(sys.argv)
