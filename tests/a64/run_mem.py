import sys
sys.path.insert(0, 'tests'); sys.path.insert(0, 'tests/a64')
import cmp_llvm, cases_mem
for f in sys.argv[1:] or ['ldst','pairs','excl','prfm']:
    L = getattr(cases_mem, f)()
    print('==', f, len(L))
    cmp_llvm.report('a64', L, show=20)
    cmp_llvm.overaccept('a64', L, show=12)
