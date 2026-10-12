import sys
sys.path.insert(0, 'tests'); sys.path.insert(0, 'tests/rv')
import cmp_llvm, cases
arch = sys.argv[1]
fams = sys.argv[2:] or ['rtype','itype','mem','branches','system','atomics','fp','lis','zext','cexpl']
rv64 = arch.startswith('rv64')
for f in fams:
    fn = getattr(cases, f)
    L = fn(rv64) if f in ('rtype','itype','mem','system','atomics','fp','zext','cexpl') else fn()
    print('==', arch, f, len(L))
    cmp_llvm.report(arch, L, show=12, reject_show=12)
    cmp_llvm.overaccept(arch, L, show=8)
