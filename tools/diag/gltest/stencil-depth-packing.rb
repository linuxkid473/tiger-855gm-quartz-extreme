#!/usr/bin/ruby
require 'dl'
CB = DL.dlopen('/System/Library/Frameworks/Carbon.framework/Carbon')
AGL = DL.dlopen('/System/Library/Frameworks/AGL.framework/AGL')
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
def ints(arr) s = arr.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p end
rect = DL.malloc(8); rect[0, 8] = [400, 770, 656, 1020].pack('s4')
winp = DL.malloc(4)
CB.sym('CreateNewWindow', 'IIIPP').call(6, 0x0, rect, winp)
win = winp.to_a('I', 1)[0]
CB.sym('ShowWindow', '0I').call(win)
port = CB.sym('GetWindowPort', 'II').call(win)[0]
dsz = (ENV['DEPTH'] || '16').to_i
pix = AGL.sym('aglChoosePixelFormat', 'IPIP').call(nil, 0, ints(([4, 5, 12, dsz] + ((ENV['STENCIL']||'0').to_i > 0 ? [13, ENV['STENCIL'].to_i] : []) + [0])))[0]
ctx = AGL.sym('aglCreateContext', 'III').call(pix, 0)[0]
AGL.sym('aglSetDrawable', 'III').call(ctx, port)
AGL.sym('aglSetCurrentContext', 'II').call(ctx)
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]} depth bits requested #{dsz}"
ge = GL.sym('glGetError', 'I'); ge.call
rp = GL.sym('glReadPixels', '0IIIIIIP')
buf = DL.malloc(16)
def rd(rp, buf, ge, tag, x, y, fmt, type)
  buf[0, 16] = "\xAA" * 16
  rp.call(x, y, 1, 1, fmt, type, buf)
  puts "  %-34s bytes=%s err=0x%x" % [tag, buf[0, 8].unpack('C8').map { |b| '%02x' % b }.join(' '), ge.call[0]]
end
v = GL.sym('glGetIntegerv', '0IP'); ib = DL.malloc(16)
v.call(0x0D56, ib); puts "  GL_DEPTH_BITS = #{ib.to_a('I',1)[0]}"
GL.sym('glViewport', '0IIII').call(0, 0, 250, 250)
GL.sym('glClearColor', '0FFFF').call(0.0, 0.0, 0.0, 1.0)
GL.sym('glClear', '0I').call(0x4100)   # default clear depth = 1.0
GL.sym('glFinish', '0').call
rd(rp, buf, ge, 'depth after default clear (1.0=00 00 80 3f)', 128, 128, 0x1902, 0x1406)
GL.sym('glEnable', '0I').call(0x0B71)
GL.sym('glMatrixMode', '0I').call(0x1701); GL.sym('glLoadIdentity','0').call
def quad(z, r, g, b, h)
  GL.sym('glColor4f', '0FFFF').call(r, g, b, 1.0)
  GL.sym('glBegin', '0I').call(7)
  [[-h,-h],[h,-h],[h,h],[-h,h]].each { |x, y| GL.sym('glVertex3f', '0FFF').call(x, y, z) }
  GL.sym('glEnd', '0').call
end
if ENV['MANUALCLEAR']
  GL.sym('glDepthFunc', '0I').call(0x207)           # ALWAYS
  GL.sym('glDepthMask', '0I').call(1)
  GL.sym('glColorMask', '0IIII').call(0, 0, 0, 0)
  quad(1.0, 0.0, 0.0, 0.0, 1.5)                     # far plane over whole window
  GL.sym('glColorMask', '0IIII').call(1, 1, 1, 1)
  GL.sym('glDepthFunc', '0I').call(0x201)           # LESS
end
GL.sym('glClearStencil', '0I').call(0)
GL.sym('glClear', '0I').call(0x4500)
GL.sym('glEnable', '0I').call(0x0B90)                  # STENCIL_TEST
GL.sym('glStencilFunc', '0III').call(0x207, 5, 0xff)   # ALWAYS ref 5
GL.sym('glStencilOp', '0III').call(0x1E01, 0x1E01, 0x1E01)  # REPLACE
quad(0.0, 1.0, 0.0, 0.0, 0.5)
GL.sym('glDisable', '0I').call(0x0B90)
GL.sym('glFinish', '0').call
rp.call(128, 128, 1, 1, 0x1901, 0x1401, buf); sc = buf[0,1].unpack('C')[0]
rp.call(10, 10, 1, 1, 0x1901, 0x1401, buf); so = buf[0,1].unpack('C')[0]
rp.call(128, 128, 1, 1, 0x1902, 0x1406, buf); d = buf.to_a('F',1)[0]
puts "  stencil(center)=#{sc} (expect 5) stencil(outside)=#{so} (expect 0) depth(center)=%.4f (expect 0.5)" % d
