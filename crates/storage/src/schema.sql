CREATE TABLE profile_groups(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0);
CREATE TABLE connection_profiles(id TEXT PRIMARY KEY NOT NULL, data TEXT NOT NULL);
CREATE TABLE editor_documents(id TEXT PRIMARY KEY NOT NULL, position INTEGER NOT NULL UNIQUE, data TEXT NOT NULL);
CREATE TABLE query_history(id TEXT PRIMARY KEY NOT NULL, timestamp INTEGER NOT NULL, data TEXT NOT NULL);
CREATE INDEX query_history_timestamp ON query_history(timestamp);
CREATE TABLE metadata_cache(profile_id TEXT NOT NULL, object_id TEXT NOT NULL, data TEXT NOT NULL, expires_at INTEGER NOT NULL, PRIMARY KEY(profile_id,object_id));
CREATE TABLE settings(key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);
CREATE TABLE recent_items(kind TEXT NOT NULL, item TEXT NOT NULL, last_used INTEGER NOT NULL, PRIMARY KEY(kind,item));
