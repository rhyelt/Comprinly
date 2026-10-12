import sys
sys.path.insert(0, 'tests'); sys.path.insert(0, 'tests/a64')
import cmp_llvm, cases_fp
for f in ['arith','cvt']:
    L = getattr(cases_fp, f)()
    print('==', f, len(L))
    cmp_llvm.report('a64', L, show=20)
    cmp_llvm.overaccept('a64', L, show=12)
