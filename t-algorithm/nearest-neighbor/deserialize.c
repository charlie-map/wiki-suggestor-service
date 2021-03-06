#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "deserialize.h"
#include "document-vector.h"
#include "../utils/helper.h"
#include "../serialize/serialize.h"

void hm_destroy_hashmap_body(void *hm_body) {
	return destroy_hashmap_body((document_vector_t *) hm_body);
}

int deserialize_title(char *title_reader, hashmap *doc_map, char ***ID, int *ID_len) {
	int ID_index = 0;
	FILE *index = fopen(title_reader, "r");

	if (!index) {
		printf("\033[0;31m");
		printf("\n** Error opening file **\n");
		printf("\033[0;37m");
	}

	size_t line_buffer_size = sizeof(char) * 8;
	char *line_buffer = malloc(line_buffer_size);

	int *row_num = malloc(sizeof(int));

	int line_buffer_length = 0;
	while ((line_buffer_length = getline(&line_buffer, &line_buffer_size, index)) != -1) {
		char **split_row = split_string(line_buffer, 0, row_num, "-d-r-c", delimeter_check, ": ", mirror);

		if (*row_num < 2) {
			free(split_row);
			continue;
		}

		int title_length = line_buffer_length - (strlen(split_row[0]) + strlen(split_row[*row_num - 1]));
		// now pull out the different components into a hashmap value:
		(*ID)[ID_index] = split_row[0];

		float mag = atof(split_row[*row_num - 1]);
		free(split_row[*row_num - 1]);

		char *doc_title = malloc(sizeof(char) * title_length);
		strcpy(doc_title, split_row[1]);

		free(split_row[1]);

		for (int cp_doc_title = 2; cp_doc_title < *row_num - 1; cp_doc_title++) {
			strcat(doc_title, " ");
			strcat(doc_title, split_row[cp_doc_title]);

			free(split_row[cp_doc_title]);
		}

		free(split_row);

		document_vector_t* new_doc_vector = create_document_vector((*ID)[ID_index], doc_title, mag);
		insert__hashmap(doc_map, (*ID)[ID_index], new_doc_vector, "", NULL, compareCharKey, NULL);

		ID_index++;
		*ID = resize_array(*ID, ID_len, ID_index, sizeof(char *));
	}

	free(row_num);
	free(line_buffer);

	fclose(index);

	return ID_index;
}

int destroy_split_string(char **split_string, int *split_string_len) {
	for (int delete_sub = 0; delete_sub < *split_string_len; delete_sub++) {
		free(split_string[delete_sub]);
	}

	free(split_string);

	return 0;
}

int first_occurence(char *str, char delim) {
	for (int find_delim = 0; str[find_delim]; find_delim++) {
		if (str[find_delim] == delim)
			return find_delim;
	}

	return -1;
}

char **deserialize(char *index_reader, hashmap *term_freq, hashmap *docs, int *max_words) {
	int words_index = 0; *max_words = 132;
	char **words = malloc(sizeof(char *) * *max_words);

	FILE *index = fopen(index_reader, "r");

	if (!index) {
		printf("\033[0;31m");
		printf("\n** Error opening file **\n");
		printf("\033[0;37m");
	}

	size_t line_buffer_size = sizeof(char) * 8;
	char *line_buffer = malloc(line_buffer_size);

	while (getline(&line_buffer, &line_buffer_size, index) != -1) {
		// splice bag by multi_delimeters:
		// use " " and ":" and "|" as delimeters
		int *line_sub_max = malloc(sizeof(int));
		char **line_subs = split_string(line_buffer, 0, line_sub_max, "-d-r", delimeter_check, " :,|", num_is_range);

		if (*line_sub_max < 2) {
			free(line_subs[0]);

			continue;
		}

		words[words_index] = line_subs[0];
		tf_t *tf = new_tf_t((char *) getKey__hashmap(docs, line_subs[2]));
		int colon_delim = first_occurence(line_buffer, ':');

		int full_rep_curr_len = strlen(line_buffer + sizeof(char) * (colon_delim + 1));
		tf->max_full_rep = full_rep_curr_len * 2;
		tf->full_rep_index = full_rep_curr_len;
		tf->full_rep = realloc(tf->full_rep, sizeof(char) * tf->max_full_rep);

		strcpy(tf->full_rep, line_buffer + sizeof(char) * (colon_delim + 1));

		tf->full_rep[tf->full_rep_index - 1] = '\0';
		tf->full_rep_index--;

		// ladder 9:124,1|93,1|245,2|190,1|193,2|19,1|104,1|55,3|57,2|
		// go through each document and compute normalized (using document frequency) term frequencies
		float doc_freq = atof(line_subs[1]);
		free(line_subs[1]);

		tf->doc_freq = doc_freq;

		insert__hashmap(term_freq, words[words_index], tf, "", NULL, compareCharKey, NULL);

		int read_doc_freq;
		for (read_doc_freq = 2; read_doc_freq < *line_sub_max; read_doc_freq += 2) {
			document_vector_t *doc = get__hashmap(docs, line_subs[read_doc_freq], "");
			free(line_subs[read_doc_freq]);

			// calculate term_frequency / document_frequency
			float term_frequency = atof(line_subs[read_doc_freq + 1]);
			free(line_subs[read_doc_freq + 1]);

			tf->tfs += term_frequency;
			tf->tfs_sq += term_frequency * term_frequency;

			float *normal_term_freq = malloc(sizeof(float));
			*normal_term_freq = term_frequency / doc_freq;

			insert__hashmap(doc->map, words[words_index], normal_term_freq, "", NULL, compareCharKey, NULL);
		}

		tf->standard_deviation = sqrt(tf->tfs_sq);

		words_index++;
		words = resize_array(words, max_words, words_index, sizeof(char *));

		free(line_sub_max);
		free(line_subs);
	}

	free(line_buffer);
	fclose(index);

	*max_words = words_index;

	return words;
}

