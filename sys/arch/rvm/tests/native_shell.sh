#!/bin/sh
# Run with the full BSD-make shell, including its libedit/terminfo linkage.
set -eu
[ "$((0x100000000 + 7))" = 4294967303 ]
value='alpha:beta:gamma'
[ "${value%%:*}" = alpha ]
[ "${value##*:}" = gamma ]
sum() {
    result=0
    for number do result=$((result + number)); done
    printf '%s' "$result"
}
[ "$(sum 1 2 3 4 5 6 7 8 9 10)" = 55 ]
answer=$(printf '%s\n' 'command substitution' | cat)
case "$answer" in 'command substitution') ;; *) exit 11 ;; esac
IFS=: read first second third <<EOF
$value
EOF
[ "$first/$second/$third" = alpha/beta/gamma ]
trapped=no
trap 'trapped=yes' USR1
kill -USR1 $$
[ "$trapped" = yes ]
trap - USR1
(exit 7) && exit 12 || [ "$?" = 7 ]
printf '%s\n' 'RVM native shell: arithmetic, expansions, functions, pipelines, heredocs, traps and status verified'
