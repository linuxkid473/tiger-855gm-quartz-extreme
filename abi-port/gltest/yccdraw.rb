#!/usr/bin/ruby
# Render a known YCbCr 4:2:2 rectangle texture into an offscreen buffer and read pixels back. usage: ruby yccdraw.rb hw|sw
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')
which = ARGV[0] || 'hw'
def ints(a); s = a.pack('I*'); p = DL.malloc(s.length); p[0, s.length] = s; p; end
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(CG.sym('CGMainDisplayID', 'I').call[0])[0]
attrs = which == 'hw' ? ints([73, 53, 8, 32, 12, 16, 0]) : ints([70, 0x20400, 53, 8, 32, 12, 16, 0])
pix = DL.malloc(4); npix = DL.malloc(4)
r = GL.sym('CGLChoosePixelFormat', 'IPPP').call(attrs, pix, npix); abort "choose failed #{r[0]}" if r[0] != 0
ctxp = DL.malloc(4)
r = GL.sym('CGLCreateContext', 'IIIP').call(pix.to_a('I', 1)[0], 0, ctxp); abort "create failed #{r[0]}" if r[0] != 0
ctx = ctxp.to_a('I', 1)[0]
W = 64; H = 64
fb = DL.malloc(W * H * 4); fb[0, W * H * 4] = "\0" * (W * H * 4)
r = GL.sym('CGLSetOffScreen', 'IIIIIP').call(ctx, W, H, W * 4, fb); puts "SetOffScreen -> #{r[0]}"
GL.sym('CGLSetCurrentContext', 'II').call(ctx)
ge = GL.sym('glGetError', 'I'); ge.call
puts "renderer: #{GL.sym('glGetString', 'SI').call(0x1F01)[0]}"
def px(fb, x, y); fb.to_str(W * H * 4).unpack('N*')[y * W + x]; end rescue nil
rp = GL.sym('glReadPixels', '0IIIIIIP'); out = DL.malloc(4)
read = lambda { |x, y| rp.call(x, y, 1, 1, 0x80E1, 0x8367, out); out.to_a('I', 1)[0] }
GL.sym('glViewport', '0IIII').call(0, 0, W, H)
GL.sym('glClearColor', '0FFFF').call(1.0, 0.0, 0.0, 1.0)
GL.sym('glClear', '0I').call(0x4000)
GL.sym('glFinish', '0').call
puts "after clear red:  pixel(32,32)=0x%08x err=0x%x   (expect ffff0000 BGRA)" % [read.call(32, 32), ge.call[0]]
# 16x16 YCbCr 4:2:2 'gray' (Cb=Y=Cr=0x80), 2 bytes per pixel
buf = DL.malloc(16 * 16 * 2); buf[0, 512] = "\x80" * 512
tex = DL.malloc(4); GL.sym('glGenTextures', '0IP').call(1, tex)
GL.sym('glBindTexture', '0II').call(0x84F5, tex.to_a('I', 1)[0])
GL.sym('glTexImage2D', '0IIIIIIIIP').call(0x84F5, 0, 0x1908, 16, 16, 0, 0x85B9, 0x85BA, buf)
puts "teximage ycbcr err=0x%x" % ge.call[0]
tp = GL.sym('glTexParameteri', '0III'); tp.call(0x84F5, 0x2801, 0x2600); tp.call(0x84F5, 0x2800, 0x2600)
GL.sym('glTexEnvi', '0III').call(0x2300, 0x2200, 0x1E01)
GL.sym('glEnable', '0I').call(0x84F5)
GL.sym('glColor4f', '0FFFF').call(1.0, 1.0, 1.0, 1.0)
glBegin = GL.sym('glBegin', '0I'); tc = GL.sym('glTexCoord2f', '0FF'); v = GL.sym('glVertex2f', '0FF')
glBegin.call(7); tc.call(0, 0); v.call(-1, -1); tc.call(16, 0); v.call(1, -1); tc.call(16, 16); v.call(1, 1); tc.call(0, 16); v.call(-1, 1)
GL.sym('glEnd', '0').call
GL.sym('glFinish', '0').call
puts "after ycbcr quad: pixel(32,32)=0x%08x err=0x%x   (gray ~ff808080 expected; ffffffff=white=broken; ffff0000=red=nothing drawn)" % [read.call(32, 32), ge.call[0]]
# same with a plain RGBA texture for reference
rgba = DL.malloc(16 * 16 * 4); rgba[0, 1024] = [0xff0000ff].pack('N') * 256   # BGRA bytes: blue pixel
GL.sym('glTexImage2D', '0IIIIIIIIP').call(0x84F5, 0, 0x1908, 16, 16, 0, 0x80E1, 0x8367, rgba)
glBegin.call(7); tc.call(0, 0); v.call(-1, -1); tc.call(16, 0); v.call(1, -1); tc.call(16, 16); v.call(1, 1); tc.call(0, 16); v.call(-1, 1)
GL.sym('glEnd', '0').call
GL.sym('glFinish', '0').call
puts "after rgba  quad: pixel(32,32)=0x%08x err=0x%x   (expect ff0000ff)" % [read.call(32, 32), ge.call[0]]
