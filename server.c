#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <time.h>
#include <math.h>

// add connections to t-algorithm:
#include "t-algorithm/serialize/serialize.h"
#include "t-algorithm/serialize/vecrep.h"
#include "t-algorithm/serialize/yomu.h"
#include "t-algorithm/nearest-neighbor/heap.h"
#include "t-algorithm/nearest-neighbor/kd-tree.h"
#include "t-algorithm/nearest-neighbor/k-means.h"
#include "t-algorithm/nearest-neighbor/deserialize.h"
#include "t-algorithm/nearest-neighbor/document-vector.h"
#include "t-algorithm/utils/hashmap.h"
#include "t-algorithm/utils/helper.h"

// database
#include "databaseC/db.h"

// matrix / linalg
#include "matrix/vector.h"
#include "matrix/matrix.h"
#include "matrix/rand.h"
#include "matrix/linreg.h"

#include "teru.h"

#define HOST "*"
#define PORT "4567"

#define K 32
#define CLUSTER_THRESHOLD 2

#define RELOAD 0

#define RECOMMENDER_DOC_NUMBER 5

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
void *next_dimension(void *dimensions, void *curr_dimension);

hashmap *build_dimensions(char ***dimension_groups, void *curr_vector_group, char vector_type);

void nearest_neighbor(req_t req, res_t res);
void unique_recommend_v2(req_t req, res_t res);

int main() {
	teru_t app = teru();
	yomu_f.init();

	app_post(app, "/nn", nearest_neighbor);
	app_post(app, "/ur", unique_recommend_v2);

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

	hashmap *dimensions = build_dimensions(&doc_vector_kdtree_dimensions, term_freq_map, 'd');
	doc_vector_kdtree_start_dimension = doc_vector_kdtree_dimensions[0];
	kdtree_t *doc_vector_kdtree = kdtree_create(weight, member_extract, dimensions,
		doc_vector_kdtree_dimensions, next_dimension, distance, meta_distance);
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

	deepdestroy__hashmap(dimensions);

	trie_destroy(stopword_trie);
	mysql_close(db);
	yomu_f.close();

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

		yomu_t *token_wiki_page = yomu_f.parse(full_page);

		curr_doc = create_document_vector(curr_docID, page_title, 0);

		pthread_mutex_lock(&(term_freq->mutex));
		pthread_mutex_lock(&ID_mutex);
		token_to_terms((hashmap *) term_freq->runner, title_fp, stopword_trie, token_wiki_page, &(ID[ID_index]), curr_doc, 1);
		pthread_mutex_unlock(&(term_freq->mutex));

		ID_index++;
		ID = resize_array(ID, ID_len, ID_index, sizeof(char *));

		pthread_mutex_unlock(&ID_mutex);
		yomu_f.destroy(token_wiki_page);

		// curr doc now holds all the data which the next steps need
	}

	cluster_t *closest_cluster = find_closest_cluster(cluster, K, curr_doc);

	char **dimension_charset;
	hashmap *closest_cluster_dimensions = build_dimensions(&dimension_charset, closest_cluster, 'c');
	char *d_1 = dimension_charset[0];

	// build array of documents within the closest cluster:
	document_vector_t **cluster_docs = malloc(sizeof(document_vector_t *) * closest_cluster->doc_pos_index);
	for (int pull_cluster_doc = 0; pull_cluster_doc < closest_cluster->doc_pos_index; pull_cluster_doc++) {
		cluster_docs[pull_cluster_doc] = (document_vector_t *) get__hashmap(doc_map, closest_cluster->doc_pos[pull_cluster_doc], "");
	}

	kdtree_t *cluster_rep = kdtree_create(weight, member_extract, closest_cluster_dimensions, d_1, next_dimension, distance, meta_distance);

	// load k-d tree
	kdtree_load(cluster_rep, (void ***) cluster_docs, closest_cluster->doc_pos_index);

	// search for most relavant document:
	s_pq_t *result_documents_pq = kdtree_search(cluster_rep, d_1, curr_doc, 1, (void **) &curr_doc, 1);
	document_vector_t *return_doc = ((document_vector_t *) result_documents_pq->min->payload);

	pq_free(result_documents_pq);

	db_res_destroy(db_r);
	db_r = db_query(db, "SELECT page_name FROM page WHERE id=?", return_doc->id);

	char *page_name_tag = (char *) get__hashmap(db_r->row__data[0], "page_name", "");

	yomu_t *page_token = yomu_f.parse(page_name_tag);

	int *page_name_len = malloc(sizeof(int));
	char *page_name_pre_find = yomu_f.read(page_token, "");
	char *page_name = find_and_replace(page_name_pre_find, "&amp;", "&");

	free(page_name_pre_find);
	free(page_name_len);

	res_end(res, page_name);
	free(page_name);

	free(dimension_charset);
	free(cluster_docs);

	yomu_f.destroy(page_token);
	kdtree_destroy(cluster_rep);
	deepdestroy__hashmap(closest_cluster_dimensions);
	db_res_destroy(db_r);

	return;
}

