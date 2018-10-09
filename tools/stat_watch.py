#!/usr/bin/env python3
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
# Copyright (C) 2017 Netronome Systems, Inc.

import os
import subprocess
import signal
import sys
import time
import re

ONLY_ETHTOOL = False
COLORS=[]
COLOR_DIM = False
IFC=""
INCL=[]
EXCL=[]

def intr(signal, frame):
        print(' Exiting...')
        sys.exit(0)
signal.signal(signal.SIGINT, intr)


def usage():
        print("Usage: %s [-E] [-c] [-crx] [-ctx] [-f pattern] [-x pattern] IFC" % \
                sys.argv[0])
        print("\tSources")
        print("\t\t-E show only ethtool -S stats (exclude ifconfig statistics)")
        print("\tColors")
        print("\t\t-crx color RX stats")
        print("\t\t-ctx color TX stats")
        print("\t\t-cerr color error stats")
        print("\t\t-cdisc color discard stats")
        print("\t\t-cd dim idle stats")
        print("\t\t-c enable all colors")
        print("\tFilters")
        print("\t\t-f <pattern> include only stats that match the pattern")
        print("\t\t-x <pattern> exclude stats which match the pattern")
        print("\t\texclude takes precedence, both can be repeated")
        print("")
        print("\tOrder of parameters doesn't matter.")
        sys.exit(1)


skip=0
for i in range(1, len(sys.argv)):
        if skip:
                skip -= 1
                continue

        if sys.argv[i] == '-E':
                ONLY_ETHTOOL = True
        elif sys.argv[i] == '-c':
                COLOR_DIM = True
                COLORS.append(('discard', "33m"))
                COLORS.append(('drop', "33m"))
                COLORS.append(('error', "31m"))
                COLORS.append(('illegal', "31m"))
                COLORS.append(('fault', "31m"))
                COLORS.append(('rx', "32m"))
                COLORS.append(('tx', "36m"))
        elif sys.argv[i] == '-ctx':
                COLORS.append(('tx', "36m"))
        elif sys.argv[i] == '-crx':
                COLORS.append(('rx', "32m"))
        elif sys.argv[i] == '-cerr':
                COLORS.append(('error', "31m"))
                COLORS.append(('illegal', "31m"))
                COLORS.append(('fault', "31m"))
        elif sys.argv[i] == '-cdisc':
                COLORS.append(('discard', "33m"))
                COLORS.append(('drop', "33m"))
        elif sys.argv[i] == '-cd':
                COLOR_DIM = True
        elif sys.argv[i] == '-f':
                INCL.append(re.compile(sys.argv[i + 1]))
                skip += 1
        elif sys.argv[i] == '-x':
                EXCL.append(re.compile(sys.argv[i + 1]))
                skip += 1
        elif IFC == '':
                IFC = sys.argv[i]
        else:
                print('What is %s?' % sys.argv[i])
                usage()

if IFC == '':
        usage()

stats = {}
session = {}

def key_ok(key):
        if len(INCL) == 0 and len(EXCL) == 0:
                return True
        res = len(INCL) == 0
        for p in INCL:
                res = p.search(key) or res
        for p in EXCL:
                res = not p.search(key) and res

        return res

sysfs_stats_path = os.path.join('/sys/class/net/', IFC, 'statistics')

def get_sysfs_stats():
        out = ''

        for filename in reversed(os.listdir(sysfs_stats_path)):
                filepath = os.path.join(sysfs_stats_path, filename)
                data = ''
                with open(filepath, 'r') as filedata:
                        data += filedata.read()
                out += '%s:%s' % (filename, data)

        return out

clock = time.time()
while True:
        columns = 80

        try:
                out = ''

                if not ONLY_ETHTOOL:
                       out += get_sysfs_stats()

                stdout = subprocess.check_output(['ethtool', '-S', IFC])
                out += stdout.decode("utf-8")

                _, columns = os.popen('stty size', 'r').read().split()
                columns = int(columns)
        except:
                os.system("clear")
                print("Reading stats from device \033[1m%s\033[0m failed" % IFC)
                stats = {}
                session = {}
                time.sleep(0.5)
                continue

        w = [26, 13, 19, 19]
        for i in range(3):
                w[i] += int((columns - 80) / 4)
        w[3] += int((columns + 3 - 80) / 4)

        pr = "\033[4;1mSTAT {:>{a}} {:>{c}} {:>{d}}\033[0m\n".\
             format("RATE", "SESSION", "TOTAL",
                    a=(w[0] + w[1] - 4), c=w[2], d=w[3])
        for l in out.split('\n'):
                s = l.split(':')
                if len(s) != 2 or s[1] == '':
                        continue
                key = s[0].strip()
                value = int(s[1].strip())

                if not key_ok(key):
                        continue

                if not key in stats:
                        stats[key] = value
                        session[key] = value
                        continue

                if value != 0:
                        color = "37m"
                        for (needle, c) in COLORS:
                                if key.find(needle) != -1:
                                        color = c
                                        break

                        if not value - stats[key] and COLOR_DIM:
                                color = '2;' + color
                        color = '\033[' + color

                        key_w = max(w[0] + w[1] - len(key), 1)

                        pr += '{:}{:} {:>{b},} {:>{c},} {:>{d},}\033[31;0m\n'.\
                              format(color, key, value - stats[key],
                                     value - session[key], value,
                                     b=key_w, c=w[2], d=w[3])

                stats[key] = value

        os.system("clear")
        sys.stdout.write(pr)

        now = time.time()
        sleep_time = 1.0 - (now - clock)
        if sleep_time < 0:
                sys.stderr.write("Warning: refresh time over 1 sec")
                clock = time.time()
        else:
                time.sleep(sleep_time)
                clock += 1
