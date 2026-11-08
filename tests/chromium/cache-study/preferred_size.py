"""Reproduces net/disk_cache/cache_util.cc PreferredCacheSize for Linux at Fly's pin."""
MiB = 2**20; D = 80 * MiB
def internal(a):
    if a < D * 10 // 8: return a * 8 // 10
    if a < D * 10: return D
    if a < D * 25: return a // 10
    if a < D * 250: return D * 5 // 2
    return a // 100
def preferred(a, http=True):
    pct = 400 if http else 100  # code-cache experiment is disabled by default
    scaled = D * pct // 100
    p = internal(a)
    if p < a // 5: p = min(p * pct // 100, a // 5)
    return min(p, scaled * 4)
if __name__ == '__main__':
  for gib in [1, 2, 4, 6.19, 8, 10, 16, 20, 32, 50, 100, 300]:
    a = int(gib * 2**30)
    print(f"{gib:>6} GiB free -> HTTP {preferred(a)/MiB:7.0f} MiB, JS code cache {preferred(a, False)/MiB:5.0f} MiB")
