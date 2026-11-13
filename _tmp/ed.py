import sys
def rep(path, old, new, count=1):
    s = open(path, encoding='utf-8').read()
    n = s.count(old)
    assert n == count, f'{path}: expected {count} got {n} for {old[:60]!r}'
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
