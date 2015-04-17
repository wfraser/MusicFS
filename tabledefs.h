//
// MusicFS :: Database Table Definitions
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#pragma once

static const int DB_SCHEMA_VERSION = 1;

static const char* TABLEDEFS[] = {
    // ON DELETE RESTRICT: referenced table's rows can't be deleted if references to them exist.
    // ON DELETE CASCADE: if referenced table's rows are deleted, deletes propogate to rows that reference them.

    "PRAGMA foreign_keys = ON;",
    "CREATE TABLE IF NOT EXISTS artist ( id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS album ( id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE );",
    R"(CREATE TABLE IF NOT EXISTS track (
        id              INTEGER PRIMARY KEY,
        artist_id       INTEGER NOT NULL,
        albumartist_id  INTEGER NOT NULL,
        album_id        INTEGER NOT NULL,
        year            INTEGER NOT NULL,
        name            TEXT    NOT NULL COLLATE NOCASE,
        track           INTEGER NOT NULL,
        disc            TEXT    NOT NULL,
        FOREIGN KEY(artist_id)      REFERENCES artist(id)   ON DELETE RESTRICT,
        FOREIGN KEY(albumartist_id) REFERENCES artist(id)   ON DELETE RESTRICT,
        FOREIGN KEY(album_id)       REFERENCES album(id)    ON DELETE RESTRICT
        );)",
    R"(CREATE TABLE IF NOT EXISTS file (
        id              INTEGER PRIMARY KEY,
        track_id        INTEGER NOT NULL,
        path            TEXT NOT NULL,
        mtime           TEXT NOT NULL,
        FOREIGN KEY(track_id)       REFERENCES track(id)    ON DELETE RESTRICT
        );)",
    R"(CREATE TABLE IF NOT EXISTS path (
        id              INTEGER PRIMARY KEY,
        path            TEXT    NOT NULL UNIQUE ON CONFLICT IGNORE,
        track_id        INTEGER,
        file_id         INTEGER,
        parent_id       INTEGER,
        FOREIGN KEY(track_id)       REFERENCES track(id)    ON DELETE CASCADE,
        FOREIGN KEY(file_id)        REFERENCES file(id)     ON DELETE CASCADE,
        FOREIGN KEY(parent_id)      REFERENCES path(id)     ON DELETE CASCADE
        );)",
    R"(CREATE TABLE IF NOT EXISTS config (
        id                  INTEGER PRIMARY KEY,
        schema_version      INTEGER NOT NULL,
        backing_fs_paths    BLOB NOT NULL,
        extension_priority  BLOB,
        path_pattern        TEXT,
        aliases_conf_path   TEXT
        );)"
};
