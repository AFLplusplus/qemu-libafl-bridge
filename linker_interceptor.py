#!/usr/bin/env python3

import subprocess, shutil, json, sys, os

FILTER = ['-shared']

CXX = os.getenv('__LIBAFL_QEMU_BUILD_CXX') or 'c++'
OUT = os.getenv('__LIBAFL_QEMU_BUILD_OUT') or 'linkinfo.json'

args = sys.argv[1:]

out_args = []
shareds = []
search = []

def process_args(args):
    global out_args, shareds, search
    prev_o = False

    for i in range(len(args)):
        if prev_o:
            prev_o = False
            continue
        elif args[i] in FILTER:
            continue
        elif args[i].endswith('.so') and not args[i].startswith('-'):
            name = os.path.basename(args[i])[3:-3] # remove prefix and suffix
            shareds.append(name)
            continue
        elif args[i] == '-o':
            prev_o = True
            continue
        elif args[i].startswith('-l'):
            shareds.append(args[i][2:])
            continue
        elif args[i].endswith('.rsp'):
            fname = args[i][1:] # remove initial @
            with open(fname) as f:
                rsp = f.read().split()
            process_args(rsp)
            continue
        elif args[i].startswith('-L'):
            search.append(args[i][2:])
        out_args.append(args[i])

process_args(args)

with open(OUT, 'w') as f:
    json.dump({
      'cmd': out_args,
      'libs': shareds,
      'search': search,
    }, f, indent=2)

subprocess.run([CXX] + args)
