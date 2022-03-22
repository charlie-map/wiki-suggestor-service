#ifndef __VECREP_L__
#define __VECREP_L__

#include "../utils/request.h"
#include "trie.h"
#include "serialize.h"

trie_t *fill_stopwords(char *stop_word_file);

int http_pull_to_file(trie_t *stopwords);

void *data_read(void *meta_ptr);
int index_write(FILE *index_writer, char **words, int *word_len, hashmap *term_freq, int doc_number);

#endif /* __VECREP_L__ */