#!/usr/bin/env python3

import subprocess, shutil, json, sys, os

FILTER = ['-shared']

CC = os.getenv('__LIBAFL_QEMU_BUILD_CC') or 'cc'
CXX = os.getenv('__LIBAFL_QEMU_BUILD_CXX') or 'c++'
OUT = os.getenv('__LIBAFL_QEMU_BUILD_OUT') or 'linkinfo.json'

args = sys.argv[1:]

if '++' in sys.argv[0]:
    cc = CXX
else:
    cc = CC

out_args = []
shareds = []
search = []
is_linking_qemu = False

def process_args(args):
    global out_args, shareds, search, is_linking_qemu
    prev_o = False

    for i in range(len(args)):
        if prev_o:
            prev_o = False
            if args[i].endswith('.so') and args[i].startswith('libqemu'):
                is_linking_qemu = True
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

if is_linking_qemu:
    with open(OUT, 'w') as f:
        json.dump({
          'cmd': out_args,
          'libs': shareds,
          'search': search,
        }, f, indent=2)

r = subprocess.run([cc] + args)
sys.exit(r.returncode)
