#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "kd-tree.h"
#include "deserialize.h"
#include "document-vector.h"

typedef struct KD_Node {
	void *payload;

	struct KD_Node *left;
	struct KD_Node *right;

	// to make less complicated later:
	struct KD_Node *parent;
} kd_node_t;

kd_node_t *node_construct(kd_node_t *parent, void *payload) {
	kd_node_t *n_node = malloc(sizeof(kd_node_t));

	n_node->payload = payload;

	n_node->left = NULL;
	n_node->right = NULL;

	n_node->parent = parent;

	return n_node; // :D
}

int node_update_payload(kd_node_t *n_node, void *payload) {
	n_node->payload = payload;

	return 0;
}

int swap(void ***members, int m1, int m2) {
	void *buff = members[m1];
	members[m1] = members[m2];
	members[m2] = buff;

	return 0;
}

struct KD_Tree {
	int (*weight)(void *, void *); // -1 for "less than",
								   // 0 for same (for deletion)
								   // 1 for "greater than"

	// member extract is used in quicksorting to take in
	// void **members and void *dimension and return
	// what ever the dimension to sort on is for each member
	// within members
	void *(*member_extract)(void *, void *);

	// the array of all the possible dimension, eg.:
	// [x, y] (could be represented as [0, 1] to access another array)
	// [R, G, B]
	void *dimension;
	// will pull whatever dimension occurs next
	// for a linked list, would be the next pointer
	// make sure it reaches a NULL
	void *(*next_d)(void *);

	float (*distance)(void *, void *);
	float (*meta_distance)(void *, void *);

	kd_node_t *kd_head;
};

kdtree_t *kdtree_create(int (*weight)(void *, void *), void *(*member_extract)(void *, void *), void *dimension, void *(*next_d)(void *), float (*distance)(void *, void *), float (*meta_distance)(void *, void *)) {
	kdtree_t *new_kd = malloc(sizeof(kdtree_t));

	new_kd->weight = weight;

	new_kd->member_extract = member_extract;
	new_kd->next_d = next_d;
	new_kd->dimension = dimension;

	new_kd->distance = distance;
	new_kd->meta_distance = meta_distance;

	new_kd->kd_head = NULL;

	return new_kd;
}

// takes in the void ***members and will fully sort the data by the current dimension
int full_sort_dimension(kdtree_t *k_t, void ***members, void *dimension, int low, int high) {
	if (low >= high)
		return 0;

	int pivot_pos = low - 1;

	void *high_ptr = k_t->member_extract(members[high], dimension);
	for (int j = low; j < high; j++) {
		if (k_t->weight(k_t->member_extract(members[j], dimension), high_ptr))
			swap(members, ++pivot_pos, j);
	}

	swap(members, ++pivot_pos, high);

	// sort left and right
	full_sort_dimension(k_t, members, dimension, low, pivot_pos - 1);
	full_sort_dimension(k_t, members, dimension, pivot_pos + 1, high);

	return pivot_pos;
}

/*
	This quicksort implementation takes:

	kdtree_t *k_t: and empty kd-tree that will slowly
		fill as pivot points are found
	kd_node_t *k_node: the node we are trying to fill
		with the pivot
	void **members: the current array of all values
		we will be sorting on and inserting into k_t
	void *dimension: the current dimension we are using
	int low: the current low position in void **members
	int high: the current high position in void **members
*/

int quicksort(kdtree_t *k_t, kd_node_t *k_node, void ***members, void *dimension, int low, int high) {
	if (!k_node)
		return 0;

	full_sort_dimension(k_t, members, dimension, low, high);

	// grab middle value
	int pivot = (low + high) * 0.5;
	// shift node payload to match pivot
	node_update_payload(k_node, members[pivot]);

	// recur on both sides:
	k_node->left = low < pivot ? node_construct(k_node, NULL) : NULL;
	k_node->right = pivot < high ? node_construct(k_node, NULL) : NULL;

	dimension = k_t->next_d(dimension);
	dimension = dimension ? dimension : k_t->dimension; // nice

	// left
	quicksort(k_t, k_node->left, members, dimension, low, pivot - 1);
	// right
	quicksort(k_t, k_node->right, members, dimension, pivot + 1, high);

	return 0; // dun
}

// this loads the entire tree (and assumes k_t is empty)
int kdtree_load(kdtree_t *k_t, void ***members, int member_length) {
	k_t->kd_head = node_construct(NULL, NULL);
	quicksort(k_t, k_t->kd_head, members, k_t->dimension, 0, member_length - 1);

	return 0;
}

// searches through current tree to find position for new
// payload based on dimension
void *kdtree_insert_helper(kdtree_t *k_t, kd_node_t *k_node, void *payload, void *dimension) {
	if (!dimension) // reset to beginning
		dimension = k_t->dimension;

	// path: 0 for left subtree, 1 for right subtree
	int path = k_t->weight(k_t->member_extract(k_node->payload, dimension), k_t->member_extract(payload, dimension));

	if (path == -1 && !k_node->left) {
		k_node->left = node_construct(k_node, payload);

		return k_node->left;
	} else if (path && !k_node->right) {
		k_node->right = node_construct(k_node, payload);

		return k_node->right;
	}

	return kdtree_insert_helper(k_t, path ? k_node->right : k_node->left, payload, k_t->next_d(dimension));
}

