#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "db.h"

void *array_rezize(void *arr, int *length, int curr, size_t s) {
	if (curr == *length) {
		*length *= 2;

		arr = realloc(arr, s * *length);
	}

	return arr;
}

int db_res_destroy(db_res *db) {
	for (int head = 0; head < db->header_count; head++)
		free(db->headers[head]);

	free(db->headers);

	for (int map = 0; map < db->row_count; map++) {
		deepdestroy__hashmap(db->row__data[map]);
	}

	free(db->row__data);

	free(db);

	return 0;
}

MYSQL *db_connect(char *server, char *user, char *password, char *database) {
	MYSQL *conn = mysql_init(NULL);

	if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}

	return conn;
}

int str_copy(char **param, int *max_length, int index, char *string, ...) {
	char curr_char = 0;

	if (!string) {
		va_list read_single_char;
		va_start(read_single_char, string);

		curr_char = va_arg(read_single_char, int);

		(*param)[index++] = (char) curr_char;
	} else {
		for (int cp_str = 0; string[cp_str]; cp_str++) {
			(*param)[index++] = string[cp_str];

			*param = array_rezize(*param, max_length, index, sizeof(char));
		}
	}

	*param = array_rezize(*param, max_length, index + 1, sizeof(char));
	(*param)[index] = '\0';

	return index;
}

db_res *db_query(MYSQL *conn, char *query, ...) {
	// compute the real query with the addition of any prepared values

	va_list query_prep_data;
	va_start(query_prep_data, query);

	int *parameter_length = malloc(sizeof(int)), parameter_index = 0;
	*parameter_length = 8;
	char *parameterized_query = malloc(sizeof(char) * *parameter_length);

	for (int read_query = 0; query[read_query]; read_query++) {
		/* continue normal pattern */
		if (query[read_query] != '?') {
			parameter_index = str_copy(&parameterized_query, parameter_length, parameter_index, NULL, query[read_query]);

			continue;
		}

		char *next_param = va_arg(query_prep_data, char *);

		parameter_index = str_copy(&parameterized_query, parameter_length, parameter_index, NULL, '"');
		parameter_index = str_copy(&parameterized_query, parameter_length, parameter_index, next_param);
		parameter_index = str_copy(&parameterized_query, parameter_length, parameter_index, NULL, '"');
	}

	if (mysql_query(conn, parameterized_query)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}

	free(parameter_length);
	free(parameterized_query);
	MYSQL_RES *res = mysql_use_result(conn);

	db_res *data = malloc(sizeof(db_res));

	// setup headers
	data->header_count = res->field_count;
	data->headers = malloc(sizeof(char *) * data->header_count);

	for (int set_header = 0; set_header < data->header_count; set_header++) {
		data->headers[set_header] = malloc(sizeof(char) * (res->fields[set_header].name_length + 1));
	
		strcpy(data->headers[set_header], res->fields[set_header].name);
	}

	// setup response data
	data->row_count = 0;
	int *data_max_length = malloc(sizeof(int)); *data_max_length = 8;
	data->row__data = malloc(sizeof(hashmap *) * *data_max_length);

	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		data->row__data[data->row_count] = make__hashmap(0, NULL, destroyCharKey);

		for (int read_header = 0; read_header < data->header_count; read_header++) {
			char *row_data = malloc(sizeof(char) * (res->lengths[read_header] + 1));
			strcpy(row_data, row[read_header]);

			insert__hashmap(data->row__data[data->row_count], data->headers[read_header], row_data, "", NULL, compareCharKey, NULL);
		}

		data->row_count++;
		data->row__data = array_rezize(data->row__data, data_max_length, data->row_count, sizeof(hashmap *));
	}

	free(data_max_length);

	mysql_free_result(res);

	return data;
}