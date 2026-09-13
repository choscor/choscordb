PRAGMA foreign_keys = ON;
PRAGMA application_id = 0x43484F53;
PRAGMA user_version = 1;

BEGIN;

CREATE TABLE customers (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT UNIQUE,
    city TEXT NOT NULL,
    active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),
    created_at TEXT NOT NULL
);

CREATE TABLE products (
    id INTEGER PRIMARY KEY,
    sku TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    price NUMERIC NOT NULL CHECK (price >= 0),
    stock INTEGER NOT NULL CHECK (stock >= 0)
);

CREATE TABLE orders (
    id INTEGER PRIMARY KEY,
    customer_id INTEGER NOT NULL REFERENCES customers(id),
    status TEXT NOT NULL CHECK (status IN ('pending', 'paid', 'shipped', 'cancelled')),
    ordered_at TEXT NOT NULL
);

CREATE TABLE order_items (
    order_id INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
    product_id INTEGER NOT NULL REFERENCES products(id),
    quantity INTEGER NOT NULL CHECK (quantity > 0),
    unit_price NUMERIC NOT NULL CHECK (unit_price >= 0),
    PRIMARY KEY (order_id, product_id)
);

CREATE INDEX orders_customer_id_idx ON orders(customer_id);
CREATE INDEX orders_ordered_at_idx ON orders(ordered_at);

INSERT INTO customers (id, name, email, city, active, created_at) VALUES
    (1, 'Minh Nguyen', 'minh@example.test', 'Ho Chi Minh City', 1, '2026-08-02 09:15:00'),
    (2, 'Amelia Chen', 'amelia@example.test', 'Singapore', 1, '2026-08-05 14:30:00'),
    (3, 'Sam Rivera', NULL, 'Manila', 1, '2026-08-11 11:45:00'),
    (4, 'Robin Park', 'robin@example.test', 'Seoul', 0, '2026-08-19 16:20:00'),
    (5, 'Linh Tran', 'linh@example.test', 'Da Nang', 1, '2026-09-01 08:05:00');

INSERT INTO products (id, sku, name, price, stock) VALUES
    (1, 'MON-27', '27-inch monitor', 199.00, 18),
    (2, 'KEY-MECH', 'Mechanical keyboard', 89.00, 42),
    (3, 'MOU-WLS', 'Wireless mouse', 39.00, 57),
    (4, 'STD-LAP', 'Laptop stand', 69.00, 25),
    (5, 'CAM-HD', 'HD webcam', 129.00, 12),
    (6, 'HUB-USB', 'USB-C hub', 59.00, 31);

INSERT INTO orders (id, customer_id, status, ordered_at) VALUES
    (1001, 1, 'shipped', '2026-09-02 10:10:00'),
    (1002, 2, 'paid', '2026-09-03 12:25:00'),
    (1003, 3, 'shipped', '2026-09-04 08:40:00'),
    (1004, 4, 'cancelled', '2026-09-05 17:05:00'),
    (1005, 1, 'paid', '2026-09-07 09:30:00'),
    (1006, 2, 'pending', '2026-09-08 13:55:00'),
    (1007, 3, 'paid', '2026-09-09 15:15:00'),
    (1008, 5, 'pending', '2026-09-10 07:50:00');

INSERT INTO order_items (order_id, product_id, quantity, unit_price) VALUES
    (1001, 1, 1, 199.00),
    (1001, 2, 2, 89.00),
    (1002, 5, 1, 129.00),
    (1002, 6, 1, 59.00),
    (1003, 3, 1, 39.00),
    (1003, 4, 1, 69.00),
    (1004, 1, 1, 199.00),
    (1005, 3, 2, 39.00),
    (1005, 6, 1, 59.00),
    (1006, 2, 1, 89.00),
    (1007, 1, 1, 199.00),
    (1007, 5, 1, 129.00),
    (1008, 6, 1, 59.00);

CREATE VIEW order_summary AS
SELECT
    o.id AS order_id,
    c.name AS customer_name,
    o.status,
    o.ordered_at,
    SUM(oi.quantity * oi.unit_price) AS total
FROM orders AS o
JOIN customers AS c ON c.id = o.customer_id
JOIN order_items AS oi ON oi.order_id = o.id
GROUP BY o.id, c.name, o.status, o.ordered_at;

COMMIT;
