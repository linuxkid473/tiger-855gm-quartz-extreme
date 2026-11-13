#!/usr/bin/ruby
# Dump consecutive dwords from the 64K GL command buffer in another process.
# usage: sudo ruby bufwords.rb PID MARKER_HEX START_OFFSET_HEX COUNT
require 'dl'
SYS = DL.dlopen('/usr/lib/libSystem.dylib')
pid = ARGV[0].to_i
pat = [ARGV[1].hex].pack('V')
start = ARGV[2].hex
count = ARGV[3].to_i
me = SYS.sym('mach_task_self', 'I').call[0]
task = DL.malloc(4)
abort "tfp failed" if SYS.sym('task_for_pid', 'IIIP').call(me, pid, task)[0] != 0
t = task.to_a('I', 1)[0]
vm_read = SYS.sym('vm_read_overwrite', 'IIIIIP')
buf = DL.malloc(0x10000)
out = DL.malloc(4)
regions = []
`vmmap #{pid} 2>/dev/null`.each_line do |l|
  regions << [$1.hex, $2.hex, $3] if l =~ /([0-9a-f]{8})-([0-9a-f]{8})\s+\[\s*\d+K?\]\s+(\S+)/
end
regions.each do |s, e, prot|
  next unless prot[0, 2] == 'rw' && e < 0x7fffffff && (e - s) == 0x10000
  if vm_read.call(t, s, 0x10000, buf.to_i, out)[0] == 0
    data = buf[0, 0x10000]
    if data.index(pat)
      words = data.unpack('V*')
      puts "buffer at 0x%08x" % s
      (start / 4).upto(start / 4 + count - 1) do |k|
        puts "  +0x%04x: %08x" % [k * 4, words[k]]
      end
      exit
    end
  end
end
puts "not found"
