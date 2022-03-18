#ifndef __DB_L__
#define __DB_L__

#include <mysql/mysql.h>
#include "../t-algorithm/utils/hashmap.h"

typedef struct MYSQL_RG {
	int row_count;

	int header_count;
	char **headers;
	hashmap** row__data;
} db_res;

MYSQL *db_connect(char *server, char *user, char *password, char *database);

db_res *db_query(MYSQL *conn, char *query, ...);

int db_res_destroy(db_res *db);

#endif