#!/bin/sh
set -eu

: "${CHOSCORDB_SSH_PASSWORD:?CHOSCORDB_SSH_PASSWORD is required}"

if ! id choscordb >/dev/null 2>&1; then
    adduser -D -s /bin/ash choscordb
fi
printf '%s:%s\n' choscordb "$CHOSCORDB_SSH_PASSWORD" | chpasswd

key=/var/lib/choscordb-ssh/ssh_host_ed25519_key
if [ ! -f "$key" ]; then
    mkdir -p "$(dirname "$key")"
    ssh-keygen -q -t ed25519 -N '' -f "$key"
fi

mkdir -p /run/sshd
exec /usr/sbin/sshd -D -e -f /etc/ssh/sshd_config
