#!/usr/bin/env python3

# Generates a program listing for the given source code.
#
#   Usage:
#       listing.py <options> <source file>
#
#   Example:
#       listing.py -O some-file.bf

import os
import re
import subprocess
import sys

# Grab source code:
for i in range(1, len(sys.argv)):
    if sys.argv[i] == '-e':
        source = [sys.argv[i + 1]]
        break
    if sys.argv[i][:2] == '-e':
        source = [sys.argv[i][2:]]
        break
else:
    source = open(sys.argv[-1], 'rt').read().split('\n')

# Generate object code in a.out, and grab tree code, using bfi:
proc = subprocess.Popen( ["./bfi"] + sys.argv[1:] + ["-t", "-c", "-o", "a.out"],
                         stdout=subprocess.PIPE )
data, _ = proc.communicate()
assert proc.poll() is not None
tree = []
for line in data.decode('utf-8').split('\n'):
    m = re.search('([A-Z]*) -?[0-9]+ origin=\[([0-9]+):([0-9]+),([0-9]+):([0-9]+)\] code=\\[([0-9a-f]+)h,([0-9a-f]+)h\\)', line)
    if m:
        tree.append((int(m.group(2), 10), int(m.group(3), 10),
                     int(m.group(4), 10), int(m.group(5), 10),
                     int(m.group(6), 16), int(m.group(7), 16),
                     str(m.group(1)), line))

# Generate assembly listing from object code using objdump:
proc = subprocess.Popen(["objdump", "-d", "a.out"], stdout=subprocess.PIPE)
data, _ = proc.communicate()
assert proc.poll() is not None
code = []
for line in data.decode('utf-8').split('\n'):
    m = re.search('^ *([0-9a-f]+):', line)
    if m:
        code.append((int(m.group(1), 16), line))

# Print combined listing:
stack = []
i = 0
for addr, line in code:
    while stack != [] and stack[-1][5] <= addr:
        if stack[-1][6] == 'LOOP':
            print('\t'*len(stack) + ']')
        stack.pop()
    while i < len(tree) and tree[i][4] <= addr:
        r1,c1,r2,c2,_,_,kind,desc = tree[i]
        stack.append(tree[i])
        i += 1
        if kind == 'LOOP':
            origin = '['
        elif r1 == r2:
            origin = source[r1 - 1][c1 - 1:c2]
        else:
            origin = '\n'.join([source[r1 - 1][c1-1:]] +
                [source[r] for r in range(r1, r2 - 1)] + [source[r2 - 1][:c2]])
        print('\t' + desc)
        print('\t'*len(stack) + origin)
    print('\t'*len(stack) + line)

# Remove temporary object file
os.remove('a.out')