int is_delim(char de, char *delims) {
	return delims[0] == de || delims[1] == de || delims[2] == de;
}

cluster_t **deserialize_cluster(char *filename, int k, hashmap *doc_map, char **word_bag, int *word_bag_len) {
	FILE *read_cluster = fopen(filename, "r");

	if (!read_cluster) {
		printf("\033[0;31m");
		printf("\n** Unable to open %s **\n", filename);
		printf("\033[0;37m");
	}

	cluster_t **cluster = malloc(sizeof(cluster_t *) * k);
	int doc_pos_index, max_doc_pos;

	size_t cluster_string_size = sizeof(char);
	char *cluster_string = malloc(cluster_string_size);

	int *line_len = malloc(sizeof(int));
	int *doc_key_len = malloc(sizeof(int));

	for (int curr_cluster = 0; curr_cluster < k; curr_cluster++) {
		cluster[curr_cluster] = malloc(sizeof(cluster_t));

		getline(&cluster_string, &cluster_string_size, read_cluster);
		char **cluster_data = split_string(cluster_string, ' ', line_len, "-d-r", is_delim, " :,", NULL);

		// first value is just the cluster mag:
		cluster[curr_cluster]->sqrt_mag = atof(cluster_data[0]);
		free(cluster_data[0]);

		// doc pos index
		doc_pos_index = atoi(cluster_data[1]);
		free(cluster_data[1]);
		max_doc_pos = doc_pos_index + 1;

		char **doc_pos = malloc(sizeof(char *) * max_doc_pos);

		cluster[curr_cluster]->doc_pos_index = doc_pos_index;
		cluster[curr_cluster]->max_doc_pos = max_doc_pos;

		hashmap *centroid = make__hashmap(0, NULL, destroy_cluster_centroid_data);

		// connect each document into the doc_pos
		// and recompute the centroid hashmap
		int read_doc;
		for (read_doc = 0; read_doc < doc_pos_index; read_doc++) {
			char *doc_key = getKey__hashmap(doc_map, cluster_data[read_doc + 2]);
			if (!doc_key)
                                continue;
			free(cluster_data[read_doc + 2]);

			doc_pos[read_doc] = doc_key;

			document_vector_t *curr_doc = get__hashmap(doc_map, doc_key, "");

			// look at all keys in the document
			char **curr_doc_keys = (char **) keys__hashmap(curr_doc->map, doc_key_len, "");

			for (int read_curr_doc_data = 0; read_curr_doc_data < *doc_key_len; read_curr_doc_data++) {
				cluster_centroid_data *data_pt = get__hashmap(centroid, curr_doc_keys[read_curr_doc_data], "");
				float curr_doc_value = *(float *) get__hashmap(curr_doc->map, curr_doc_keys[read_curr_doc_data], "");

				if (!data_pt) {
					data_pt = create_cluster_centroid_data(curr_doc_value / doc_pos_index);

					insert__hashmap(centroid, curr_doc_keys[read_curr_doc_data], data_pt, "", NULL, compareCharKey, NULL);
					continue;
				}

				data_pt->tf_idf += curr_doc_value / doc_pos_index;
				data_pt->doc_freq++;
			}

			free(curr_doc_keys);
		}

		free(cluster_data[read_doc + 2]);
		free(cluster_data);

		cluster[curr_cluster]->doc_pos = doc_pos;
		cluster[curr_cluster]->centroid = centroid;
	}

	free(line_len);
	free(doc_key_len);

	free(cluster_string);
	fclose(read_cluster);

	return cluster;
}
