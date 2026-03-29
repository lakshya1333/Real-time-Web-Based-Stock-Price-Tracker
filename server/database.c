#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

sqlite3 *db = NULL;

int db_init(const char *db_path) {
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    const char *sql_syslogs = 
        "CREATE TABLE IF NOT EXISTS syslogs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "event_type TEXT,"
        "message TEXT);";
        
    const char *sql_prices = 
        "CREATE TABLE IF NOT EXISTS prices ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "symbol TEXT,"
        "price REAL);";

    char *err_msg = 0;
    if (sqlite3_exec(db, sql_syslogs, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "SQL error in syslogs table: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    if (sqlite3_exec(db, sql_prices, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "SQL error in prices table: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    return 0;
}

void db_log_event(const char *event_type, const char *message) {
    if (!db) return;
    const char *sql = "INSERT INTO syslogs (event_type, message) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, event_type, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, message, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "Failed to prepare db_log_event statement\n");
    }
}

void db_record_price(const char *symbol, double price) {
    if (!db) return;
    const char *sql = "INSERT INTO prices (symbol, price) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, price);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "Failed to prepare db_record_price statement\n");
    }
}

void db_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}
