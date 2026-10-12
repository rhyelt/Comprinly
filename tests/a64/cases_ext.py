def ext():
    L = []
    for n in 'retaa retab eretaa eretab sb ssbb pssbb cfinv xaflag axflag tcommit bti'.split(): L.append(n)
    L += ['bti c', 'bti j', 'bti jc']
    for n in 'pacia pacib pacda pacdb autia autib autda autdb'.split():
        L += ['%s x1, x2' % n, '%s x30, sp' % n, '%s x0, x29' % n]
    for n in 'paciza pacizb pacdza pacdzb autiza autizb autdza autdzb xpaci xpacd'.split(): L += ['%s x1' % n, '%s x30' % n]
    for n in 'braaz brabz blraaz blrabz'.split(): L += ['%s x1' % n, '%s x30' % n]
    for n in 'braa brab blraa blrab'.split(): L += ['%s x1, x2' % n, '%s x30, sp' % n]
    L += ['pacga x1, x2, x3', 'pacga x1, xzr, sp']
    for n in 'ldraa ldrab'.split():
        for o in ['', ', #8', ', #-8', ', #4088', ', #-4096']:
            L.append('%s x1, [x2%s]' % (n, o)); L.append('%s x1, [sp%s]!' % (n, o) if o else '%s x1, [sp]' % n)
    L += ['irg x1, x2', 'irg sp, sp', 'irg x1, x2, x3', 'irg x1, sp, xzr', 'gmi x1, x2, x3', 'gmi x1, sp, x3']
    for n in 'addg subg'.split():
        for o in [0, 16, 1008]:
            for t in [0, 15]: L.append('%s x1, x2, #%d, #%d' % (n, o, t)); L.append('%s sp, sp, #%d, #%d' % (n, o, t))
    L += ['subp x1, x2, x3', 'subp x1, sp, sp', 'subps x1, x2, x3', 'cmpp x1, x2', 'cmpp sp, x2']
    L += ['ldg x1, [x2]', 'ldg x1, [x2, #16]', 'ldg x1, [sp, #-4096]', 'ldg x1, [x2, #4080]']
    for n in 'stg stzg st2g stz2g'.split():
        for o in ['', ', #16', ', #-16', ', #4080']:
            L.append('%s x1, [x2%s]' % (n, o)); L.append('%s sp, [x2%s]' % (n, o))
            L.append('%s x1, [x2%s]!' % (n, o) if o else '%s x1, [x2]' % n)
        L += ['%s x1, [x2], #32' % n, '%s x1, [sp], #-32' % n]
    for o in ['', ', #16', ', #-1024', ', #1008']:
        L.append('stgp x1, x2, [x3%s]' % o); L.append('stgp x1, x2, [x3%s]!' % o if o else 'stgp x1, x2, [x3]')
    L += ['stgp x1, x2, [x3], #32', 'stgp x1, x2, [sp], #-64']
    L += ['ldgm x1, [x2]', 'stgm x1, [x2]', 'stzgm x1, [x2]', 'ldgm x1, [sp]']
    L += ['setf8 w1', 'setf16 w30', 'rmif x1, #3, #5', 'rmif x30, #63, #15', 'udf', 'udf #5', 'udf #65535', 'wfet x1', 'wfit x30', 'tstart x1', 'ttest x2', 'tcancel #5']
    for n in 'abs cnt ctz'.split(): L += ['%s x1, x2' % n, '%s w1, w2' % n]
    for n in 'smax umax smin umin'.split():
        L += ['%s x1, x2, x3' % n, '%s w1, w2, w3' % n]
    L += ['smax x1, x2, #5', 'smax w1, w2, #-128', 'smax x1, x2, #127', 'smin x1, x2, #-1', 'umax x1, x2, #255', 'umin w1, w2, #0', 'umax w1, w2, #1']
    for n in 'ldapurb ldapurh stlurb stlurh'.split():
        L += ['%s w1, [x2]' % n, '%s w1, [x2, #4]' % n, '%s w1, [x2, #-256]' % n, '%s w1, [x2, #255]' % n]
    for n in 'ldapursb ldapursh'.split():
        L += ['%s w1, [x2, #4]' % n, '%s x1, [x2, #-4]' % n]
    L += ['ldapursw x1, [x2, #4]', 'ldapur w1, [x2]', 'ldapur x1, [sp, #8]', 'stlur w1, [x2, #-4]', 'stlur x1, [x2]']
    L += ['ld64b x0, [x1]', 'st64b x0, [x1]', 'ld64b x2, [sp]']
    return L
