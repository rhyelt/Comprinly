import sys
sys.path.insert(0, 'tests')
sys.path.insert(0, 'tests/a64')
import cmp_llvm, cases_int
fam = sys.argv[1:] or ['addsub','logical','movs','pcrel','bitfield','csel','arith','system']
for f in fam:
    L = getattr(cases_int, f)()
    print('==', f, len(L))
    cmp_llvm.report('a64', L, show=15)
