#!/usr/bin/ruby
# Texture upload matrix on a chosen renderer. usage: ruby tex.rb [hw|sw]
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')
which = (ARGV[0] || 'hw')

def ints(arr)
  s = arr.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p
end
mainid = CG.sym('CGMainDisplayID', 'I').call[0]
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(mainid)[0]
attrs = if which == 'hw'
  ints([73, 84, mask, 8, 32, 12, 16, 80, 0])
else
  ints([70, 0x20400, 84, mask, 8, 32, 12, 16, 80, 0])
end
pix = DL.malloc(4); npix = DL.malloc(4)
r = GL.sym('CGLChoosePixelFormat', 'IPPP').call(attrs, pix, npix)
abort "choose failed #{r[0]}" if r[0] != 0
ctxp = DL.malloc(4)
r = GL.sym('CGLCreateContext', 'IIIP').call(pix.to_a('I', 1)[0], 0, ctxp)
abort "create failed #{r[0]}" if r[0] != 0
GL.sym('CGLSetCurrentContext', 'II').call(ctxp.to_a('I', 1)[0])
glGetError = GL.sym('glGetError', 'I')
glGetError.call
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"

bind = GL.sym('glBindTexture', '0II')
gen = GL.sym('glGenTextures', '0IP')
teximg = GL.sym('glTexImage2D', '0IIIIIIIIP')
subimg = GL.sym('glTexSubImage2D', '0IIIIIIIIP')
buf = DL.malloc(512 * 512 * 4)

pstore = GL.sym('glPixelStorei', '0II')
targets = { 'RECT' => 0x84F5, '2D' => 0x0DE1 }
formats = {
  'RGBA<-YCBCR422/8_8' => [0x1908, 0x85B9, 0x85BA],
  'RGBA<-YCBCR422/8_8_REV' => [0x1908, 0x85B9, 0x85BB],
  'RGB<-YCBCR422/8_8' => [0x1907, 0x85B9, 0x85BA],
  'YCBCR422 internal' => [0x85B9, 0x85B9, 0x85BA],
  'RGBA/BGRA/8888REV (ref)' => [0x1908, 0x80E1, 0x8367],
}
sizes = [[64, 64], [640, 360], [320, 240]]
[0, 1].each do |cs|
  pstore.call(0x85B2, cs)          # GL_UNPACK_CLIENT_STORAGE_APPLE
  targets.each do |tn, t|
    formats.each do |fnm, (ifmt, fmt, typ)|
      sizes.each do |w, h|
        tex = DL.malloc(4)
        gen.call(1, tex)
        bind.call(t, tex.to_a('I', 1)[0])
        glGetError.call
        teximg.call(t, 0, ifmt, w, h, 0, fmt, typ, buf)
        e = glGetError.call[0]
        puts "client_storage=%d %-4s %-26s %3dx%-3d -> 0x%x" % [cs, tn, fnm, w, h, e]
      end
    end
  end
end
puts "ext: #{GL.sym('glGetString', 'SI').call(0x1F03)[0].to_s[0,400]}"
puts "done"
