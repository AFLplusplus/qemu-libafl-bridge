#!/usr/bin/env python3

import subprocess, shutil, json, sys, os, re

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
sources = []
search = []
rpath = []

is_linking_qemu = False

shared_library_pattern = r"^[^-].*/lib(.*)\.so(\.[0-9].*)?(?!rsp)$"
rpath_pattern = r".*,-rpath,(.*)'?.*"
rpath_link_pattern = r"^.*,-rpath-link,(.*)$"

linker_interceptor_pattern = r"(\": \")(.*linker_interceptor.py)( )"
linker_interceptorpp_pattern = r"(\": \")(.*linker_interceptor\+\+.py)( )"

def fix_compile_commands():
    with open("compile_commands.json", 'r') as f:
        compile_commands = f.read()
    
    res = re.sub(linker_interceptor_pattern, rf"\g<1>{CC}\g<3>", compile_commands)
    res = re.sub(linker_interceptorpp_pattern, rf"\g<1>{CXX}\g<3>", res)

    with open("compile_commands.json", 'w') as f:
        f.write(res)

    if not os.path.isfile("../compile_commands.json"):
        os.symlink("build/compile_commands.json", "../compile_commands.json")

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
        elif (res := re.match(shared_library_pattern, args[i])) is not None:
            name = res.group(1)
            shareds.append(name)
            continue
        elif (res := re.match(rpath_link_pattern, args[i])) is not None:
            rpath_link_path = res.group(1)
            search.append(rpath_link_path)
            continue
        elif (res := re.match(rpath_pattern, args[i])) is not None:
            rpath_path = res.group(1)
            rpath.append(rpath_path)
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
    with open("compile_commands.json", 'r') as f:
        compile_commands = json.load(f)
        for entry in compile_commands:
            sources.append(entry["file"])

    fix_compile_commands()

    with open(OUT, 'w') as f:
        json.dump({
            'cmd': out_args,
            'libs': shareds,
            'search': search,
            'rpath': rpath,
            'sources': sources,
        }, f, indent=2)

r = subprocess.run([cc] + args)
sys.exit(r.returncode)
