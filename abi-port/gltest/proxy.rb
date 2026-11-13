#!/usr/bin/ruby
# Probe what GLEngine thinks of texture sizes on the chosen renderer. usage: ruby proxy.rb [hw|sw]
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
GL.sym('CGLSetCurrentContext', 'II').call(ctxp.to_a('I', 1)[0])
ge = GL.sym('glGetError', 'I'); ge.call
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"
teximg = GL.sym('glTexImage2D', '0IIIIIIIIP')
gtl = GL.sym('glGetTexLevelParameteriv', '0IIIP')
bind = GL.sym('glBindTexture', '0II')
gen = GL.sym('glGenTextures', '0IP')
buf = DL.malloc(256 * 256 * 4)
v = DL.malloc(4)

# proxy texture queries
[[0x8064, 'PROXY_2D']].each do |t, n|
  [1, 2, 4, 16, 64, 256, 1024, 2048, 4096].each do |s|
    teximg.call(t, 0, 0x1908, s, s, 0, 0x1908, 0x1401, nil)
    e = ge.call[0]
    gtl.call(t, 0, 0x1000, v)  # GL_TEXTURE_WIDTH
    w = v.to_a('I', 1)[0]
    puts "%-9s %5d -> err 0x%x width=%d" % [n, s, e, w]
  end
end

# real upload with NULL data (allocation only)
tex = DL.malloc(4); gen.call(1, tex); bind.call(0x0DE1, tex.to_a('I', 1)[0]); ge.call
teximg.call(0x0DE1, 0, 0x1908, 16, 16, 0, 0x1908, 0x1401, nil)
puts "2D 16x16 NULL data -> err 0x%x" % ge.call[0]
teximg.call(0x0DE1, 1, 0x1908, 8, 8, 0, 0x1908, 0x1401, nil)
puts "2D level1 8x8 NULL -> err 0x%x" % ge.call[0]
glPixelStorei = GL.sym('glPixelStorei', '0II')
glPixelStorei.call(0x0CF5, 1)    # GL_UNPACK_ALIGNMENT 1
teximg.call(0x0DE1, 0, 0x1908, 16, 16, 0, 0x1908, 0x1401, buf)
puts "2D 16x16 align1 -> err 0x%x" % ge.call[0]
