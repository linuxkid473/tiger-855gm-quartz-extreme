import subprocess,re,struct,sys
sys.path.insert(0,".")
import macho32
def sections(imgfile):
    o=subprocess.run(["otool","-l",imgfile],capture_output=True,text=True).stdout
    return [(n,s,int(a,16),int(z,16),int(off)) for n,s,a,z,off in re.findall(r"sectname (\S+)\n\s+segname (\S+)\n\s+addr (0x[0-9a-f]+)\n\s+size (0x[0-9a-f]+)\n\s+offset (\d+)",o)]
def build(symsrc, imgfile, symkind):
    # symsrc: list of (name,addr) ; returns {mangledClass: [slot names or None]} (slot index = dword index in vtable)
    a2n={}; vts={}
    for n,a in symsrc:
        a2n.setdefault(a,n)
        if n.startswith("__ZTV") and not n.startswith("__ZTVN"): vts[n[5:]]=a
    secs=sections(imgfile); data=open(imgfile,"rb").read()
    def rd(a):
        for n,s,ad,sz,off in secs:
            if ad<=a<ad+sz and off: return struct.unpack_from("<I",data,off+a-ad)[0]
    srt=sorted(vts.values()); lay={}
    for cls,a in vts.items():
        nxt=next((x for x in srt if x>a),a+4*400)
        sl=[]
        for i in range((nxt-a)//4):
            v=rd(a+i*4); sl.append(a2n.get(v) if v else None)
        lay[cls]=sl
    return lay
def old_layouts():
    m=macho32.parse("mach41.sym")
    syms=[(n,v) for n,t,s,de,v in m["syms"] if v]
    return build(syms,"k1041.i386","old")
def new_layouts():
    nm=subprocess.run(["nm","-n","k1048"],capture_output=True,text=True).stdout.splitlines()
    syms=[]
    for l in nm:
        p=l.split()
        if len(p)==3: syms.append((p[2],int(p[0],16)))
    return build(syms,"k1048","new")
