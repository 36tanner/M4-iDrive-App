"""Compare constant arguments at calls into QNX libraries: stock v0.7.6 worker vs this build."""
import re, subprocess, sys
from collections import defaultdict
CALLEES = {"fcntl","socket","setsockopt","getsockopt","clock_gettime","pthread_condattr_setclock","dlopen",
           "poll","sigaction","signal","sigaddset","sigprocmask","pthread_sigmask","access","recv","send",
           "sendto","recvfrom","alarm","connect","bind","inet_pton"}
def plt_names(path):
    """PLT entry address -> imported name: from undefined dynsym values (gcc/ld) or .rel.plt order (lld)."""
    names = {}
    syms = subprocess.run(["readelf","--dyn-syms","-W",path],capture_output=True,text=True).stdout
    for line in syms.splitlines():
        f = line.split()
        if len(f) >= 8 and f[6] == "UND" and f[3] == "FUNC" and int(f[1],16):
            names[int(f[1],16)] = f[7].split("@")[0]
    if names: return names
    secs = subprocess.run(["readelf","-S","-W",path],capture_output=True,text=True).stdout
    m = re.search(r"\.plt\s+PROGBITS\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", secs)
    start, size = int(m.group(1),16), int(m.group(2),16)
    rels = subprocess.run(["readelf","-r","-W",path],capture_output=True,text=True).stdout
    plt = [l.split()[4].split("@")[0] for l in rels.split(".rel.plt",1)[1].splitlines() if "JUMP_SLOT" in l]
    header = size - 16*len(plt)
    return {start+header+16*i: n for i,n in enumerate(plt)}

def calls(path):
    plt = plt_names(path)
    dis = subprocess.run(["llvm-objdump","-d","--no-show-raw-insn",path],capture_output=True,text=True).stdout
    func, window, out = None, [], defaultdict(set)
    for line in dis.splitlines():
        m = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if m: func, window = m.group(1), []; continue
        ins = line.split("\t",1)[-1].strip() if "\t" in line else line.strip()
        m = re.search(r"\bbl\w*\s+0x([0-9a-f]+)", ins)
        target = plt.get(int(m.group(1),16)) if m else None
        if target in CALLEES and func and not func.startswith("$"):
            regs = {}
            for w in window[-10:]:
                mm = re.match(r"(mov|movw|mvn)\w*\s+(r[0-3]),\s*#(-?0x[0-9a-f]+|-?\d+)", w)
                if mm:
                    v = int(mm.group(3),0)
                    if mm.group(1)=="mvn": v = ~v & 0xffffffff
                    regs[mm.group(2)] = v
            out[(func,target)].add(tuple(sorted(regs.items())))
            window=[]; continue
        window.append(ins)
    return out
stock, mine = calls(sys.argv[1]), calls(sys.argv[2])
for key in sorted(set(stock)|set(mine)):
    s, m = stock.get(key,set()), mine.get(key,set())
    flat = lambda sets: sorted({kv for t in sets for kv in t})
    fs, fm = flat(s), flat(m)
    status = "same" if fs==fm else "DIFF"
    print(f"{status:4} {key[0]:32} {key[1]:26} stock={fs} mine={fm}")
