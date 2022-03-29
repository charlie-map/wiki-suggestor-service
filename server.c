#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define HOST "*"
#define PORT "4567"

#define K 32
#define CLUSTER_THRESHOLD 2

#define RELOAD 0

MYSQL *db;

trie_t *stopword_trie;

int ID_index, *ID_len;
char **ID; pthread_mutex_t ID_mutex;
hashmap *doc_map;
cluster_t **cluster;

char **doc_vector_kdtree_dimensions, *doc_vector_kdtree_start_dimension;
mutex_t *mutex_doc_vector_kdtree;

int *word_bag_len;
mutex_t *term_freq;
mutex_t *title_fp;

int weight(void *map1_val, void *map2_val);
float distance(void *map1_val, void *map2_val);
float meta_distance(void *map1_body, void *map2_body);
void *member_extract(void *map, void *dimension);
void *next_dimension(void *curr_dimension);

hashmap *dimensions;
char **build_dimensions(void *curr_vector_group, char vector_type);

void nearest_neighbor(req_t req, res_t res);
void unique_recommend(req_t req, res_t res);

int main() {
	teru_t app = teru();

	app_post(app, "/nn", nearest_neighbor);
	app_post(app, "/ur", unique_recommend);

	stopword_trie = fill_stopwords("t-algorithm/serialize/stopwords.txt");

	// reset files
	if (RELOAD)
		http_pull_to_file(stopword_trie);

	hashmap *term_freq_map = make__hashmap(0, NULL, destroy_tf_t);
	doc_map = make__hashmap(0, NULL, hm_destroy_hashmap_body);

	ID_len = malloc(sizeof(int)); *ID_len = 8; ID_index = 0;
	ID = malloc(sizeof(char *) * *ID_len);
	pthread_mutex_init(&ID_mutex, NULL);
	ID_index = deserialize_title("title.txt", doc_map, &ID, ID_len);
	word_bag_len = malloc(sizeof(int));
	char **word_bag = deserialize("docbags.txt", term_freq_map, doc_map, word_bag_len);

	doc_vector_kdtree_dimensions = build_dimensions(term_freq_map, 'd');
	doc_vector_kdtree_start_dimension = doc_vector_kdtree_dimensions[0];
	kdtree_t *doc_vector_kdtree = kdtree_create(weight, member_extract, doc_vector_kdtree_dimensions, next_dimension, distance, meta_distance);
	mutex_doc_vector_kdtree = malloc(sizeof(mutex_t));
	*mutex_doc_vector_kdtree = newMutexLocker(doc_vector_kdtree);

	document_vector_t **kdtree_document_vector_arr = malloc(sizeof(document_vector_t *) * ID_index);
	for (int read_doc_vec = 0; read_doc_vec < ID_index; read_doc_vec++) {
		kdtree_document_vector_arr[read_doc_vec] = get__hashmap(doc_map, ID[read_doc_vec], "");
	}

	kdtree_load(doc_vector_kdtree, (void ***) kdtree_document_vector_arr, ID_index);

	FILE *title_writer = fopen("title.txt", "a");
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
	// db = db_connect("HOST", "USER", "PASSWORD", "DATABASE");

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

	free(kdtree_document_vector_arr);

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
		db_res *db_wiki_page = db_query(db, "SELECT page_name, wiki_page FROM page WHERE id=?", curr_docID);
		char *page_title = (char *) get__hashmap(db_wiki_page->row__data[0], "page_name", "");
		char *page_text = (char *) get__hashmap(db_wiki_page->row__data[0], "wiki_page", "");

		int full_page_len = strlen(curr_docID) + strlen(page_title) + strlen(page_text) + 59;
		char *full_page = malloc(sizeof(char) * full_page_len);

		sprintf(full_page, "<page>\n<id>%s</id>\n<title>%s</title>\n<text>%s</text>\n</page>", curr_docID, page_title, page_text);

		token_t *token_wiki_page = tokenize('s',full_page, "");

		curr_doc = create_document_vector(curr_docID, page_title, 0);

		pthread_mutex_lock(&(term_freq->mutex));
		pthread_mutex_lock(&ID_mutex);
		token_to_terms((hashmap *) term_freq->runner, title_fp, stopword_trie, token_wiki_page, &(ID[ID_index]), curr_doc);
		pthread_mutex_unlock(&(term_freq->mutex));

		ID_index++;
		ID = resize_array(ID, ID_len, ID_index, sizeof(char *));

		pthread_mutex_unlock(&ID_mutex);

		// curr doc now holds all the data which the next steps need
	}

	cluster_t *closest_cluster = find_closest_cluster(cluster, K, curr_doc);

	char **dimension_charset = build_dimensions(closest_cluster, 'c');
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
	document_vector_t *return_doc = ((document_vector_t *) kdtree_search(cluster_rep, d_1, curr_doc, 1, (void **) &curr_doc)->min->payload);

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

