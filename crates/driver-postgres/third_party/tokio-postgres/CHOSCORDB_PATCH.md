This is `tokio-postgres` 0.7.18 from crates.io, with its upstream license files.

The local delta adds `query_raw_with_result_formats` and
`bind_with_result_formats`. They pass a per-column list of PostgreSQL Bind
result format codes (0 text, 1 binary) to the existing protocol encoder.
The ordinary APIs retain their upstream all-binary behavior. ChoscorDB uses
the new methods so an unfamiliar result type is sent through PostgreSQL's
own text output function without executing a user's SQL twice.

The delta also adds two methods that save round trips:

- `TransactionBuilder::start_and_prepare` sends `START TRANSACTION`, a setup
  statement and the Parse of the first statement together. It returns the
  open transaction and, separately, the result of the Parse.
- `Transaction::bind_and_query_portal_with_result_formats` sends Bind and the
  first Execute of a portal together, and returns the portal and the rows of
  that Execute.
