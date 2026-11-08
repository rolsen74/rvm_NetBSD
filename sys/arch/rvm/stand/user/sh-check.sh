#!/bin/sh
# This script runs in the guest using the tree's unchanged NetBSD sh.
set -eu
PATH=/bin:/usr/bin
HOME=/root
TZ=UTC0
export PATH HOME TZ
[ "$(/bin/echo native echo)" = 'native echo' ]
[ "$(/bin/pwd)" = / ]
cd /etc
[ "$(/bin/pwd -P)" = /etc ]
cd /
[ ~ = /root ]
quoted='two words * $literal'
[ "$quoted" = 'two words * $literal' ]
sum=0
for n in 1 2 3 4; do sum=$((sum + n)); done
[ "$sum" -eq 10 ]
make_message() { printf '%s:%s' "$1" "$2"; }
[ "$(make_message 'one two' three)" = 'one two:three' ]
case "$quoted" in 'two words'*literal) ;; *) exit 11;; esac
/bin/echo 'disk and pipe' > /tmp/sh-output
[ "$(/bin/cat /tmp/sh-output | /bin/cat)" = 'disk and pipe' ]
[ "$(/bin/ls -1 /bin/c?t)" = /bin/cat ]
listing=$(/bin/ls -ld /etc/passwd)
case "$listing" in -rw-r--r--*root*wheel*/etc/passwd) ;; *) exit 15;; esac
/bin/ls -R /bin >/tmp/sh-list
[ -s /tmp/sh-list ]
/bin/ls -lh /etc/passwd >/tmp/sh-list
[ -s /tmp/sh-list ]
/bin/echo second >> /tmp/sh-output
[ "$(/bin/cat /tmp/sh-output)" = 'disk and pipe
second' ]
[ "$(/bin/cat <<'EOF'
here document
EOF
)" = 'here document' ]
set -- /bin/c?t
[ "$#" -eq 1 ] && [ "$1" = /bin/cat ]
set -- 'one two' three
[ "$#" -eq 2 ] && [ "$1" = 'one two' ]
seen=no
trap 'seen=yes' USR1
kill -USR1 $$
[ "$seen" = yes ]
trap - USR1
if (exit 7); then exit 12; else [ "$?" -eq 7 ]; fi
if no-such-rvm-command 2>/tmp/sh-error; then exit 13; else [ "$?" -eq 127 ]; fi
[ -s /tmp/sh-error ]
(printf background > /tmp/sh-background) &
child=$!
wait "$child"
[ "$(/bin/cat /tmp/sh-background)" = background ]
( exec /bin/echo exec-works ) > /tmp/sh-exec
[ "$(/bin/cat /tmp/sh-exec)" = exec-works ]
/bin/mkdir -p /tmp/sh-directory/child
/bin/echo removable >/tmp/sh-directory/child/file
/bin/rm -r /tmp/sh-directory
[ ! -e /tmp/sh-directory ]
/bin/mkdir /tmp/sh-directory
/bin/rmdir /tmp/sh-directory
/bin/sleep 0.01
# Force the nonlocal error-recovery path, then continue in the parent.
if ( eval 'if' ) 2>/tmp/sh-error; then exit 14; fi
printf '%s\n' 'NetBSD/RVM: native utilities and sh quoting, arithmetic, pipes, redirection, substitution, globbing, traps and wait verified'
