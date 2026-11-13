#!/usr/bin/ruby
# Issue GL work on the hardware renderer step by step and report accelerator stats after each.
# usage: ruby draw.rb STEP   (STEP: clear | quad | texquad)
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')
step = ARGV[0] || 'clear'
def ints(arr)
  s = arr.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p
end
mainid = CG.sym('CGMainDisplayID', 'I').call[0]
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(mainid)[0]
attrs = ints([73, 84, mask, 8, 32, 12, 16, 80, 0])
pix = DL.malloc(4); npix = DL.malloc(4)
abort "choose" if GL.sym('CGLChoosePixelFormat', 'IPPP').call(attrs, pix, npix)[0] != 0
ctxp = DL.malloc(4)
abort "create" if GL.sym('CGLCreateContext', 'IIIP').call(pix.to_a('I', 1)[0], 0, ctxp)[0] != 0
GL.sym('CGLSetCurrentContext', 'II').call(ctxp.to_a('I', 1)[0])
ge = GL.sym('glGetError', 'I'); ge.call
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"

def stats(tag)
  s = `ioreg -l -w0 | grep PerformanceStatistics | tr "," "\n" | grep -E "submitStamp|lastReadStamp|contextGLCount|bufferSwapCount"`.split("\n").map { |x| x.strip.delete('"') }.join(' ')
  puts "[#{tag}] #{s}"
end
stats('start')

glViewport = GL.sym('glViewport', '0IIII')
glClear = GL.sym('glClear', '0I')
glClearColor = GL.sym('glClearColor', '0FFFF')
glFlush = GL.sym('glFlush', '0')
glViewport.call(0, 0, 256, 256)
glClearColor.call(0.2, 0.4, 0.6, 1.0)
glClear.call(0x4000)
puts "after clear: err 0x%x" % ge.call[0]
glFlush.call
puts "after flush: err 0x%x" % ge.call[0]
stats('clear')
exit if step == 'clear'

glBegin = GL.sym('glBegin', '0I'); glEnd = GL.sym('glEnd', '0')
glVertex2f = GL.sym('glVertex2f', '0FF'); glColor4f = GL.sym('glColor4f', '0FFFF')
glBegin.call(7)
glColor4f.call(1.0, 0.0, 0.0, 1.0)
glVertex2f.call(-0.5, -0.5); glVertex2f.call(0.5, -0.5); glVertex2f.call(0.5, 0.5); glVertex2f.call(-0.5, 0.5)
glEnd.call
glFlush.call
puts "after quad: err 0x%x" % ge.call[0]
stats('quad')
exit if step == 'quad'
puts "texquad not implemented"
