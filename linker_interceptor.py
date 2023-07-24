#!/usr/bin/env python3

import subprocess, json, sys, os

FILTER = ['-shared']

CXX = os.getenv('__LIBAFL_QEMU_BUILD_CXX') or 'c++'
OUT = os.getenv('__LIBAFL_QEMU_BUILD_OUT') or 'linkinfo.json'

args = sys.argv[1:]

out_args = []
shareds = []
prev_o = False
for i in range(len(args)):
    if prev_o:
        prev_o = False
        continue
    elif args[i] in FILTER:
        continue
    elif args[i].endswith('.so') and not args[i].startswith('-'):
        shareds.append(args[i])
        continue
    elif args[i] == '-o':
        prev_o = True
        continue
    elif args[i].startswith('-L') or args[i].startswith('-l'):
        shareds.append(args[i])
    out_args.append(args[i])

with open(OUT, 'w') as f:
    json.dump({'cmd': out_args, 'so': shareds}, f, indent=2)

subprocess.run([CXX] + args)
