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

Stop the container while preserving its data:

```sh
docker compose -f examples/databases/compose.yaml down
```

To discard the demo volume and run `postgres.sql` again on the next start:

```sh
docker compose -f examples/databases/compose.yaml down --volumes
```

## Try a query

This works in both examples:

```sql
SELECT customer_name, status, total
FROM order_summary
ORDER BY total DESC;
```
