#!/usr/bin/ruby
# Find a GL command buffer in another process by a marker dword and summarise its layout.
# usage: sudo ruby bufdump.rb PID MARKER_HEX
require 'dl'
SYS = DL.dlopen('/usr/lib/libSystem.dylib')
pid = ARGV[0].to_i
pat = [ARGV[1].hex].pack('V')
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
  a = s
  while a < e
    n = [e - a, 0x10000].min
    if vm_read.call(t, a, n, buf.to_i, out)[0] == 0
      got = out.to_a('I', 1)[0]
      data = buf[0, got]
      if (i = data.index(pat))
        words = data.unpack('V*')
        puts "region 0x%08x-0x%08x %s ; marker at +0x%x of this 64K chunk (chunk base 0x%08x)" % [s, e, prot, i, a]
        nz = []
        words.each_with_index { |v, k| nz << [k * 4, v] if v != 0 }
        puts "nonzero dwords: #{nz.length} of #{words.length}"
        puts "-- first 60 --"
        nz[0, 60].each { |o, v| puts "  +0x%04x: %08x" % [o, v] }
        puts "-- last 30 --"
        nz[-30..-1].each { |o, v| puts "  +0x%04x: %08x" % [o, v] } if nz.length > 30
        exit
      end
    end
    a += n
  end
end
puts "marker not found"
