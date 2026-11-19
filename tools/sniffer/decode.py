#!/usr/bin/env python3
"""Decode iSense RF <-> iBase RF frames from a sniffer log (bit-sync mode, `b 32768`).

This is the reference decoder used during reverse engineering (see docs/protocol.md).
Usage: decode.py captures/<name>.log        (no argument: self-test)
"""
import sys

SYNC = '000111'           # preamble's last 0 + "00111"
DATA_BITS = 206
OT_NAMES = {0: 'status', 1: 'control-setpoint', 16: 'room-setpoint', 17: 'rel-modulation', 20: 'day-time',
            21: 'date', 22: 'year', 25: 'flow-temp', 28: 'return-temp', 126: 'master-version', 127: 'slave-version'}


def manchester_bits(chips):
    s = chips.find(SYNC, 16)
    if s < 0:
        return None
    c, out = chips[s + len(SYNC):], []
    for i in range(0, len(c) - 1, 2):
        p = c[i:i + 2]
        if p == '01': out.append('1')
        elif p == '10': out.append('0')
        else: break
    return ''.join(out)


def crc16(bits, poly=0x3D65, init=0x1A12):
    r = init
    for ch in bits:
        fb = ((r >> 15) & 1) ^ (ch == '1')
        r = (r << 1) & 0xFFFF
        if fb: r ^= poly
    return r


def parse(bits):
    if bits is None or len(bits) < DATA_BITS + 16:
        return None
    d = bits[:DATA_BITS]
    val = int(d[190:206], 2)
    return dict(ok=crc16(d) == int(bits[DATA_BITS:DATA_BITS + 16], 2), type=int(d[160:168], 2),
                ot_id=int(d[172:182], 2) - 0x13C, seq=int(d[146:149], 2),
                src='display' if int(d[182:190], 2) == 1 else 'base', val=val,
                f88=(val - 65536 if val & 0x8000 else val) / 256)


def template_hex(bits):
    """display_template for the ESPHome component: display frame bits 0..205, variable fields zeroed."""
    b = list(bits[:DATA_BITS])
    for a, z in ((146, 149), (160, 168), (172, 182), (190, 206)):
        b[a:z] = '0' * (z - a)
    return '%052X' % int(''.join(b) + '00', 2)


def main(path):
    lines = open(path, errors='replace').read().splitlines()
    templates = set()
    for i, l in enumerate(lines):
        if not l.startswith('SYNCBURST') or i + 1 >= len(lines):
            continue
        f = dict(kv.split('=', 1) for kv in l.split()[1:] if '=' in kv)
        bits = manchester_bits(lines[i + 1][5:].strip())
        p = parse(bits)
        if not p:
            continue
        if p['ok'] and p['src'] == 'display':
            templates.add(template_hex(bits))
        print('%8s %6s %-4s %-7s type=%02X id=%3d %-16s val=0x%04X f8.8=%7.2f' % (
            f.get('t', '?'), f.get('rssi', '?'), 'ok' if p['ok'] else 'BAD', p['src'], p['type'], p['ot_id'],
            OT_NAMES.get(p['ot_id'], '?'), p['val'], p['f88']))
    for t in sorted(templates):
        print('display_template: "%s"' % t)


def selftest():
    # Synthetic kit (random kit-ID bits): iBase reply, id 0, value 0x030A, as decoded data+CRC bits.
    data_hex = '9D2E09896916C76AABFF9CF1270AE43A4947A7C62494F0000C2B1680'
    bits = format(int(data_hex, 16), '0224b')[:222]
    chips = '1010' * 8 + '00111' + ''.join('01' if b == '1' else '10' for b in bits) + '1100'
    p = parse(manchester_bits('10' + chips))
    assert p['ok'] and p['src'] == 'base' and p['ot_id'] == 0 and p['val'] == 0x030A, p
    bad = bits[:40] + ('0' if bits[40] == '1' else '1') + bits[41:]
    assert not parse(bad)['ok']
    print('selftest ok')


if __name__ == '__main__':
    main(sys.argv[1]) if len(sys.argv) > 1 else selftest()
