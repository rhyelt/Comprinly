import sys
sys.path.insert(0, 'tests'); sys.path.insert(0, 'tests/a64')
import cmp_llvm, cases_simd
names = sys.argv[1:] or ['three', 'two', 'misc', 'elem', 'immv', 'shifts', 'byel', 'ldst', 'crypto', 'dots']
for f in names:
    L = getattr(cases_simd, f)()
    print('==', f, len(L))
    cmp_llvm.report('a64', L, show=25)
    cmp_llvm.overaccept('a64', L, show=8)
