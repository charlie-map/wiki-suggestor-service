#ifndef __SERIALIZE_L__
#define __SERIALIZE_L__

#include "trie.h"
#include "yomu.h"
#include "../utils/hashmap.h"
#include "../nearest-neighbor/k-means.h"
#include "../nearest-neighbor/document-vector.h"

// struct for mutex locking
typedef struct MutexLocker {
	pthread_mutex_t mutex;
	void *runner;
} mutex_t;
mutex_t newMutexLocker(void *payload);

typedef struct TermFreqRep {
	int max_full_rep, full_rep_index;
	char *full_rep; // DOC_ID,FREQ|DOC_ID,FREQ|DOC_ID,FREQ

	char *curr_doc_id; // to know if we should reset curr_freq
	int curr_term_freq; // the current document to calculate

	// tfs = term frequency sum for this tf_t
	float tfs, tfs_sq;
	float standard_deviation;
	int doc_freq;
} tf_t;
tf_t *new_tf_t(char *ID);
void destroy_tf_t(void *tf);

int delimeter_check(char curr_char, char *delims);

void *is_block(void *hmap, char *tag);
int token_to_terms(hashmap *term_freq, mutex_t *title_fp, trie_t *stopword_trie,
	yomu_t *full_page, char **ID, document_vector_t *opt_doc, float frequency_scalar);

void destroy_hashmap_val(void *ptr);

#endif
