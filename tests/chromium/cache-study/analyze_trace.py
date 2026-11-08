"""Summarise a Fly startup trace: Fly's own trace events (count, total and
largest duration) and the browser main-thread tasks posted from Fly sources,
grouped by function. Repeated, costly work is where a cache could pay off."""
import json, sys, collections
data = json.load(open(sys.argv[1]))
events = data['traceEvents'] if isinstance(data, dict) else data
names = {}
for e in events:
    if e.get('ph') == 'M' and e.get('name') == 'process_name':
        names[e['pid']] = e['args']['name']
browser = next((p for p, n in names.items() if n == 'Browser'), None)
fly = collections.defaultdict(list)
tasks = collections.defaultdict(list)
for e in events:
    if e.get('ph') != 'X':
        continue
    n = e.get('name', '')
    if n.startswith('Fly.'):
        fly[n].append(e.get('dur', 0))
    if e.get('pid') == browser and n in ('ThreadControllerImpl::RunTask', 'ThreadPool_RunTask'):
        a = e.get('args', {})
        src = a.get('src_file', '') or a.get('task', {}).get('posted_from', {}).get('file_name', '')
        fn = a.get('src_func', '') or a.get('task', {}).get('posted_from', {}).get('function_name', '')
        if 'fly/' in src:
            tasks[f"{src.split('fly/')[-1]}:{fn}"].append(e.get('dur', 0))
def show(title, groups):
    print(f'== {title}')
    print('name count total_ms max_ms')
    for k, v in sorted(groups.items(), key=lambda kv: -sum(kv[1])):
        print(k, len(v), round(sum(v) / 1000, 2), round(max(v) / 1000, 2))
show('Fly trace events', fly)
show('Browser tasks posted from Fly sources', tasks)
json.dump({'fly_events': {k: dict(count=len(v), total_ms=sum(v) / 1000, max_ms=max(v) / 1000) for k, v in fly.items()},
           'fly_tasks': {k: dict(count=len(v), total_ms=sum(v) / 1000, max_ms=max(v) / 1000) for k, v in tasks.items()}},
          open(sys.argv[2], 'w'), indent=1)
