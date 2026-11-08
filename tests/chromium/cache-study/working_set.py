import sys, time
sys.path.insert(0, sys.argv[1]); from simple_index import parse
d = parse(sys.argv[2]); now = d['modified']
e = d['entries']; total = sum(s for _, _, s in e)
print(f"index written {time.ctime(now)}; entries={len(e)} bytes={total/2**20:.1f}MiB oldest_last_used={(now-min(t for _,t,_ in e if t))/86400:.1f}d")
print("window entries MiB %bytes")
for label, sec in [('1h',3600),('1d',86400),('2d',172800),('3d',259200),('7d',604800),('14d',1209600),('30d',2592000)]:
    sel = [s for _, t, s in e if t >= now - sec]
    print(f"{label} {len(sel)} {sum(sel)/2**20:.1f} {100*sum(sel)/total:.1f}")
sizes = sorted(s for _,_,s in e)
print("entry size p50/p90/p99/max KiB:", [round(sizes[min(len(sizes)-1,int(len(sizes)*q))]/1024,1) for q in (.5,.9,.99,1.0)])
big = sorted(sizes, reverse=True)
print("bytes in largest 10 entries MiB:", round(sum(big[:10])/2**20,1))
