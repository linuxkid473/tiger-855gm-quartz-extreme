import struct
def parse(path, off=0):
    d=open(path,"rb").read()
    magic,cpu,sub,ft,ncmds,szc,flags=struct.unpack_from("<IiiIIII",d,off)
    assert magic==0xfeedface,hex(magic)
    p=off+28; cmds=[]; secs=[]; sym=None
    for _ in range(ncmds):
        cmd,size=struct.unpack_from("<II",d,p)
        if cmd==1:
            segname=d[p+8:p+24].rstrip(b"\0").decode(); vmaddr,vmsize,fileoff,filesize,mp,ip,nsects,fl=struct.unpack_from("<IIIIiiII",d,p+24)
            q=p+56
            for _ in range(nsects):
                sn=d[q:q+16].rstrip(b"\0").decode(); sg=d[q+16:q+32].rstrip(b"\0").decode()
                addr,sz,o,al,reloff,nreloc,fl2=struct.unpack_from("<IIIIIII",d,q+32)
                secs.append(dict(name=sn,seg=sg,addr=addr,size=sz,off=o,reloff=reloff,nreloc=nreloc)); q+=68
        elif cmd==2:
            symoff,nsyms,stroff,strsize=struct.unpack_from("<IIII",d,p+8); sym=(symoff,nsyms,stroff,strsize)
        cmds.append((cmd,size)); p+=size
    syms=[]
    if sym:
        symoff,nsyms,stroff,strsize=sym
        for i in range(nsyms):
            n_strx,n_type,n_sect,n_desc,n_value=struct.unpack_from("<IBBhI",d,symoff+i*12)
            nm=d[stroff+n_strx:d.index(b"\0",stroff+n_strx)].decode(errors="replace")
            syms.append((nm,n_type,n_sect,n_desc,n_value))
    return dict(d=d,ft=ft,secs=secs,syms=syms,cmds=cmds)

def relocs(m, sec):
    d=m["d"]; out={}
    for i in range(sec["nreloc"]):
        a,info=struct.unpack_from("<II",d,sec["reloff"]+i*8)
        if a & 0x80000000:  # scattered
            out[a & 0xffffff]=("scattered",info); continue
        symnum=info&0xffffff; pcrel=(info>>24)&1; length=(info>>25)&3; ext=(info>>27)&1; typ=(info>>28)&0xf
        out[a]=(symnum,ext,typ)
    return out
