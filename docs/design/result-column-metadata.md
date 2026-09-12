# Result column metadata

This reference defines how result schema metadata from PRD §10 reaches the native result grid.

The grid keeps each column's database type, precision, scale, timezone, and tri-state nullability for as long as the result schema is visible. The ordinary column header stays compact: it shows the column name and database type, adding precision and scale when the driver reports them and the type label does not already contain modifiers.

Hovering a horizontal header exposes a plain-text detail list containing every reported field. Unknown precision, scale, timezone, or nullability is identified as unknown instead of being inferred. The same detail is available through the header's accessible-description role. Vertical row headers retain their one-based absolute row numbers and have no schema detail.

Schema metadata is included in the result model's existing memory budget and transfer reservation. A schema that exceeds the reservation fails atomically; it cannot leave partially updated labels or metadata in the grid.

Acceptance covers signed scales, timezone labels, nullable, non-nullable, and unknown nullability, preservation through the Rust/C++ workspace boundary, tooltip and accessibility exposure, compact headers, and conservative memory accounting.
