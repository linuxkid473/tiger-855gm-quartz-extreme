import os, sys
OLD = b'/System/Library/Frameworks/OpenGL.framework'
root, new_prefix = sys.argv[1], sys.argv[2].encode()
assert len(new_prefix) <= len(OLD)
total = 0
for dp, dn, fn in os.walk(root):
    for f in fn:
        p = os.path.join(dp, f)
        if os.path.islink(p) or not os.path.isfile(p): continue
        d = bytearray(open(p, 'rb').read()); n = 0; i = d.find(OLD)
        while i >= 0:
            end = d.index(b'\0', i)                      # end of this C string
            rest = bytes(d[i + len(OLD):end])
            new = new_prefix + rest
            d[i:end] = new + b'\0' * (end - i - len(new))
            n += 1; i = d.find(OLD, i + 1)
        if n:
            open(p, 'wb').write(d); total += n; print('%3d  %s' % (n, os.path.relpath(p, root)))
print('total', total)
