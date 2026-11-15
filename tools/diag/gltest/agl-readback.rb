#!/usr/bin/ruby
require 'dl'
CB = DL.dlopen('/System/Library/Frameworks/Carbon.framework/Carbon')
AGL = DL.dlopen('/System/Library/Frameworks/AGL.framework/AGL')
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
def ints(arr) s = arr.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p end
rect = DL.malloc(8); rect[0, 8] = [50, 50, 306, 306].pack('s4')
winp = DL.malloc(4)
r = CB.sym('CreateNewWindow', 'IIIPP').call(6, 0x0, rect, winp)
puts "CreateNewWindow -> #{r[0]}"
win = winp.to_a('I', 1)[0]
CB.sym('ShowWindow', '0I').call(win)
port = CB.sym('GetWindowPort', 'II').call(win)[0]
depth = (ENV['DEPTH'] || '16').to_i; st = (ENV['STENCIL'] || '0').to_i
lst = [4, 5, 12, depth]
lst += [13, st] if st > 0
attrs = ints(lst + [0])
pix = AGL.sym('aglChoosePixelFormat', 'IPIP').call(nil, 0, attrs)[0]
puts "pixfmt #{pix}"
ctx = AGL.sym('aglCreateContext', 'III').call(pix, 0)[0]
puts "ctx #{ctx} err=#{AGL.sym('aglGetError','I').call[0]}"
AGL.sym('aglSetDrawable', 'III').call(ctx, port)
AGL.sym('aglSetCurrentContext', 'II').call(ctx)
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"
ge = GL.sym('glGetError', 'I')
GL.sym('glViewport', '0IIII').call(0, 0, 256, 256)
GL.sym('glClearColor', '0FFFF').call(0.0, 0.0, 0.0, 1.0)
GL.sym('glClearDepth', '0D').call(1.0)
GL.sym('glClear', '0I').call(0x4100)
GL.sym('glEnable', '0I').call(0x0B71)  # depth test
GL.sym('glMatrixMode', '0I').call(0x1701); GL.sym('glLoadIdentity','0').call
def quad(z, r, g, b, x0, y0, x1, y1)
  GL.sym('glColor4f', '0FFFF').call(r, g, b, 1.0)
  GL.sym('glBegin', '0I').call(7)
  [[x0,y0],[x1,y0],[x1,y1],[x0,y1]].each { |x, y| GL.sym('glVertex3f', '0FFF').call(x, y, z) }
  GL.sym('glEnd', '0').call
end
quad(0.0, 1.0, 0.0, 0.0, -0.5, -0.5, 0.5, 0.5)     # red quad, z=0 -> depth 0.5
quad(-0.5, 0.0, 1.0, 0.0, 0.0, 0.0, 0.8, 0.8)     # green quad nearer, z=-0.5 -> depth 0.25
GL.sym('glFinish', '0').call
puts "draw err 0x%x" % ge.call[0]
rp = GL.sym('glReadPixels', '0IIIIIIP')
buf = DL.malloc(16)
[[128,128,'center (green over red)'],[100,100,'red-only region'],[10,10,'background'],[200,200,'green only']].each do |x, y, label|
  ge.call
  rp.call(x, y, 1, 1, 0x1902, 0x1406, buf); ed = ge.call[0]
  d = buf.to_a('F', 1)[0]
  rp.call(x, y, 1, 1, 0x1908, 0x1401, buf); ec = ge.call[0]
  c = buf[0, 4].unpack('C4').inspect
  puts "%-26s depth=%.4f (err 0x%x) rgba=%s (err 0x%x)" % [label, d, ed, c, ec]
end
