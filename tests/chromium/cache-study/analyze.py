"""Summarise replay runs: per workload and cache limit, the share of requests
and bytes the HTTP disk cache served, and the network bytes left."""
import json, sys, glob, os
W = sys.argv[1]
rows = {}
for f in glob.glob(os.path.join(W, 'out-*.json')):
    name = os.path.basename(f)[4:-5]
    workload, size = name.split('-', 1)
    try:
        d = json.load(open(f))
    except Exception:
        continue
    rows.setdefault(workload, []).append(d)
order = lambda d: 10**9 if d['size'] == 'default' else int(d['size'])
out = {}
for workload, runs in rows.items():
    runs.sort(key=order)
    print(f'== {workload}')
    print('limit_MiB pass requests disk_hits hit% net_MiB hit_MiB byte_hit% load_s timeouts cache_MiB code_MiB')
    out[workload] = []
    for d in runs:
        http = d['disk'].get('Default/Cache/Cache_Data', 0) / 2**20
        code = d['disk'].get('Default/Code Cache/js', 0) / 2**20
        for p in d['perPass']:
            served = p['diskHits']
            req = p['requests'] - p['failed']
            hitb, netb = p['hitBodyBytes'] / 2**20, p['networkBytes'] / 2**20
            row = dict(limit=d['size'], pass_=p['pass'], requests=p['requests'], disk_hits=served,
                       hit_pct=round(100 * served / max(req, 1), 1), network_MiB=round(netb, 1),
                       hit_MiB=round(hitb, 1), byte_hit_pct=round(100 * hitb / max(hitb + netb, 1e-9), 1),
                       load_s=round(p['loadSeconds'], 1), timeouts=p['loadTimeouts'],
                       final_http_cache_MiB=round(http, 1), final_code_cache_MiB=round(code, 1))
            out[workload].append(row)
            print(*row.values())
json.dump(out, open(os.path.join(W, 'replay-summary.json'), 'w'), indent=1)
