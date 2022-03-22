#ifndef __DESERIALIZE_L__
#define __DESERIALIZE_L__

#include "k-means.h"
#include "../utils/hashmap.h"

hashmap_body_t *create_hashmap_body(char *id, char *title, float mag);
void destroy_hashmap_body(hashmap_body_t *body_hash);

int deserialize_title(char *title_reader, hashmap *doc_map, char ***ID, int *ID_len);

char **deserialize(char *index_reader, hashmap *term_freq, hashmap *docs, int *max_words);

cluster_t **deserialize_cluster(char *filename, int k, hashmap *doc_map, char **word_bag, int *word_bag_len);

#endif /* __DESERIALIZE_L__ */