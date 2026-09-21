FROM alpine:3.22

RUN apk add --no-cache openssh-server

COPY sshd_config /etc/ssh/sshd_config
COPY entrypoint.sh /usr/local/bin/choscordb-ssh-entrypoint

EXPOSE 22

ENTRYPOINT ["/usr/local/bin/choscordb-ssh-entrypoint"]
