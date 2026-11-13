#!/usr/bin/ruby
# Create a hardware CGL context on the Intel830 renderer and probe it (no compiler needed).
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')

def ints(arr)
  s = arr.pack('I*')
  p = DL.malloc(s.length)
  p[0, s.length] = s
  p
end

mainid = CG.sym('CGMainDisplayID', 'I').call[0]
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(mainid)[0]

glGetError = GL.sym('glGetError', 'I')
glGetString = GL.sym('glGetString', 'SI')
def err(tag, f)
  e = f.call[0]
  puts "  glGetError after #{tag}: 0x#{e.to_s(16)}"
end

# kCGLPFAAccelerated=73 kCGLPFADisplayMask=84 kCGLPFAColorSize=8 kCGLPFADepthSize=12 kCGLPFAWindow=80
attrs = ints([73, 84, mask, 8, 32, 12, 16, 80, 0])
pix = DL.malloc(4); npix = DL.malloc(4)
r = GL.sym('CGLChoosePixelFormat', 'IPPP').call(attrs, pix, npix)
puts "CGLChoosePixelFormat -> #{r[0]} npix=#{npix.to_a('I',1)[0]}"
exit 1 if r[0] != 0
pixobj = pix.to_a('I', 1)[0]
ctxp = DL.malloc(4)
r = GL.sym('CGLCreateContext', 'IIIP').call(pixobj, 0, ctxp)
puts "CGLCreateContext -> #{r[0]}"
exit 1 if r[0] != 0
ctx = ctxp.to_a('I', 1)[0]
r = GL.sym('CGLSetCurrentContext', 'II').call(ctx)
puts "CGLSetCurrentContext -> #{r[0]}"
err('SetCurrentContext', glGetError)

# GL_VENDOR 0x1F00 RENDERER 0x1F01 VERSION 0x1F02 EXTENSIONS 0x1F03
[[0x1F00, 'VENDOR'], [0x1F01, 'RENDERER'], [0x1F02, 'VERSION'], [0x1F03, 'EXTENSIONS']].each do |e, n|
  s = glGetString.call(e)[0]
  puts "#{n}: #{s.inspect}"
  err(n, glGetError)
end

# a few state calls the compositor uses
glEnable = GL.sym('glEnable', '0I')
glDisable = GL.sym('glDisable', '0I')
glViewport = GL.sym('glViewport', '0IIII')
glClearColor = GL.sym('glClearColor', '0FFFF') rescue nil
{ 'GL_TEXTURE_2D' => 0x0DE1, 'GL_BLEND' => 0x0BE2, 'GL_SCISSOR_TEST' => 0x0C11,
  'GL_TEXTURE_RECTANGLE_EXT' => 0x84F5, 'GL_TEXTURE_RECTANGLE_ARB' => 0x84F5 }.each do |n, v|
  glEnable.call(v)
  err("glEnable #{n}", glGetError)
  glDisable.call(v)
  err("glDisable #{n}", glGetError)
end
glViewport.call(0, 0, 256, 256)
err('glViewport', glGetError)

# texture upload through the driver (client memory)
tex = DL.malloc(4)
GL.sym('glGenTextures', '0IP').call(1, tex)
err('glGenTextures', glGetError)
t = tex.to_a('I', 1)[0]
puts "  texture name #{t}"
GL.sym('glBindTexture', '0II').call(0x0DE1, t)
err('glBindTexture 2D', glGetError)
data = DL.malloc(64 * 64 * 4)
GL.sym('glTexImage2D', '0IIIIIIIIP').call(0x0DE1, 0, 0x1908, 64, 64, 0, 0x1908, 0x1401, data)
err('glTexImage2D 64x64 RGBA', glGetError)
GL.sym('glTexParameteri', '0III').call(0x0DE1, 0x2801, 0x2600)
err('glTexParameteri MIN', glGetError)
GL.sym('glFinish', '0').call
err('glFinish', glGetError)
puts "done"
