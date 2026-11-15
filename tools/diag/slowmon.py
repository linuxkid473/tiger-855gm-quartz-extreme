#!/usr/bin/python
# Runs as root on the Tiger box. Logs 1 line / cycle and auto-captures evidence when it slows down.
import time, os, sys
D = '/var/log/slowmon'
if not os.path.isdir(D): os.makedirs(D)
log = open(D + '/log.txt', 'a')
ev = open(D + '/events.txt', 'a')

def bench():
    t = time.time()
    x = 0
    for i in xrange(60000):
        x = (x + i) & 0xffff
    return time.time() - t

def sh(cmd):
    p = os.popen(cmd + ' 2>&1'); s = p.read(); p.close(); return s

def pid_of(name):
    for l in sh('ps -axco pid,command').split('\n'):
        f = l.split()
        if len(f) == 2 and f[1] == name: return f[0]
    return None

def temp():
    for l in sh('ioreg -c AppleACPIThermalZone').split('\n'):
        if 'Zone Temperature' in l:
            try: return '%.1fC' % (int(l.split('=')[1]) / 10.0 - 273.15)
            except: return '?'
    return '?'

def gpu():
    o = sh('ioreg -c IOIntel830Accelerator -l')
    import re
    m = re.search(r'"lastReadStamp"=(\d+).*?"submitStamp"=(\d+)', o)
    if m: return 'gpu rd %s sub %s' % (m.group(1), m.group(2))
    return 'gpu ?'

def dash():
    r = []
    for l in sh('ps -axco pid,command').split('\n'):
        f = l.split()
        if len(f) == 2 and f[1] in ('DashboardClient', 'Dock'): r.append(f[1][:4] + f[0])
    r.sort()
    return ' '.join(r)

lastdash = ''
base = []
n = 0
last = time.time()
cool = 0
print >>log, '--- start', time.strftime('%H:%M:%S')
log.flush()
while 1:
    b = bench()
    base.append(b); base = base[-300:]
    sb = base[:]; sb.sort()
    ref = sb[len(sb) / 10]       # ~10th percentile = the fast speed
    ratio = b / ref
    now = time.time()
    gap = now - last
    last = now
    top = sh('top -l 2 -s 1 -n 4')
    cpu = ''
    procs = []
    for l in top.split('\n'):
        if 'CPU usage' in l: cpu = l.split('CPU usage:')[1].strip()
        f = l.split()
        if len(f) > 3 and f[0].isdigit(): procs.append(f[1] + ' ' + f[2])
    procs = procs[-4:]
    tp = temp()
    line = '%s b=%.0fms r=%.2f gap=%.1f T=%s | %s | %s | %s' % (time.strftime('%H:%M:%S'), b * 1000, ratio, gap, tp, cpu, '; '.join(procs), gpu())
    print >>log, line
    log.flush()
    try: os.fsync(log.fileno())
    except: pass
    n += 1
    dd = dash()
    if dd != lastdash:
        print >>ev, '--- PROCESS SET CHANGED %s : %s' % (time.strftime('%H:%M:%S'), dd)
        ev.flush()
        lastdash = dd
    trig = (len(base) > 20 and ratio > 1.6) or gap > 3.5 or os.path.exists(D + '/NOW')
    if trig and now > cool:
        cool = now + 25
        if os.path.exists(D + '/NOW'): os.unlink(D + '/NOW')
        tag = time.strftime('%H%M%S')
        print >>ev, '=== EVENT %s ratio=%.2f gap=%.1f : %s' % (tag, ratio, gap, line)
        ev.flush()
        ws = pid_of('WindowServer'); dk = pid_of('Dock')
        out = []
        out.append(sh('vm_stat | head -12'))
        out.append(sh('ioreg -l | grep -o "\\"lastReadStamp\\"=[0-9]*,.*\\"submitStamp\\"=[0-9]*" | head -2'))
        out.append(sh('top -l 2 -s 1 -n 12 | tail -16'))
        open(D + '/ev_%s_state.txt' % tag, 'w').write('\n'.join(out))
        if ws: sh('sample %s 3 -file %s/ev_%s_ws.txt' % (ws, D, tag))
        if dk: sh('sample %s 2 -file %s/ev_%s_dock.txt' % (dk, D, tag))
        # re-measure after capture so we know if it was still slow
        print >>ev, '    after-capture bench: %.0f ms (ref %.0f ms)' % (bench() * 1000, ref * 1000)
        ev.flush()
