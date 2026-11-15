require 'dl'
lib = DL.dlopen('/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices')
post = lib['CGPostMouseEvent', 'IFFIII']
def ev(post, x, y, down) post.call(x.to_f, y.to_f, 1, 1, down) end
cmd = ARGV[0]
if cmd == 'click'
  x, y = ARGV[1].to_i, ARGV[2].to_i
  ev(post, x, y, 0); sleep 0.15; ev(post, x, y, 1); sleep 0.15; ev(post, x, y, 0)
elsif cmd == 'drag'
  x1, y1, x2, y2 = ARGV[1..4].map { |a| a.to_i }
  ev(post, x1, y1, 0); sleep 0.2; ev(post, x1, y1, 1); sleep 0.2
  10.times { |i| ev(post, x1+(x2-x1)*(i+1)/10, y1+(y2-y1)*(i+1)/10, 1); sleep 0.05 }
  ev(post, x2, y2, 0)
end
