#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#include "serialize.h"
#include "../stemmer.h"
#include "../utils/helper.h"

mutex_t newMutexLocker(void *payload) {
	mutex_t new_mutexer = { .runner = payload, .mutex = PTHREAD_MUTEX_INITIALIZER };

	return new_mutexer;
}

void destroy_hashmap_val(void *ptr) {
	free((int *) ptr);

	return;
}

void *is_block(void *hmap, char *tag) {
	return get__hashmap((hashmap *) hmap, tag, "");
}

tf_t *new_tf_t(char *ID) {
	tf_t *new_tf = malloc(sizeof(tf_t));

	new_tf->max_full_rep = 8; new_tf->full_rep_index = 0;
	new_tf->full_rep = malloc(sizeof(char) * new_tf->max_full_rep);
	memset(new_tf->full_rep, '\0', sizeof(char) * new_tf->max_full_rep);

	new_tf->curr_doc_id = ID;
	new_tf->curr_term_freq = 1;

	new_tf->tfs = 0;
	new_tf->tfs_sq = 0;

	new_tf->standard_deviation = 0;
	new_tf->doc_freq = 1;

	return new_tf;
}

void destroy_tf_t(void *tf) {
	free(((tf_t *) tf)->full_rep);

	free((tf_t *) tf);

	return;
}

int is_m(void *tf, void *extra) {
	tf_t *tt = (tf_t *) tf;

	return strcmp(tt->curr_doc_id, (char *) extra) == 0;
}

/* Update to wordbag:
	Now index_fp, title_fp, and idf_hash need mutex locking,
	so bring mutex attr with them
*/
int token_to_terms(hashmap *term_freq, mutex_t *title_fp, trie_t *stopword_trie,
	yomu_t *full_page, char **ID, document_vector_t *opt_doc, float frequency_scalar) {
	int total_bag_size = 0;

	int *page_token_check = malloc(sizeof(int));
	yomu_t **page_tokens = yomu_f.children(full_page, "page", page_token_check);

	if (*page_token_check == 0) {
		free(page_tokens);
		free(page_token_check);

		return 1;
	}

	yomu_t *page_token = page_tokens[0];

	free(page_tokens);
	free(page_token_check);

	// create title page:
	// get ID
	yomu_t **id_tokens = yomu_f.children(page_token, "id", NULL);
	yomu_t *singleton_id = yomu_f.merge(1, id_tokens);
	*ID = yomu_f.read(singleton_id, "");
	int ID_len = strlen(*ID);
	if (opt_doc)
		opt_doc->id = *ID;

	total_bag_size += ID_len - 1;

	free(id_tokens);

	// get title
	yomu_t **title_tokens = yomu_f.children(page_token, "title", NULL);
	yomu_t *singleton_title = yomu_f.merge(1, title_tokens);
	char *title = yomu_f.read(singleton_title, "");
	if (opt_doc)
		opt_doc->title = title;

	// write to title_fp
	pthread_mutex_lock(&(title_fp->mutex));
	fputs(*ID, title_fp->runner);
	fputs(":", title_fp->runner);
	fputs(title, title_fp->runner);

	free(title_tokens);

	free(title);

	// grab full page data
	int *word_number_max = malloc(sizeof(int));
	yomu_t **text_tokens = yomu_f.children(page_token, "text", NULL);
	yomu_t *text_token = yomu_f.merge(1, text_tokens);

	char *token_page_data = yomu_f.read(text_token, "-d-m", "!style");

	// create an int array so we can know the length of each char *
	int *phrase_len = malloc(sizeof(int));
	char **full_page_data = split_string(token_page_data, ' ', word_number_max, "-l", &phrase_len);

	free(token_page_data);

	int sum_of_squares = 0; // calculate sum of squares

	// loop through full_page_data and for each word:
		// check if the word is already in the hashmap, if it is:
			// add to the frequency for that word
		// if it isn't:
			// insert into hashmap at word with frequency = 0
	for (int add_hash = 0; add_hash < *word_number_max; add_hash++) {
		// check if word is in the stopword trie:
		if (trie_search(stopword_trie, full_page_data[add_hash])) {
			free(full_page_data[add_hash]);
			continue; // skip
		}

		phrase_len[add_hash] = stem(full_page_data[add_hash], 0, phrase_len[add_hash] - 1) + 1;
		full_page_data[add_hash][phrase_len[add_hash]] = 0;

		// get from word_freq_hash:
		// get char * from idf and free full_page_data
		char *prev_hash_key = (char *) getKey__hashmap(term_freq, full_page_data[add_hash]);

		if (prev_hash_key) {
			tf_t *hashmap_freq = (tf_t *) get__hashmap(term_freq, full_page_data[add_hash], "");

			if (strcmp(*ID, hashmap_freq->curr_doc_id) == 0) {
				hashmap_freq->curr_term_freq++;

				if (opt_doc) {
					float *opt_map_value = get__hashmap(opt_doc->map, full_page_data[add_hash], "");

					if (opt_map_value) {
						*opt_map_value += 1 * frequency_scalar;
					}
				}
			} else { // reset features
				hashmap_freq->tfs += hashmap_freq->curr_term_freq;
				hashmap_freq->tfs_sq += hashmap_freq->curr_term_freq * hashmap_freq->curr_term_freq;

				hashmap_freq->curr_term_freq = 1;
				hashmap_freq->curr_doc_id = *ID;

				hashmap_freq->doc_freq++;

				hashmap_freq->standard_deviation = sqrt(hashmap_freq->tfs_sq);

				// setup document_vector_t if there
				if (opt_doc) {
					float *new_opt_map_value = malloc(sizeof(float));
					*new_opt_map_value = 1 * frequency_scalar;

					insert__hashmap(opt_doc->map, full_page_data[add_hash], new_opt_map_value, "", NULL, compareCharKey, NULL);
				}
			}

			continue;
		}

		total_bag_size += phrase_len[add_hash];

		tf_t *new_tf = new_tf_t(*ID);

		insert__hashmap(term_freq, full_page_data[add_hash], new_tf, "", NULL, compareCharKey, destroyCharKey);
	}

	free(word_number_max);
	free(phrase_len);

	int *key_len = malloc(sizeof(int));
	char **keys = (char **) keys__hashmap(term_freq, key_len, "m", is_m, *ID);

	for (int count_sums = 0; count_sums < *key_len; count_sums++) {
		tf_t *m_val = (tf_t *) get__hashmap(term_freq, keys[count_sums], "");
		int key_freq = m_val->curr_term_freq;
		sum_of_squares += key_freq * key_freq;

		// update full_rep
		// ID,freq|
		int freq_len = (int) log10(key_freq) + 3;
		int length = ID_len + freq_len;

		// make sure char has enough space
		while (m_val->full_rep_index + length + 1 >= m_val->max_full_rep) {
			m_val->max_full_rep *= 2;

			m_val->full_rep = realloc(m_val->full_rep, sizeof(char) * m_val->max_full_rep);
		}

		sprintf(m_val->full_rep + sizeof(char) * m_val->full_rep_index, "%s,%d|", *ID, key_freq);
		m_val->full_rep_index += length;
		m_val->full_rep[m_val->full_rep_index] = '\0';
	}

	fprintf(title_fp->runner, " %d\n", sum_of_squares);
	pthread_mutex_unlock(&(title_fp->mutex));

	free(keys);
	free(key_len);

	free(full_page_data);

	return 0;
}

int compareFloatKey(void *v1, void *v2) {
	return *(float *) v1 < *(float *) v2;
}