void *kdtree_insert(kdtree_t *k_t, void *payload) {
	if (!k_t->kd_head) {
		k_t->kd_head = node_construct(NULL, payload);

		return 0;
	}

	return kdtree_insert_helper(k_t, k_t->kd_head, payload, k_t->dimension);
}

// searches for min in Dth dimension
kd_node_t *kdtree_min_helper(kdtree_t *k_t, kd_node_t *k_node, void *dimension, void *D) {
	if (!k_node)
		return NULL;

	// get left small (need either way)
	kd_node_t *left_small = kdtree_min_helper(k_t, k_node->left, k_t->next_d(dimension), D);

	if (dimension == D) // pointer comparison
		// only search on left side:
		return left_small;

	// otherwise get right_small and compare
	kd_node_t *right_small = kdtree_min_helper(k_t, k_node->right, k_t->next_d(dimension), D);

	// compare each side in the current dimension to choose smallest
	int size = left_small && right_small ?
		k_t->weight(k_t->member_extract(left_small->payload, dimension), k_t->member_extract(right_small->payload, dimension)) :
		left_small ? -1 : 1;

	// if -1, return left_small
	// if 1, return right_small
	if (size == -1)
		return right_small;
	else
		return left_small;
}

// D being the dimension to compare to
void *kdtree_min(kdtree_t *k_t, void *D) {
	if (!k_t->kd_head) return NULL;
	kd_node_t *k_node = kdtree_min_helper(k_t, k_t->kd_head, k_t->dimension, D);

	return k_node->payload;
}


int is_not_search_node(void *payload, void **payloads, int payload_len) {
	for (int check_payload = 0; check_payload < payload_len; check_payload++)
		if (payload == payloads[check_payload])
			return 0;

	return 1;
}

int insert_pq(s_pq_t *head, void *payload) {
	head->pq_size++;
	// insert into head pos:
	s_pq_node_t *min = head->min;

	if (!min) {
		head->min = malloc(sizeof(s_pq_node_t));
		head->min->payload = payload;
		head->min->next = NULL;
		return 0;
	}

	s_pq_node_t *new_pq_node = malloc(sizeof(s_pq_node_t));
	new_pq_node->payload = payload;
	new_pq_node->next = min;

	head->min = new_pq_node;

	return 0;
}

s_pq_node_t *skip(s_pq_t *head, int skip_amount) {
	s_pq_node_t *curr = head->min;

	for (int read_skip = 0; read_skip < skip_amount && curr; read_skip++) {
		curr = curr->next;
	}

	return curr;
}

int node_curr_greater(float *node_curr_values, float *meta_curr_values, int value_len) {
	for (int node_check_curr_values = 0; node_check_curr_values < value_len; node_check_curr_values++) {
		if (node_curr_values[node_check_curr_values] < meta_curr_values[node_check_curr_values])
			return node_check_curr_values;
	}

	return -1;
}
/* search:
	searches through kdtree_t *k_t to find closest related documents based on kd_payload (the search term)
	-- k_node: the current document vector we are standing at within k_t
	-- dimension: the current dimension based on the level within k_t
	-- max_document_returns: the maximum documents to return from the search

	returns a linked list of documents that were most related to kd_payload
*/
int search_kdtree_helper(kdtree_t *k_t, kd_node_t *k_node, void *dimension, void *search_payload, s_pq_t *curr_s_ll, int max_document_returns, void **current_payloads, int number_of_payloads) {
	if (curr_s_ll->pq_size == max_document_returns)
		return 0;

	if (!k_node->right && !k_node->left) {
		if (is_not_search_node(k_node->payload, current_payloads, max_document_returns))
			insert_pq(curr_s_ll, k_node->payload);

		return 0;
	} else if (!k_node->right) {
		if (is_not_search_node(k_node->right->payload, current_payloads, max_document_returns))
			insert_pq(curr_s_ll, k_node->right->payload);

		return 0;
	} else if (!k_node->left) {
		if (is_not_search_node(k_node->left->payload, current_payloads, max_document_returns))
			insert_pq(curr_s_ll, k_node->left->payload);

		return 0;
	}

	// look at current k_node and see which direction to go in:
	void *node_termfreq = k_t->member_extract(k_node->payload, dimension);
	void *search_termfreq = k_t->member_extract(search_payload, dimension);

	int weight = k_t->weight(search_termfreq, node_termfreq);

	search_kdtree_helper(k_t, weight ? k_node->right : k_node->left, k_t->next_d(dimension), search_payload, curr_s_ll, max_document_returns, current_payloads, number_of_payloads);

	// if pq isn't full, add k_node anyways
	if (curr_s_ll->pq_size < max_document_returns) {
		return insert_pq(curr_s_ll, k_node->payload);
	}

	float *meta_curr_document_distances = malloc(sizeof(float) * max_document_returns);
	float *node_curr_document_distance = malloc(sizeof(float) * max_document_returns);
	int read_curr_doc;
	for (read_curr_doc = 0; skip(curr_s_ll, read_curr_doc); read_curr_doc++) {
		meta_curr_document_distances[read_curr_doc] = k_t->meta_distance(skip(curr_s_ll, read_curr_doc)->payload, search_payload);
		node_curr_document_distance[read_curr_doc] = k_t->distance(k_t->member_extract(skip(curr_s_ll, read_curr_doc)->payload, dimension), k_t->member_extract(k_node->payload, dimension));
	}

	int worst_pos;
	if ((worst_pos = node_curr_greater(node_curr_document_distance, meta_curr_document_distances, read_curr_doc)) == -1) {
		free(meta_curr_document_distances);
		free(node_curr_document_distance);

		return 0;
	}

	// otherwise see which one should be swapped out for the value at this position
	skip(curr_s_ll, worst_pos)->payload = k_node->payload;

	free(meta_curr_document_distances);
	free(node_curr_document_distance);
	return 0;
}

