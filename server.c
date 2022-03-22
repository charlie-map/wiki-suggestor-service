#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>

// add connections to t-algorithm:
#include "t-algorithm/serialize/serialize.h"
#include "t-algorithm/serialize/vecrep.h"
#include "t-algorithm/serialize/token.h"
#include "t-algorithm/nearest-neighbor/kd-tree.h"
#include "t-algorithm/nearest-neighbor/k-means.h"
#include "t-algorithm/nearest-neighbor/deserialize.h"
#include "t-algorithm/nearest-neighbor/document-vector.h"
#include "t-algorithm/utils/hashmap.h"
#include "t-algorithm/utils/helper.h"

// database
#include "databaseC/db.h"

#include "teru.h"

#define HOST "localhost"
#define PORT "8888"

#define K 32
#define CLUSTER_THRESHOLD 2

#define RELOAD 0

MYSQL *db;

trie_t *stopword_trie;

int ID_index, *ID_len;
char **ID; pthread_mutex_t ID_mutex;
hashmap *doc_map;
cluster_t **cluster;

mutex_t *term_freq;
mutex_t *title_fp;

int weight(void *map1_val, void *map2_val);
float distance(void *map1_val, void *map2_val);
float meta_distance(void *map1_body, void *map2_body);
void *member_extract(void *map, void *dimension);
void *next_dimension(void *curr_dimension);

hashmap *dimensions;
char **build_dimensions(cluster_t *curr_cluster);

void nearest_neighbor(req_t req, res_t res) {
	// grab unique ID from body
	char *unique_id = (char *) req_body(req, "unique-id");

	// take unique_id and search for the internal id from the database
	db_res *db_r = db_query(db, "SELECT id FROM page WHERE unique_id=?", unique_id);

	if (!db_r->row_count) {
		db_res_destroy(db_r);

		res_end(res, "No page");
		return;
	}

	char *curr_docID = (char *) get__hashmap(db_r->row__data[0], "id", "");
	document_vector_t *curr_doc = get__hashmap(doc_map, curr_docID, "");

	// document exists, but has not been serialized into the current document map
	if (!curr_doc) {
		db_res *db_wiki_page = db_query(db, "SELECT wiki_page FROM page WHERE id=?", curr_docID);

		token_t *token_wiki_page = tokenize('s', (char *) get__hashmap(db_wiki_page->row__data[0], "wiki_page", ""), "");

		curr_doc = create_hashmap_body(NULL, NULL, 0);

		pthread_mutex_lock(&(term_freq->mutex));
		pthread_mutex_lock(&ID_mutex);
		word_bag((hashmap *) term_freq->runner, title_fp, stopword_trie, token_wiki_page, &(ID[ID_index]), curr_doc);
		pthread_mutex_unlock(&(term_freq->mutex));

		ID_index++;
		ID = resize_array(ID, ID_len, ID_index, sizeof(char *));

		pthread_mutex_unlock(&ID_mutex);

		// curr doc now holds all the data which the next steps need
	}

	cluster_t *closest_cluster = find_closest_cluster(cluster, K, curr_doc);

	char **dimension_charset = build_dimensions(closest_cluster);
	char *d_1 = dimension_charset[0];

	// build array of documents within the closest cluster:
	document_vector_t **cluster_docs = malloc(sizeof(document_vector_t *) * closest_cluster->doc_pos_index);
	for (int pull_cluster_doc = 0; pull_cluster_doc < closest_cluster->doc_pos_index; pull_cluster_doc++) {
		cluster_docs[pull_cluster_doc] = (document_vector_t *) get__hashmap(doc_map, closest_cluster->doc_pos[pull_cluster_doc], "");
	}

	kdtree_t *cluster_rep = kdtree_create(weight, member_extract, d_1, next_dimension, distance, meta_distance);

	// load k-d tree
	kdtree_load(cluster_rep, (void ***) cluster_docs, closest_cluster->doc_pos_index);

	// search for most relavant document:
	document_vector_t *return_doc = kdtree_search(cluster_rep, d_1, curr_doc);

	db_res_destroy(db_r);
	db_r = db_query(db, "SELECT page_name FROM page WHERE id=?", return_doc->id);

	char *page_name_tag = (char *) get__hashmap(db_r->row__data[0], "page_name", "");

	token_t *page_token = tokenize('s', page_name_tag, "");

	int *page_name_len = malloc(sizeof(int));
	char *page_name = token_read_all_data(page_token, page_name_len, NULL, NULL);

	free(page_name_len);

	res_end(res, page_name);
	free(page_name);

	free(dimension_charset);
	free(cluster_docs);

	destroy_token(page_token);
	kdtree_destroy(cluster_rep);
	db_res_destroy(db_r);

	return;
}

