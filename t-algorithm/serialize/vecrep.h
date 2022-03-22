#ifndef __VECREP_L__
#define __VECREP_L__

#include "../utils/request.h"
#include "trie.h"
#include "serialize.h"

typedef struct SerializeObject serialize_t;
serialize_t *create_serializer(char **all_IDs, char **array_body, int *array_length,
	socket_t **sock_data, pthread_mutex_t *sock_mutex, trie_t *stopword_trie,
	mutex_t *term_freq, mutex_t *title_writer,
	int start_read_body, int end_read_body);

int http_pull_to_file();

void *data_read(void *meta_ptr);
int index_write(FILE *index_writer, char **words, int *word_len, hashmap *term_freq, int doc_number);

#endif /* __VECREP_L__ */