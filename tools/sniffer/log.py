#!/usr/bin/env python3
"""Tee sniffer serial output to captures/<name>.log.
Usage: log.py <port> <seconds> <name> [cmd ...]   (cmds are sent after boot, e.g. 'f 868.2925' 'b 32768')"""
import os, sys, time, serial

PORT, secs, name, cmds = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4:]
os.makedirs(os.path.join(os.path.dirname(__file__), 'captures'), exist_ok=True)
path = os.path.join(os.path.dirname(__file__), 'captures', name + '.log')

s = serial.Serial(PORT, 115200, timeout=0.1)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False  # reset into app
t0 = time.time(); sent = False
with open(path, 'wb') as f:
    while time.time() - t0 < secs:
        if not sent and time.time() - t0 > 3:
            for c in cmds: s.write((c + '\n').encode()); time.sleep(0.2)
            sent = True
        chunk = s.read(4096)
        if chunk: f.write(chunk); f.flush(); sys.stdout.write(chunk.decode(errors='replace')); sys.stdout.flush()
print(f'\n-> {path}')
