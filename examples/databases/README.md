# Example PostgreSQL and SQLite databases

These examples contain the same small commerce dataset: five customers, six
products, eight orders, thirteen line items, indexes, constraints, nullable
data, timestamps, numeric values, and an `order_summary` view. The credentials
below are disposable local-demo values. Do not reuse them elsewhere.

Run all commands from the repository root.

## SQLite

Python 3 is the only prerequisite:

```sh
python3 examples/databases/setup_sqlite.py
```

In ChoscorDB, choose **Connection → New SQLite session** and use the absolute
path printed by the command (normally `build/examples/choscordb-demo.sqlite`).
To rebuild it from scratch, add `--force`. Use `--output PATH` to choose another
location.

## PostgreSQL

Docker with Compose v2 is the only prerequisite:

```sh
docker compose -f examples/databases/compose.yaml up -d --wait
```

Create a PostgreSQL connection in ChoscorDB with:

| Field | Value |
| --- | --- |
| Host | `localhost` |
| Port | `5432` |
| Database | `choscordb_demo` |
| User | `choscordb` |
| Password | `choscordb-demo-password` |
| TLS | Disable (explicit local-demo opt-out) |

If port 5432 is occupied, start the service with a different host port and enter
that port in ChoscorDB:

```sh
CHOSCORDB_DEMO_POSTGRES_PORT=55432 \
  docker compose -f examples/databases/compose.yaml up -d --wait
```

### PostgreSQL through SSH

The same Compose project starts a real OpenSSH server on `localhost:2222`.
Before ChoscorDB can connect, add this disposable server's persistent host key
to your OpenSSH `known_hosts` file. First compare the key reached through the
published port with the key inside the container; the two fingerprints must
match:

```sh
ssh-keyscan -p 2222 localhost 2>/dev/null | ssh-keygen -lf -
docker compose -f examples/databases/compose.yaml exec -T ssh \
  ssh-keygen -lf /var/lib/choscordb-ssh/ssh_host_ed25519_key.pub
```

After verifying the fingerprints, trust the fixture key:

```sh
mkdir -p ~/.ssh
chmod 700 ~/.ssh
ssh-keyscan -H -p 2222 localhost 2>/dev/null >> ~/.ssh/known_hosts
chmod 600 ~/.ssh/known_hosts
```

Create a PostgreSQL connection with these database fields to verify a normal
password-protected database through the tunnel:

| Database field | Value |
| --- | --- |
| Host | `postgres` |
| Port | `5432` |
| Database | `choscordb_demo` |
| User | `choscordb` |
| Password | `choscordb-demo-password` |
| TLS | Disable (explicit local-demo opt-out) |

Enable **Connect through SSH tunnel** and enter:

| SSH field | Value |
| --- | --- |
| SSH host | `localhost` |
| SSH port | `2222` |
| SSH username | `choscordb` |
| Authentication | Password |
| SSH password | `choscordb-ssh-password` |

To verify that ChoscorDB supports an empty database password, keep the same SSH
fields and change the database **Host** to `postgres-passwordless`. Leave the
database **Password** empty. The SSH password is still required; only the
database password is empty.

You can check the SSH login separately with standard OpenSSH:

```sh
ssh -p 2222 -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no choscordb@localhost true
```

If port 2222 is occupied, set `CHOSCORDB_DEMO_SSH_PORT` when starting Compose,
then use that port in the host-key and connection steps above.

Stop the container while preserving its data:

```sh
docker compose -f examples/databases/compose.yaml down
```

To discard the demo volume and run `postgres.sql` again on the next start:

```sh
docker compose -f examples/databases/compose.yaml down --volumes
```

This also discards the SSH host key. Before starting the rebuilt fixture,
remove its old default-port entry with
`ssh-keygen -R '[localhost]:2222'`; then verify and trust the new key as above.

## Try a query

This works in both examples:

```sql
SELECT customer_name, status, total
FROM order_summary
ORDER BY total DESC;
```
