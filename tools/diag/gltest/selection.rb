#!/usr/bin/ruby
# GL_SELECT picking test on whichever renderer the process gets.
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')
def ints(arr) s = arr.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p end
mainid = CG.sym('CGMainDisplayID', 'I').call[0]
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(mainid)[0]
hw = (ARGV[0] != 'sw')
attrs = hw ? ints([73, 84, mask, 8, 32, 12, 16, 80, 0]) : ints([84, mask, 8, 32, 12, 16, 80, 0])
pix = DL.malloc(4); npix = DL.malloc(4)
abort "choose failed" if GL.sym('CGLChoosePixelFormat', 'IPPP').call(attrs, pix, npix)[0] != 0
ctxp = DL.malloc(4)
abort "create failed" if GL.sym('CGLCreateContext', 'IIIP').call(pix.to_a('I', 1)[0], 0, ctxp)[0] != 0
GL.sym('CGLSetCurrentContext', 'II').call(ctxp.to_a('I', 1)[0])
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"
ge = GL.sym('glGetError', 'I')
buf = DL.malloc(4 * 64)
GL.sym('glViewport', '0IIII').call(0, 0, 256, 256)
GL.sym('glMatrixMode', '0I').call(0x1701)
GL.sym('glLoadIdentity', '0').call
GL.sym('glSelectBuffer', '0IP').call(64, buf)
puts "SelectBuffer err 0x%x" % ge.call[0]
rm = GL.sym('glRenderMode', 'II')
rm.call(0x1C02)   # GL_SELECT
puts "RenderMode(SELECT) err 0x%x" % ge.call[0]
GL.sym('glInitNames', '0').call
GL.sym('glPushName', '0I').call(0)
[[1, -0.9, -0.9, -0.1, -0.1], [2, 0.1, 0.1, 0.9, 0.9]].each do |name, x0, y0, x1, y1|
  GL.sym('glLoadName', '0I').call(name)
  GL.sym('glBegin', '0I').call(7)
  GL.sym('glVertex3f', '0FFF').call(x0, y0, 0.0); GL.sym('glVertex3f', '0FFF').call(x1, y0, 0.0)
  GL.sym('glVertex3f', '0FFF').call(x1, y1, 0.0); GL.sym('glVertex3f', '0FFF').call(x0, y1, 0.0)
  GL.sym('glEnd', '0').call
end
puts "draw err 0x%x" % ge.call[0]
hits = rm.call(0x1C00)[0]   # back to RENDER, returns hit count
puts "hits = #{hits} (expect 2) err 0x%x" % ge.call[0]
words = buf.to_a('I', 16)
puts "buffer: #{words.inspect}"
