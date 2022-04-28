#ifndef __HELPER_L__
#define __HELPER_L__

/* MIRROR FUNCTION */
int mirror(int t, ...);

/* RESIZE FUNCTIONALITY */
void *resize_array(void *arr, int *max_len, int curr_index, size_t singleton_size);

/* DESTROY HASHMAP FLOAT */
void destroy_hashmap_float(void *v);

/* FIND AND REPLACE */
char *find_and_replace(char *original, char *match, char *replacer);

/* SPLIT STRING FUNCTIONALITY */
int all_is_range(char _char);
int char_is_range(char _char);
int num_is_range(char _char);
char **split_string(char *full_string, char delimeter, int *arr_len, char *extra, ...);

/* DELIMETER CHECK ON SPLIT STRING */
int delimeter_check(char curr_char, char *delims);

/* BASE64 ENCODER */
char *base64_encode(char *original);

#endif /* __HELPER_L__ */