int p_tag_match(yomu_t *t) {
	return yomu_f.hasClass(t, "mw-empty-elt") ? 0 : 1;
}

float compute_p_value(int vote, float vote_time, float focus_time, float avvt, float avft) {
	return ((vote == 3 ? 3.1 : vote) - 3) * ((vote_time == 0 ? 0.1 : vote_time) / 
		(avvt == 0 ? 0.1 : avvt)) * (focus_time / (avft == 0 ? 0.1 : avft));
}

int float_compare(void *f1, void *f2) {
	return *(float *) f1 < *(float *) f2;
}

/*
	will find highest term frequencied words using a heap
*/
char **compute_best_words(hashmap *user_doc, hashmap *user_term_freq, int *final_len) {
	// first pull out document IDs:
	int *user_doc_key_len = malloc(sizeof(int));
	char **user_doc_key = (char **) keys__hashmap(user_doc, user_doc_key_len, "");

	int *doc_key_len = malloc(sizeof(int));
	for (int user_doc_p = 0; user_doc_p < *user_doc_key_len; user_doc_p++) {

		hashmap *curr_doc = ((document_vector_t *) get__hashmap(user_doc, user_doc_key[user_doc_p], ""))->map;
		// grab words for specific document
		char **doc_key = (char **) keys__hashmap(curr_doc, doc_key_len, "");

		// check each term and its frequency and add to user_term_freq hashmap
		for (int doc_p = 0; doc_p < *doc_key_len; doc_p++) {
			float *existence = (float *) get__hashmap(user_term_freq, doc_key[doc_p], "");
			float *doc_v = (float *) get__hashmap(curr_doc, doc_key[doc_p], "");

			if (existence) {
				*existence += doc_v ? *doc_v : 0;
			} else {
				float *new_term_freq = malloc(sizeof(float));
				*new_term_freq = 1.0 + (doc_v ? *doc_v : 0);
				insert__hashmap(user_term_freq, doc_key[doc_p], new_term_freq, "", NULL, compareCharKey, NULL);
			}
		}

		free(doc_key);
	}

	// after initial insertions into user_term_freq, use
	// user_term_freq_pq to find the most important words in user_term_freq
	heap_t *user_term_freq_pq = heap_create(float_compare);

	int *user_term_freq_key_len = malloc(sizeof(int));
	char **user_term_freq_key = (char **) keys__hashmap(user_term_freq, user_term_freq_key_len, "");

	for (int doc_key_p = 0; doc_key_p < *user_term_freq_key_len; doc_key_p++) {
		float *key_freq = (float *) get__hashmap(user_term_freq, user_term_freq_key[doc_key_p], "");

		heap_push(user_term_freq_pq, user_term_freq_key[doc_key_p], key_freq);

		if (doc_key_p >= 50) {
			heap_pop(user_term_freq_pq, 0);
		}
	}

	free(user_term_freq_key_len);
	free(user_term_freq_key);

	// now slowly loop through each heap value
	int heap_len = heap_size(user_term_freq_pq);

	char **best_words = malloc(sizeof(char *) * (heap_len + 1));
	for (int best = 0; best < heap_len; best++)
		best_words[best] = (char *) heap_pop(user_term_freq_pq, 0);

	best_words[heap_len] = NULL;
	*final_len = heap_len;

	free(doc_key_len);

	free(user_doc_key);
	free(user_doc_key_len);

	heap_destroy(&user_term_freq_pq);

	return best_words;
}

