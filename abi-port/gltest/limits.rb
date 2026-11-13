#!/usr/bin/ruby
# Print GL limits reported by the chosen renderer. usage: ruby limits.rb [hw|sw]
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
gi = GL.sym('glGetIntegerv', '0IP')
ge = GL.sym('glGetError', 'I')
ge.call
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"
{ 'MAX_TEXTURE_SIZE' => 0x0D33, 'MAX_TEXTURE_UNITS_ARB' => 0x84E2, 'MAX_RECT_TEXTURE_SIZE' => 0x84F8,
  'MAX_VIEWPORT_DIMS' => 0x0D3A, 'MAX_3D_TEXTURE_SIZE' => 0x8073, 'MAX_CUBE_MAP_SIZE' => 0x851C,
  'MAX_TEXTURE_LOD_BIAS' => 0x84FD, 'MAX_ELEMENTS_VERTICES' => 0x80E8, 'RED_BITS' => 0x0D52,
  'DEPTH_BITS' => 0x0D56, 'MAX_LIGHTS' => 0x0D31, 'MAX_CLIP_PLANES' => 0x0D32 }.each do |n, e|
  v = DL.malloc(8)
  gi.call(e, v)
  a = v.to_a('I', 2)
  puts "%-22s %d %d   err=0x%x" % [n, a[0], a[1], ge.call[0]]
end
ext = GL.sym('glGetString', 'SI').call(0x1F03)[0]
puts "EXTENSIONS (#{ext.split(' ').length}): #{ext}"
