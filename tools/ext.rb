require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')
def ints(a); s=a.pack('I*'); p=DL.malloc(s.length); p[0,s.length]=s; p; end
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask','II').call(CG.sym('CGMainDisplayID','I').call[0])[0]
attrs = ints([84, mask, 8, 32, 12, 16, 80, 0])
pix=DL.malloc(4); npix=DL.malloc(4)
r=GL.sym('CGLChoosePixelFormat','IPPP').call(attrs,pix,npix); abort "choose #{r[0]}" if r[0]!=0
ctxp=DL.malloc(4); r=GL.sym('CGLCreateContext','IIIP').call(pix.to_a('I',1)[0],0,ctxp); abort "create #{r[0]}" if r[0]!=0
GL.sym('CGLSetCurrentContext','II').call(ctxp.to_a('I',1)[0])
gs=GL.sym('glGetString','SI')
puts "renderer: #{gs.call(0x1F01)[0]}"; puts "version: #{gs.call(0x1F02)[0]}"
puts gs.call(0x1F03)[0].split.join("\n")
