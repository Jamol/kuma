#!/usr/bin/python
#coding:utf-8

from __future__ import print_function
import sys
from optparse import OptionParser

def get_default_options(systemName):
    opt = {
        'rebuild':              False,
        'debug':                True,
        'release':              True,
        'msvc':                 'vs2017',
        'memcheck':             False,
        'strip':                False, # for andorid
    }
    if systemName == 'windows':
        opt['archs'] = ['x86', 'x64']
    elif systemName == 'mac':
        opt['archs'] = ['x86_64', 'arm64']
    elif systemName == 'ios':
        opt['archs'] = ['x86_64', 'arm64', 'armv7']
    elif systemName == 'linux':
        #opt['archs'] = ['x86_64', 'arm64', 'arm']
        opt['archs'] = ['x86_64']
    elif systemName == 'android':
        opt['archs'] = ['armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64']
    return opt

def get_option(argv, systemName):
    parser = OptionParser(description='libkev build option')
    parser.add_option('--rebuild', dest='rebuild', action='store_true', default=False,
                      help='clean up before build')
    parser.add_option('--debug', dest='debug', action='store_true', default=False,
                      help='build debug version only')
    parser.add_option('--release', dest='release', action='store_true', default=False,
                      help='build release version only')
    parser.add_option('--archs', dest='archs', action='store', default='',
                      help='specify target architecture. default: all. use "," for multiple targets.'
                      'windows candidates: x86,x64 '
                      'android candidates: armeabi-v7a,arm64-v8a,x86,x86_64 '
                      'ios candidates: armv7,arm64,x86_64 '
                      'mac candidates: x86_64,arm64')
    parser.add_option('--msvc', dest='msvc', action='store', default='',
                      help='specify the version of microsoft visual studio')
    parser.add_option('--memcheck', dest='memcheck', action='store_true', default=False,
                      help='enable memory check')
    parser.add_option('--strip', dest='strip', action='store_true', default=False,
                      help='strip symbols for andoird build')

    (options, args) = parser.parse_args(args=argv[1:])

    userOption = get_default_options(systemName)
    userOption['rebuild'] = options.rebuild
    if options.debug:
        userOption['debug'] = True
        userOption['release'] = False
    elif options.release:
        userOption['debug'] = False
        userOption['release'] = True
    if options.archs != '':
        userOption['archs'] = []
        for arch in options.archs.split(','):
            userOption['archs'].append(arch)
    if options.msvc != '':
        userOption['msvc'] = options.msvc
    userOption['memcheck'] = options.memcheck
    userOption['strip'] = options.strip
    
    print('User Options: ', userOption)
    return userOption


if __name__ == '__main__':
    options = get_option(sys.argv, 'windows')