void unique_recommend_v2(req_t req, res_t res) {
	// calculate user id via their uuid
	char *user_uuid = req_body(req, "uuid");

	if (!user_uuid) {
		res_end(res, "Missing UUID");
		return;
	}

	db_res *db_r = db_query(db, "SELECT id, age, avvt, avft FROM user WHERE unique_id=?", user_uuid);

	if (!db_r->row_count) {
		db_res_destroy(db_r);

		res_end(res, "No user found");
		return;
	}

	// grab user ID
	char *user_ID = (char *) get__hashmap(db_r->row__data[0], "id", "");

	// get page_id, vote, page_vote_time, and focus_time from view_vote table
	db_res *user_votes = db_query(db, "SELECT page_id, vote, page_vote_time, focus_time FROM view_vote WHERE user_id=?", user_ID);

	if (!user_votes->row_count) {
		db_res_destroy(db_r);

		res_end(res, "000"); // some internal code I just made up and will soon forget
		return;
	}

	// compute an estimated p value:
	/*
		the final desired range is between -1 (strong negative user pref) and 1 (strong positive user pref)

		the most efficient way will be to just compute some number using the following equation and then
		scale the results down based on the maximum on the negative side and positive side. For example,
		the following array of initial computed numbers:

		[-5.4, -3, -0.1, 0.25, 3.2]

		would become:

		[-1, -0.56, -0.02, 0.08, 1]

		To compute the initial numbers, the following equation will be used:

		((v1 == 3 ? 3.1 : v1) - 3) * (vt / avvt) * (ft / avft)

		where v1 = vote (1-5, defaults to 4 if they don't vote on it),
			  vt = vote time (0-?, the time at which they vote in hours, defaults to 0.1
			  	since any higher will too dramatically alter results)
			  ft = focus time (0-?, the time focused on the tab)

		SEE compute_p_value
	*/
	// compute a user vector using all of the users documents (code from unique_recommnder)
	hashmap *sub_user_doc = make__hashmap(0, NULL, NULL);
	document_vector_t **full_document_vectors = malloc(sizeof(document_vector_t *) * user_votes->row_count);
	for (int copy_document_vector = 0; copy_document_vector < user_votes->row_count; copy_document_vector++) {
		char *page_id = (char *) get__hashmap(user_votes->row__data[copy_document_vector], "page_id", "");
		full_document_vectors[copy_document_vector] = get__hashmap(doc_map, page_id, "");

		// need to serialize
		if (!full_document_vectors[copy_document_vector]) {
			db_res *db_wiki_page = db_query(db, "SELECT page_name, wiki_page FROM page WHERE id=?", page_id);
			char *page_title = (char *) get__hashmap(db_wiki_page->row__data[0], "page_name", "");
			char *page_text = (char *) get__hashmap(db_wiki_page->row__data[0], "wiki_page", "");

			int full_page_len = strlen(page_id) + strlen(page_title) + strlen(page_text) + 59;
			char *full_page = malloc(sizeof(char) * full_page_len);

			sprintf(full_page, "<page>\n<id>%s</id>\n<title>%s</title>\n<text>%s</text>\n</page>", page_id, page_title, page_text);

			yomu_t *token_wiki_page = yomu_f.parse(full_page);

			full_document_vectors[copy_document_vector] = create_document_vector(page_id, page_title, 0);

			pthread_mutex_lock(&(term_freq->mutex));
			pthread_mutex_lock(&ID_mutex);
			token_to_terms((hashmap *) term_freq->runner, title_fp, stopword_trie, token_wiki_page, &(ID[ID_index]), full_document_vectors[copy_document_vector], 1);
			pthread_mutex_unlock(&(term_freq->mutex));

			ID_index++;
			ID = resize_array(ID, ID_len, ID_index, sizeof(char *));

			pthread_mutex_unlock(&ID_mutex);
			yomu_f.destroy(token_wiki_page);
		}

		insert__hashmap(sub_user_doc, page_id, full_document_vectors[copy_document_vector], "", NULL, compareCharKey, NULL);
	}

	// otherwise we can move forward to computing a centroid
	cluster_t **user_cluster_wrapped = k_means(sub_user_doc, 1, CLUSTER_THRESHOLD);
	cluster_t *user_cluster = *user_cluster_wrapped;

	// finding more than one?
	document_vector_t *user_doc_vec = malloc(sizeof(document_vector_t));
	user_doc_vec->sqrt_mag = user_cluster->sqrt_mag;
	user_doc_vec->map = user_cluster->centroid;

	pthread_mutex_lock(&(mutex_doc_vector_kdtree->mutex));
	s_pq_t *closest_doc_vector = kdtree_search(mutex_doc_vector_kdtree->runner, doc_vector_kdtree_start_dimension, user_doc_vec, RECOMMENDER_DOC_NUMBER, (void **) full_document_vectors, user_votes->row_count);
	pthread_mutex_unlock(&(mutex_doc_vector_kdtree->mutex));

	// find top 50 words from within the user_cluster to create a ranking
	// matrix that correlates to the votes on pages
	hashmap *user_term_freq = make__hashmap(0, NULL, destroy_hashmap_float);
	int *real_user_words_len = malloc(sizeof(int));
	char **user_words_top50 = compute_best_words(sub_user_doc, user_term_freq, real_user_words_len);

	// create matrix with the term frequency of the top 50 words of each document:
	double *term_matrix = malloc(sizeof(double) * (*real_user_words_len * user_votes->row_count));
	double *resultant_y = malloc(sizeof(double) * user_votes->row_count);

	for (int doc_vec_p = 0; doc_vec_p < user_votes->row_count; doc_vec_p++) {
		int row_jump = doc_vec_p * *real_user_words_len;

		// loop through words and find the term freq to place into the term_matrix
		// also make sure to update the vote value in resultant_y
		resultant_y[doc_vec_p] = atof((char *) get__hashmap(user_votes->row__data[doc_vec_p], "vote", ""));
		resultant_y[doc_vec_p] = resultant_y[doc_vec_p] == 0 ? 1 : resultant_y[doc_vec_p] == 2 ?
			-0.1 : resultant_y[doc_vec_p] - 2;

		for (int word_p = 0; word_p < *real_user_words_len; word_p++) {
			float *doc_term_freq = (float *) get__hashmap(full_document_vectors[doc_vec_p]->map, user_words_top50[word_p], "");
			pthread_mutex_lock(&term_freq->mutex);
			float doc_freq = ((tf_t *) get__hashmap(term_freq->runner, user_words_top50[word_p], ""))->doc_freq * 1.0;
			pthread_mutex_unlock(&term_freq->mutex);

			printf("%s -- %1.3f * %1.3f - ", user_words_top50[word_p], doc_term_freq ? *doc_term_freq : 0, doc_freq);
			term_matrix[row_jump + word_p] = doc_term_freq ? (*doc_term_freq == 0 ? 0.1 * doc_freq : *doc_term_freq * doc_freq) : 0.0;
			printf("final: %1.3f\n", term_matrix[row_jump + word_p]);
		}
	}

	deepdestroy__hashmap(user_term_freq);
	// compute the weight for each term:
	for (int i = 0; i < user_votes->row_count; i++) {
		printf("%d: ", i);

		for (int j = 0; j < *real_user_words_len; j++) {
			printf("%lf - ", term_matrix[i * user_votes->row_count + j]);
		}
		printf("\n");
	}

	struct matrix *A = matrix_from_array(term_matrix, user_votes->row_count, *real_user_words_len);
	struct vector *y = vector_from_array(resultant_y, user_votes->row_count);

	free(term_matrix);
	free(resultant_y);

	db_res_destroy(user_votes);
	struct linreg *linreg_weight = linreg_fit(A, y);

	matrix_free(A);
	vector_free(y);

	int current_placed_recommended = 0;
	double *recommended_doc_vec_ranks = malloc(sizeof(double) * RECOMMENDER_DOC_NUMBER);
	memset(recommended_doc_vec_ranks, 0, sizeof(double) * RECOMMENDER_DOC_NUMBER);

	document_vector_t **recommended_doc_vec_order = malloc(sizeof(document_vector_t *) * RECOMMENDER_DOC_NUMBER);
	int set_mem = 0;
	for (s_pq_node_t *curr_doc_vec = closest_doc_vector->min; curr_doc_vec; curr_doc_vec = curr_doc_vec->next) {
		recommended_doc_vec_order[set_mem] = (document_vector_t *) curr_doc_vec->payload;
		set_mem++;
	}

	if (linreg_weight) {
		struct vector *w = linreg_weight->beta;

		// use weights in w to calculate a ranking scheme for the returned 5 best documents
		for (s_pq_node_t *curr_doc_vec = closest_doc_vector->min; curr_doc_vec;) {
			document_vector_t *cdv = (document_vector_t *) curr_doc_vec->payload;

			double *doc_vec_key = malloc(sizeof(double) * *real_user_words_len);

			for (int get_key = 0; get_key < *real_user_words_len; get_key++) {
				float *doc_term_freq = (float *) get__hashmap(cdv->map, user_words_top50[get_key], "");

				doc_vec_key[get_key] = doc_term_freq ? *doc_term_freq : 0;
			}

			struct vector *doc_v = vector_from_array(doc_vec_key, *real_user_words_len);

			double resultant_y = vector_dot_product(doc_v, w);
			vector_free(doc_v);

			// find position in recommended_doc_vec_ranks and recommended_doc_vec_order
			// for new document based on the weight
			int document_placement;
			for (document_placement = 0; document_placement < current_placed_recommended; document_placement++) {
				if (resultant_y > recommended_doc_vec_ranks[document_placement])
					break;
			}

			// based on document_placement, splice in the new document:
			double rank_buffer; document_vector_t *doc_vec_buffer;
			for (document_placement; document_placement < current_placed_recommended; document_placement++) {
				rank_buffer = recommended_doc_vec_ranks[document_placement];
				recommended_doc_vec_ranks[document_placement] = resultant_y;

				doc_vec_buffer = recommended_doc_vec_order[document_placement];
				recommended_doc_vec_order[document_placement] = cdv;
			}

			current_placed_recommended++;
		}

		linreg_free(linreg_weight);
	} else {
		current_placed_recommended = 4;
	}

	free(real_user_words_len);
	free(user_words_top50);
	free(recommended_doc_vec_ranks);

	// with sorted recommended_doc_vec_order, loop through them and
	// compute the data to send to frontend:
	int *curr_doc_titles_len = malloc(sizeof(int)), doc_titles_index = 1;
	*curr_doc_titles_len = 512;
	char *doc_titles = malloc(sizeof(char) * *curr_doc_titles_len);
	doc_titles[0] = '[';
	doc_titles[1] = '\0';

	printf("%d\n", current_placed_recommended);
	s_pq_node_t *del_pq_vec = closest_doc_vector->min;
	for (int compute_title_style = 0; compute_title_style < current_placed_recommended + 1; compute_title_style++) {
		document_vector_t *curr_doc_vec = recommended_doc_vec_order[compute_title_style];
		if (!curr_doc_vec) break;

		printf("%s\n", curr_doc_vec->id);
		db_res *db_doc = db_query(db, "SELECT wiki_page FROM page WHERE id=?", curr_doc_vec->id);

		yomu_t *token_curr_doc_vec = yomu_f.parse((char *) get__hashmap(db_doc->row__data[0], "wiki_page", ""));

		db_res_destroy(db_doc);
		// a couple of data items we can grab:
		// first image we encounter
		int *image_token_len = malloc(sizeof(int));
		yomu_t **images = yomu_f.find(token_curr_doc_vec, "img", image_token_len);
		// look for first occurrence of an image with the class "thumbimage"
		int find_thumb;
		for (find_thumb = 0; find_thumb < *image_token_len; find_thumb++)
			if (yomu_f.hasClass(images[find_thumb], "thumbimage"))
				break;
		yomu_t *get_first_image = *image_token_len == 0 ? NULL : find_thumb == *image_token_len ?
			images[0] : images[find_thumb];

		char *image_url = get_first_image ? yomu_f.attr.get(get_first_image, "src") + sizeof(char) * 2 : "";
		// first couple blips of text within first p tag in div.mw-parser-output
		free(images);
		free(image_token_len);

		int *p_tag_len = malloc(sizeof(int));
		yomu_t **p_yomu = yomu_f.find(token_curr_doc_vec, "p", p_tag_len);
		// then select p tags, (maybe look at first couple?)
		// need a way to selectively choose if skips should occur
		int choose_p_tag;
		for (choose_p_tag = 0; choose_p_tag < *p_tag_len; choose_p_tag++)
			if (!yomu_f.hasClass(p_yomu[choose_p_tag], "mw-empty-elt"))
				break;

		char *document_intro_pre;
		if (choose_p_tag < *p_tag_len)
			document_intro_pre = yomu_f.read(p_yomu[choose_p_tag], "");
		else {
			document_intro_pre = malloc(sizeof(char) * 27);
			strcpy(document_intro_pre, "Description not available.");
		}

		free(p_yomu);
		free(p_tag_len);

		char *document_intro;
		if (document_intro_pre) {
			char *document_intro_fix_quote = find_and_replace(document_intro_pre, "\"", "&ldquo;");
			char *document_intro_fix_space = find_and_replace(document_intro_fix_quote, "&nbsp;", " ");
			char *document_intro_fix_tab = find_and_replace(document_intro_fix_space, "\t", " ");
			char *document_intro_fix_newline = find_and_replace(document_intro_fix_tab, "\n", " ");
			char *document_intro_fix_backslash = find_and_replace(document_intro_fix_newline, "\\", " ");

			free(document_intro_fix_quote);
			free(document_intro_fix_space);
			free(document_intro_fix_tab);
			free(document_intro_fix_newline);

			char *en_dash = malloc(sizeof(char) * 2);
			sprintf(en_dash, "%c", 150);
			document_intro = find_and_replace(document_intro_fix_backslash, en_dash, "-");

			free(en_dash);
			free(document_intro_fix_backslash);
		} else {
			document_intro = malloc(sizeof(char) * 22);
			strcpy(document_intro, "No description found.");
		}

		free(document_intro_pre);

		int new_len = strlen(curr_doc_vec->title) + strlen(image_url) + strlen(document_intro) + 38;

		doc_titles = resize_array(doc_titles, curr_doc_titles_len, doc_titles_index + new_len, sizeof(char));
		char *remove_amp_title = find_and_replace(curr_doc_vec->title, "&amp;", "&");

		sprintf(doc_titles + sizeof(char) * doc_titles_index,
			"{\"title\":\"%s\",\"image\":\"%s\",\"descript\":\"%s\"}", remove_amp_title, image_url, document_intro);

		free(remove_amp_title);
		doc_titles_index += new_len;
		if (compute_title_style < current_placed_recommended)
			doc_titles[doc_titles_index - 1] = ',';

		doc_titles[doc_titles_index] = '\0';

		free(document_intro);
		yomu_f.destroy(token_curr_doc_vec);

		s_pq_node_t *del_pq_vec_next = del_pq_vec ? del_pq_vec->next : NULL;
		if (del_pq_vec)
			free(del_pq_vec);
		del_pq_vec = del_pq_vec_next;
	}

	doc_titles = resize_array(doc_titles, curr_doc_titles_len, doc_titles_index + 1, sizeof(char));
	strcat(doc_titles, "]");

	free(curr_doc_titles_len);
	free(full_document_vectors);
	free(closest_doc_vector);
	free(user_doc_vec);

	free(recommended_doc_vec_order);

	db_res_destroy(db_r);

	// destroy user maps
	deepdestroy__hashmap(sub_user_doc);
	destroy_cluster(user_cluster_wrapped, 1);

	printf("%s\n", doc_titles);
	res_end(res, doc_titles);

	free(doc_titles);
	return;
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
hashmap *build_dimensions(char ***dimension_groups, void *curr_vector_group, char vector_type) {
	float cluster_size = build_dimension_length(curr_vector_group, vector_type);
	hashmap *dimension_map = build_dimension_map(curr_vector_group, vector_type);

	int *key_length = malloc(sizeof(int));
	*dimension_groups = (char **) keys__hashmap(dimension_map, key_length, "");

	for (int read_best = 0; read_best < cluster_size; read_best++) {
		int best_stddev_pos = read_best;
		float best_stddev; int best_doc_freq;

		if (vector_type == 'c') {
			best_stddev = ((cluster_centroid_data *) get__hashmap(dimension_map, (*dimension_groups)[best_stddev_pos], ""))->standard_deviation;
			best_doc_freq = ((cluster_centroid_data *) get__hashmap(dimension_map, (*dimension_groups)[best_stddev_pos], ""))->doc_freq;
		} else {
			best_stddev = ((tf_t *) get__hashmap(dimension_map, (*dimension_groups)[best_stddev_pos], ""))->standard_deviation;
			best_doc_freq = ((tf_t *) get__hashmap(dimension_map, (*dimension_groups)[best_stddev_pos], ""))->doc_freq;
		}

		for (int find_best_key = read_best + 1; find_best_key < *key_length; find_best_key++) {
			float test_stddev; int test_doc_freq;

			if (vector_type == 'c') {
				test_stddev = ((cluster_centroid_data *) get__hashmap(dimension_map, (*dimension_groups)[find_best_key], ""))->standard_deviation;
				test_doc_freq = ((cluster_centroid_data *) get__hashmap(dimension_map, (*dimension_groups)[find_best_key], ""))->doc_freq;
			} else {
				test_stddev = ((tf_t *) get__hashmap(dimension_map, (*dimension_groups)[find_best_key], ""))->standard_deviation;
				test_doc_freq = ((tf_t *) get__hashmap(dimension_map, (*dimension_groups)[find_best_key], ""))->doc_freq;
			}

			// if test_stddev is greater than best_stddev, update best_stddev_pos and best_stddev
			if (test_stddev * 0.6 + test_doc_freq > best_stddev * 0.6 + best_doc_freq && test_doc_freq < cluster_size) {
				best_stddev = test_stddev;
				best_doc_freq = test_doc_freq;

				best_stddev_pos = find_best_key;
			}
		}

		// swap best_stddev_pos and read_best char * values
		char *key_buffer = (*dimension_groups)[read_best];
		(*dimension_groups)[read_best] = (*dimension_groups)[best_stddev_pos];
		(*dimension_groups)[best_stddev_pos] = key_buffer;
	}

	// then build a hashmap that points from one char * to the next, circularly linked
	hashmap *dimensions = make__hashmap(0, NULL, NULL);

	for (int insert_word = 0; insert_word < *key_length - 1; insert_word++) {
		insert__hashmap(dimensions, (*dimension_groups)[insert_word], (*dimension_groups)[insert_word + 1], "", NULL, compareCharKey, NULL);
	}

	insert__hashmap(dimensions, (*dimension_groups)[*key_length - 1], (*dimension_groups)[0], "", NULL, compareCharKey, NULL);

	free(key_length);
	return dimensions;
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
	float doc1_freq = doc1_termfreq ? *(float *) doc1_termfreq : 0;
	float doc2_freq = doc2_termfreq ? *(float *) doc2_termfreq : 0;

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

void *next_dimension(void *dimensions, void *curr_dimension) {
	// curr dimension is a char * that searches into a hashmap for the next value
	// this hashmap has each char * pointing to the next, which allows for the
	// dimensions to be based on an initial weighting from the cluster centroid
	return get__hashmap(dimensions, (char *) curr_dimension, "");
}