// expects user unique ID (uuid form) which connects to then selecting all documents
// they have viewed thus far
void unique_recommend(req_t req, res_t res) {
	// calculate user id via their uuid
	char *user_uuid = req_body(req, "uuid");

	if (!user_uuid) {
		res_end(res, "Missing UUID");
		return;
	}

	printf("%s\n", user_uuid);
	db_res *db_r = db_query(db, "SELECT id, age FROM user WHERE unique_id=?", user_uuid);

	if (!db_r->row_count) {
		db_res_destroy(db_r);

		res_end(res, "No user found");
		return;
	}

	// grab user ID
	char *buffer_user_ID = (char *) get__hashmap(db_r->row__data[0], "id", "");
	char *user_ID = malloc(sizeof(char) * (strlen(buffer_user_ID) + 1)); strcpy(user_ID, buffer_user_ID);

	db_res_destroy(db_r);

	// get page_id, vote, page_vote_time, and focus_time from view_vote table
	db_r = db_query(db, "SELECT page_id, vote, page_vote_time, focus_time FROM view_vote WHERE user_id=?", user_ID);

	if (!db_r->row_count) {
		db_res_destroy(db_r);

		res_end(res, "000"); // some internal code I just made up and will soon forget
		return;
	}

	printf("%d\n", db_r->row_count);
	hashmap *sub_user_doc = make__hashmap(0, NULL, NULL);
	document_vector_t **full_document_vectors = malloc(sizeof(document_vector_t *) * 3);
	for (int copy_document_vector = 0; copy_document_vector < db_r->row_count; copy_document_vector++) {
		char *page_id = (char *) get__hashmap(db_r->row__data[copy_document_vector], "page_id", "");
		full_document_vectors[copy_document_vector] = get__hashmap(doc_map, page_id, "");
		insert__hashmap(sub_user_doc, page_id, full_document_vectors[copy_document_vector], "", compareCharKey, NULL);
	}

	// otherwise we can move forward to computing a centroid
	cluster_t **user_cluster_wrapped = k_means(sub_user_doc, 1, CLUSTER_THRESHOLD);
	cluster_t *user_cluster = *user_cluster_wrapped;
	free(user_cluster_wrapped);

	// finding more than one?
	document_vector_t *user_doc_vec = malloc(sizeof(document_vector_t));
	user_doc_vec->sqrt_mag = user_cluster->sqrt_mag;
	user_doc_vec->map = user_cluster->centroid;

	pthread_mutex_lock(&(mutex_doc_vector_kdtree->mutex));
	s_pq_t *closest_doc_vector = kdtree_search(mutex_doc_vector_kdtree->runner, doc_vector_kdtree_start_dimension, user_doc_vec, 3, (void **) full_document_vectors);
	pthread_mutex_unlock(&(mutex_doc_vector_kdtree->mutex));

	for (s_pq_node_t *start_doc = closest_doc_vector->min; start_doc; start_doc = start_doc->next) {
		printf("%s\n", ((document_vector_t *) start_doc->payload)->title);
	}

	free(user_doc_vec);
}

// build_dimensions functionalities based on vector_type:
float build_dimension_length(void *curr_vector_group, char vector_type) {
	return log(vector_type == 'c' ? ((cluster_t *) curr_vector_group)->doc_pos_index : *word_bag_len) + 1;
}

hashmap *build_dimension_map(void *curr_vector_group, char vector_type) {
	return vector_type == 'c' ? ((cluster_t *) curr_vector_group)->centroid :
		(hashmap *) curr_vector_group;
}

// vector type:
// 'c' for cluster_ts
// 'd' for document_vectors
char **build_dimensions(void *curr_vector_group, char vector_type) {
	float cluster_size = build_dimension_length(curr_vector_group, vector_type);
	hashmap *dimension_map = build_dimension_map(curr_vector_group, vector_type);

	int *key_length = malloc(sizeof(int));
	char **keys = (char **) keys__hashmap(dimension_map, key_length, "");

	for (int read_best = 0; read_best < cluster_size; read_best++) {
		int best_stddev_pos = read_best;
		float best_stddev; int best_doc_freq;

		if (vector_type == 'c') {
			best_stddev = ((cluster_centroid_data *) get__hashmap(dimension_map, keys[best_stddev_pos], ""))->standard_deviation;
			best_doc_freq = ((cluster_centroid_data *) get__hashmap(dimension_map, keys[best_stddev_pos], ""))->doc_freq;
		} else {
			best_stddev = ((tf_t *) get__hashmap(dimension_map, keys[best_stddev_pos], ""))->standard_deviation;
			best_doc_freq = ((tf_t *) get__hashmap(dimension_map, keys[best_stddev_pos], ""))->doc_freq;
		}

		for (int find_best_key = read_best + 1; find_best_key < *key_length; find_best_key++) {
			float test_stddev; int test_doc_freq;

			if (vector_type == 'c') {
				test_stddev = ((cluster_centroid_data *) get__hashmap(dimension_map, keys[find_best_key], ""))->standard_deviation;
				test_doc_freq = ((cluster_centroid_data *) get__hashmap(dimension_map, keys[find_best_key], ""))->doc_freq;
			} else {
				test_stddev = ((tf_t *) get__hashmap(dimension_map, keys[find_best_key], ""))->standard_deviation;
				test_doc_freq = ((tf_t *) get__hashmap(dimension_map, keys[find_best_key], ""))->doc_freq;
			}

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
		return 0;
	else if (map1_val && !map2_val)
		return 1;
	else
		return *(float *) map1_val < *(float *) map2_val;
}

float distance(void *doc1_termfreq, void *doc2_termfreq) {
	float doc1_freq = *(float *) doc1_termfreq;
	float doc2_freq = *(float *) doc2_termfreq;

	float cosine_sim = (doc1_freq + doc2_freq) / ((doc1_freq * doc1_freq) + (doc2_freq * doc2_freq));

	return cosine_sim;
}

float meta_distance(void *map1_body, void *map2_body) {
	document_vector_t *doc1 = (document_vector_t *) map1_body;
	document_vector_t *doc2 = (document_vector_t *) map2_body;

	float cosine_sim = cosine_similarity(doc1->map, doc1->sqrt_mag, doc2->map, doc2->sqrt_mag);

	return cosine_sim;
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
