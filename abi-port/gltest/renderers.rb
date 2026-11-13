#!/usr/bin/ruby
# List CGL renderers on Tiger using Ruby's DL (no compiler needed).
require 'dl'
GL = DL.dlopen('/System/Library/Frameworks/OpenGL.framework/OpenGL')
CG = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/CoreGraphics')

mainid = CG.sym('CGMainDisplayID', 'I').call[0]
mask = CG.sym('CGDisplayIDToOpenGLDisplayMask', 'II').call(mainid)[0]
puts "display id #{mainid} mask 0x#{mask.to_s(16)}"

rend = DL.malloc(4)
n = DL.malloc(4)
r = GL.sym('CGLQueryRendererInfo', 'IIPP').call(mask, rend, n)
puts "CGLQueryRendererInfo -> #{r[0]}"
count = n.to_a('I', 1)[0]
robj = rend.to_a('I', 1)[0]
puts "renderers: #{count}"
props = {
  'RendererID' => 70, 'Accelerated' => 73, 'OffScreen' => 53, 'Window' => 80,
  'DisplayMask' => 84, 'VideoMemory' => 120, 'TextureMemory' => 121
}
describe = GL.sym('CGLDescribeRenderer', 'IIIIP')
count.times do |i|
  line = "  [#{i}]"
  props.each do |name, p|
    v = DL.malloc(4)
    rc = describe.call(robj, i, p, v)[0]
    line += " #{name}=" + (rc == 0 ? "0x" + v.to_a('I', 1)[0].to_s(16) : "err#{rc}")
  end
  puts line
end