int main() {
	teru_t app = teru();

	app_post(app, "/nn", nearest_neighbor);

	stopword_trie = fill_stopwords("t-algorithm/serialize/stopwords.txt");

	// reset files
	if (RELOAD)
		http_pull_to_file(stopword_trie);
	
	hashmap *term_freq_map = make__hashmap(0, NULL, destroy_tf_t);

	ID_len = malloc(sizeof(int)); *ID_len = 8; ID_index = 0;
	ID = malloc(sizeof(char *) * *ID_len);
	pthread_mutex_init(&ID_mutex, NULL);
	ID_index = deserialize_title("title.txt", doc_map, &ID, ID_len);
	int *word_bag_len = malloc(sizeof(int));
	char **word_bag = deserialize("docbags.txt", term_freq_map, doc_map, word_bag_len);

	FILE *title_writer = fopen("title.txt", "w");
	fseek(title_writer, 0, SEEK_END);
	if (!title_writer) {
		printf("\033[0;31m");
		printf("\n** Error opening file **\n");
		printf("\033[0;37m");
	}
	title_fp = malloc(sizeof(mutex_t)); *title_fp = newMutexLocker(title_writer);
	term_freq = malloc(sizeof(mutex_t)); *term_freq = newMutexLocker(term_freq_map);

	if (RELOAD) {
		cluster = cluster = k_means(doc_map, K, CLUSTER_THRESHOLD);
		cluster_to_file(cluster, K, "cluster.txt");
	} else {
		cluster = deserialize_cluster("cluster.txt", K, doc_map, word_bag, word_bag_len);
	}

	// setup database:
	// db = db_connect("SERVER", "USERNAME", "PASSWORD", "DATABASE-NAME");

	int status = app_listen(HOST, PORT, app);

	// reserialize documents
	FILE *index_writer = fopen("docbags.txt", "w");

	if (!index_writer) {
		printf("\033[0;31m");
		printf("\n** Error opening file **\n");
		printf("\033[0;37m");
	}

	index_write(index_writer, word_bag, word_bag_len, (hashmap *) term_freq->runner, *ID_len);
	fclose(index_writer);

	destroy_cluster(cluster, K);
	deepdestroy__hashmap(doc_map);
	deepdestroy__hashmap((hashmap *) term_freq->runner);

	for (int free_words = 0; free_words < *word_bag_len; free_words++) {
		free(word_bag[free_words]);
	}

	free(word_bag_len);
	free(word_bag);

	// mysql_close(db);

	return 0;
}

char **build_dimensions(cluster_t *curr_cluster) {
	float cluster_size = log(curr_cluster->doc_pos_index) + 1;

	int *key_length = malloc(sizeof(int));
	char **keys = (char **) keys__hashmap(curr_cluster->centroid, key_length, "");

	for (int read_best = 0; read_best < cluster_size; read_best++) {

		int best_stddev_pos = read_best;
		float best_stddev = ((cluster_centroid_data *) get__hashmap(curr_cluster->centroid, keys[best_stddev_pos], ""))->standard_deviation;
		int best_doc_freq = ((cluster_centroid_data *) get__hashmap(curr_cluster->centroid, keys[best_stddev_pos], ""))->doc_freq;
		for (int find_best_key = read_best + 1; find_best_key < *key_length; find_best_key++) {

			float test_stddev = ((cluster_centroid_data *) get__hashmap(curr_cluster->centroid, keys[find_best_key], ""))->standard_deviation;
			int test_doc_freq = ((cluster_centroid_data *) get__hashmap(curr_cluster->centroid, keys[find_best_key], ""))->doc_freq;

			// if test_stddev is greater than best_stddev, update best_stddev_pos and best_stddev
			if (test_stddev * 0.6 + test_doc_freq > best_stddev * 0.6 + best_doc_freq && test_doc_freq < cluster_size) {
				best_stddev = test_stddev;
				best_doc_freq = test_doc_freq;

				best_stddev_pos = find_best_key;
			}
		}

		// swap best_stddev_pos and read_best char * values
		char *key_buffer = keys[read_best];
		keys[read_best] = keys[best_stddev_pos];
		keys[best_stddev_pos] = key_buffer;
	}

	// then build a hashmap that points from one char * to the next, circularly linked
	dimensions = make__hashmap(0, NULL, NULL);

	for (int insert_word = 0; insert_word < *key_length - 1; insert_word++) {
		insert__hashmap(dimensions, keys[insert_word], keys[insert_word + 1], "", compareCharKey, NULL);
	}

	insert__hashmap(dimensions, keys[*key_length - 1], keys[0], "", compareCharKey, NULL);

	free(key_length);
	return keys;
}

int weight(void *map1_val, void *map2_val) {
	if ((!map1_val && !map2_val) || (!map1_val && map2_val))
		return 1;
	else if (map1_val && !map2_val)
		return 0;
	else
		return *(float *) map1_val < *(float *) map2_val;
}

float distance(void *map1_val, void *map2_val) {
	float val1 = map1_val ? *(float *) map1_val : 0;
	float val2 = map2_val ? *(float *) map2_val : 0;

	float d = val1 - val2;

	return d * d;
}

float meta_distance(void *map1_body, void *map2_body) {
	float mag1 = map1_body ? ((document_vector_t *) map1_body)->mag : 0;
	float mag2 = map2_body ? ((document_vector_t *) map2_body)->mag : 0;

	float d = mag1 - mag2;

	return d * d;
}

void *member_extract(void *map_body, void *dimension) {
	return get__hashmap(((document_vector_t *) map_body)->map, (char *) dimension, "");
}

void *next_dimension(void *curr_dimension) {
	// curr dimension is a char * that searches into a hashmap for the next value
	// this hashmap has each char * pointing to the next, which allows for the
	// dimensions to be based on an initial weighting from the cluster centroid
	return get__hashmap(dimensions, (char *) curr_dimension, "");
}
