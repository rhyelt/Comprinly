#!/bin/sh
cd "$(dirname "$0")/compat" || exit 1
a=$(python3 run.py | tail -1)
b=$(python3 objrun.py | tail -1)
echo "$a"
echo "$b"
case "$a" in *"bad 0") ;; *) [ "${CLY_COMPAT_STRICT:-0}" = 1 ] && exit 1;; esac
case "$b" in *"bad 0") ;; *) exit 1;; esac
exit 0