s_pq_t *kdtree_search(kdtree_t *k_t, void *dimension, void *kd_payload, int max_document_returns, void **current_payloads, int number_of_payloads) {
	s_pq_t *head_node = malloc(sizeof(s_pq_t));

	head_node->min = NULL;
	head_node->pq_size = 0;

	search_kdtree_helper(k_t, k_t->kd_head, dimension, kd_payload, head_node, max_document_returns, current_payloads, number_of_payloads);

	return head_node;
}

// DFS for node
// returns the dimension of the curr_node
void *node_find(kdtree_t *k_t, kd_node_t *curr_node, kd_node_t **node_finder, void *load, void *dimension) {
	if (!curr_node)
		return NULL;

	if (curr_node->payload == load) {
		*node_finder = curr_node;

		return dimension;
	}

	void *l_d = node_find(k_t, curr_node->left, node_finder, load, k_t->next_d(dimension));
	void *r_d = node_find(k_t, curr_node->right, node_finder, load, k_t->next_d(dimension));

	return l_d ? l_d : r_d;
}

// "tbd" aka "to be deleted"
// k_node should point to whichever node is to be deleted
// if k_node == NULL, must input a third param
// of void *payload of node tbd

// returns payload of deleted node
void *kdtree_delete(kdtree_t *k_t, void *k_node, ...) {
	va_list payload;
	va_start(payload, k_node);

	void *load_tbd = !k_node ? va_arg(payload, void *) : ((kd_node_t *) k_node)->payload;

	kd_node_t **node_finder = malloc(sizeof(kd_node_t *));
	*node_finder = k_node;

	// node_D is the dimension that the node we found is in
	void *node_D = node_find(k_t, k_t->kd_head, node_finder, load_tbd, k_t->dimension);

	kd_node_t *node_tbd = *node_finder;
	free(node_finder);
		void *node_load = node_tbd->payload;

	// case 0: leaf node: delete node, done
	if (!node_tbd->left && !node_tbd->right) {
		void *node_load = node_tbd->payload;
		free(node_tbd);
	} else if (!node_tbd->left) { // has right tree
		// find min of right tree
		kd_node_t *min_node = kdtree_min_helper(k_t, node_tbd->right, node_D, node_D);

		// put min_load into node_tbd
		node_tbd->payload = min_node->payload;

		// recursively delete min_node from right subtree
		kdtree_delete(k_t, min_node);
	} else { // has just a left tree
		// recursively do similar process as right tree until a node is found

		kd_node_t *min_node = kdtree_min_helper(k_t, node_tbd->left, node_D, node_D);

		node_tbd->payload = min_node->payload;

		kdtree_delete(k_t, min_node);
	}

	return load_tbd;
}

int kdtree_destroy_helper(kd_node_t *k_node) {
	// left
	if (k_node->left)
		kdtree_destroy_helper(k_node->left);
	// right
	if (k_node->right)
		kdtree_destroy_helper(k_node->right);

	// delete data within the specific node:
	free(k_node);

	return 0;
}

// recursively go through whole tree (dimension doesn't matter)
int kdtree_destroy(kdtree_t *k_t) {
	if (k_t->kd_head)
		kdtree_destroy_helper(k_t->kd_head);

	free(k_t);

	return 0;
}

/* DEFAULTS for x,y kd-tree */
int default_weight(void *p1, void *p2) {
	return *(int *) p1 < *(int *) p2;
}

// working in x, y so void *member should be a 2D array
// with pos 0 the x value and pos 1 the y value
void *default_member_extract(void *member, void *dimension) {
	return ((int **) member)[*(int *) dimension];
}

// just for x(0) and y(1)
void *default_next_d(void *dimension) {
	(*(int *) dimension)++;

	if (*(int *) dimension == 2)
		*(int *) dimension = 0;

	return dimension;
}
