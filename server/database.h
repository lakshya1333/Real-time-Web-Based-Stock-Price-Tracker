#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

extern sqlite3 *db;

int db_init(const char *db_path);
void db_close(void);
void db_log_event(const char *event_type, const char *message);
void db_record_price(const char *symbol, double price);

#endif
