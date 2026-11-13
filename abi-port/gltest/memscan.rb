#!/usr/bin/ruby
# Scan another process's memory (root) for GPU command words and dump context.
# usage: sudo ruby memscan.rb PID PATTERN_HEX [PATTERN_HEX...]
require 'dl'
SYS = DL.dlopen('/usr/lib/libSystem.dylib')
pid = ARGV[0].to_i
pats = ARGV[1..-1].map { |h| [h.hex].pack('V') }
self_task = SYS['mach_task_self_'] rescue nil
tfp = SYS.sym('task_for_pid', 'IIIP')
mts = SYS.sym('mach_task_self', 'I')
me = mts.call[0]
task = DL.malloc(4)
r = tfp.call(me, pid, task)[0]
abort "task_for_pid failed: #{r}" if r != 0
t = task.to_a('I', 1)[0]
vm_read = SYS.sym('vm_read_overwrite', 'IIIIIP')
buf = DL.malloc(0x10000)
out = DL.malloc(4)

# region list from vmmap
regions = []
`vmmap #{pid} 2>/dev/null`.each_line do |l|
  if l =~ /([0-9a-f]{8})-([0-9a-f]{8})\s+\[\s*\d+K?\]\s+(\S+)/
    regions << [$1.hex, $2.hex, $3]
  end
end
puts "regions: #{regions.length}"
hits = 0
regions.each do |s, e, prot|
  next unless prot[0,2] == 'rw' && e < 0x7fffffff
  a = s
  while a < e
    n = [e - a, 0x10000].min
    rc = vm_read.call(t, a, n, buf.to_i, out)[0]
    if rc == 0
      got = out.to_a('I', 1)[0]
      data = buf[0, got]
      pats.each do |p|
        i = 0
        while (i = data.index(p, i))
          addr = a + i
          start = [addr - 0x40, a].max
          # re-read a window around the hit
          rc2 = vm_read.call(t, start, 0x100, buf.to_i, out)[0]
          if rc2 == 0
            w = buf[0, 0x100].unpack('V*')
            puts "HIT %s at 0x%08x (region 0x%08x-0x%08x %s)" % [p.unpack('V')[0].to_s(16), addr, s, e, prot]
            w.each_with_index { |v, k| puts "  0x%08x: %08x%s" % [start + k * 4, v, (start + k * 4 == addr ? '  <==' : '')] }
            hits += 1
            return_early = hits >= 3
            exit if return_early
          end
          i += 4
        end
      end
    end
    a += n
  end
end
puts "done, hits=#{hits}"
