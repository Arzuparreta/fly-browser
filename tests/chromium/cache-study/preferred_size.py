"""Linux automatic sizing at Fly's pin; --upstream shows the study's baseline."""
import argparse
MiB = 2**20; D = 80 * MiB
def internal(a):
    if a < D * 10 // 8: return a * 8 // 10
    if a < D * 10: return D
    if a < D * 25: return a // 10
    if a < D * 250: return D * 5 // 2
    return a // 100
def preferred(a, http=True, upstream=False):
    pct = 400 if http else 100  # code-cache experiment is disabled by default
    scaled = D * pct // 100
    p = scaled if a is None else internal(a)
    if a is not None and p < a // 5: p = min(p * pct // 100, a // 5)
    ceiling = 256 * MiB if http and not upstream else scaled * 4
    return min(p, scaled * 4, ceiling)
if __name__ == '__main__':
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--upstream', action='store_true')
  args = parser.parse_args()
  for gib in [1, 2, 4, 6.19, 8, 10, 16, 20, 32, 50, 100, 300]:
    a = int(gib * 2**30)
    print(f"{gib:>6} GiB free -> HTTP {preferred(a, upstream=args.upstream)/MiB:7.0f} MiB, JS code cache {preferred(a, False)/MiB:5.0f} MiB")
