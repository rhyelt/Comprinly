# Comprinly 1.2.1

A bug fix release.

REMINDER: Comprinly is fully vibe-coded and for fun. Expect some bugs, and please report them.

## Fixed

- `cmp ebp,-1`, `xor ebp,ebp`, `test ebp,ebp` and `lea ebp,[rax+1]` were shortened wrongly and changed meaning. They now assemble as written.
- 64-bit `add`, `cmp` and similar now accept a symbol as the immediate.
- With `-obj`, undefined symbols are treated as external instead of being an error.
- ELF objects now carry a `.note.GNU-stack` section, so `ld` no longer warns about an executable stack.

## Tests

A new register test covers the `ebp` and `esp` cases above and runs with the rest of the suite.
