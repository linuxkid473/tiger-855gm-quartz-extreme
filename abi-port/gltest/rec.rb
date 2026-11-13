#!/usr/bin/ruby
# Dump GLEngine's per-renderer capability record (ctx+0x8a00, 0x144 bytes) for a renderer.
# usage: ruby rec.rb [hw|sw]
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')
which = (ARGV[0] || 'hw')
def ints(arr)
  s = arr.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p
end
mainid = CG.sym('CGMainDisplayID', 'I').call[0]
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(mainid)[0]
attrs = which == 'hw' ? ints([73, 84, mask, 8, 32, 12, 16, 80, 0]) : ints([70, 0x20400, 84, mask, 8, 32, 12, 16, 80, 0])
pix = DL.malloc(4); npix = DL.malloc(4)
abort "choose" if GL.sym('CGLChoosePixelFormat', 'IPPP').call(attrs, pix, npix)[0] != 0
ctxp = DL.malloc(4)
abort "create" if GL.sym('CGLCreateContext', 'IIIP').call(pix.to_a('I', 1)[0], 0, ctxp)[0] != 0
cgl = ctxp.to_a('I', 1)[0]
GL.sym('CGLSetCurrentContext', 'II').call(cgl)
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}  CGLContextObj=0x#{cgl.to_s(16)}"

def rd(addr, n)
  DL::PtrData.new(addr, n).to_a('I', n / 4)
end
words = rd(cgl, 0x200)
found = nil
words.each_with_index do |w, i|
  next if w < 0x100000 || w > 0xb0000000
  begin
    ctx = w
    p1 = rd(ctx + 0x4d10, 4)[0]
    if p1 == ctx + 0x8a00 + 0 || (p1 > ctx && p1 < ctx + 0x9000 && p1 >= ctx + 0x8a00)
      found = [i * 4, ctx, p1]
      break
    end
  rescue Exception
  end
end
abort "ctx not found" unless found
puts "GLE ctx at CGLContextObj+0x%x = 0x%x ; ctx+0x4d10 -> 0x%x (ctx+0x%x)" % [found[0], found[1], found[2], found[2] - found[1]]
rec = rd(found[2], 0x144)
rec.each_with_index do |v, i|
  next if v == 0
  puts "  rec+0x%03x = 0x%08x (%d)" % [i * 4, v, v < 0x80000000 ? v : v - 0x100000000]
end
