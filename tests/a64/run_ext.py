import sys
sys.path.insert(0, 'tests'); sys.path.insert(0, 'tests/a64')
import cmp_llvm, cases_ext
L = cases_ext.ext()
print('==', len(L))
cmp_llvm.report('a64', L, show=25)
cmp_llvm.overaccept('a64', L, show=8)
