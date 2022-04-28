#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <math.h>

#include "helper.h"

int mirror(int t, ...) {
	return 1;
}

void *resize_array(void *arr, int *max_len, int curr_index, size_t singleton_size) {
	while (curr_index >= *max_len) {
		*max_len *= 2;

		arr = realloc(arr, singleton_size * *max_len);
	}
	
	return arr;
}

// searches through original looking for a match and then replaces
char *find_and_replace(char *original, char *match, char *replacer) {
	int *new_len = malloc(sizeof(int)), index_new = 0; *new_len = 8;
	char *new = malloc(sizeof(char) * *new_len);
	new[0] = '\0';

	int match_len = strlen(match), replacer_len = strlen(replacer);
	int index_match_check = 0;
	char *match_checker = malloc(sizeof(char) * (match_len + 1));
	memset(match_checker, '\0', sizeof(char) * (match_len + 1));
	// start searching original for matches

	for (int read_org = 0; original[read_org]; read_org++) {
		// if character equals match
		if (original[read_org] == match[index_match_check]) {
			// add to current match_checker and index_match_check
			match_checker[index_match_check] = original[read_org];

			index_match_check++;

			// if match_checker is fully filled, strcat replacer onto new and reset match_checker
			if (index_match_check == match_len) {
				new = resize_array(new, new_len, index_new + replacer_len + 1, sizeof(char));

				strcat(new, replacer);
				index_new += replacer_len;

				index_match_check = 0;
			}

			continue;
		} else {
			// add to original, but make sure index_match_check is 0
			if (!index_match_check) {
				// normal add
				new[index_new] = original[read_org];

				index_new++;
				new = resize_array(new, new_len, index_new, sizeof(char));

				new[index_new] = '\0';
			} else {
				// strcat into new
				new = resize_array(new, new_len, index_new + index_match_check + 2, sizeof(char));

				strcat(new, match_checker);
				index_new += index_match_check;

				new[index_new] = original[read_org];
				index_new++;

				new[index_new] = '\0';

				index_match_check = 0;
			}
		}
	}

	free(new_len);
	free(match_checker);

	return new;
}

// create index structure
int delimeter_check(char curr_char, char *delims) {
	for (int check_delim = 0; delims[check_delim]; check_delim++) {
		if (delims[check_delim] == curr_char)
			return 1;
	}

	return 0;
}

/*
	goes through a char * to create an array of char * that are split
	based on the delimeter character. If the string was:

	char *example = "Hi, I am happy, as of now, thanks";

	then using split_string(example, ','); would return:

	["Hi", "I am happy", "as of now", "thanks"]

	-- UPDATE: 
	char *param:
		place directly after arr_len,
		-- uses a pattern to signify more parameters
	int **minor_length:
		stores the length of each position in the returned char **
		use: "-l" to access push in minor_length
		-- only used if passed, otherwise null
		-- int ** will have same length as char **
	int (*is_delim)(char delim, char *delimeters):
		gives the options to instead have multi delimeters
		use: "-d" to have is_delim and char *delimeters passed in
	int (*is_range)(char _char):
		checks for range: default is (with chars) first one:
				number range as well is second one:
		use: "-r" to access range functions
	should_lowercase:
		used for deciding if a value should be lowercased
		use: "-c" to turn this to false (defaults to true)
*/
	int all_is_range(char _char) { return 1; }
	int char_is_range(char _char) {
		return (((int) _char >= 65 && (int) _char <= 90) || ((int) _char >= 97 && (int) _char <= 122));
	}

	int num_is_range(char _char) {
		return (((int) _char >= 65 && (int) _char <= 90) ||
			((int) _char >= 97 && (int) _char <= 122) ||
			((int) _char >= 48 && (int) _char <= 57));
	}
/*
*/
char **split_string(char *full_string, char delimeter, int *arr_len, char *extra, ...) {
	va_list param;
	va_start(param, extra);

	int **minor_length = NULL;
	int (*is_delim)(char, char *) = NULL;
	char *multi_delims = NULL;

	int (*is_range)(char _char) = char_is_range;

	int should_lowercase = 1;

	for (int check_extra = 0; extra[check_extra]; check_extra++) {
		if (extra[check_extra] != '-')
			continue;

		if (extra[check_extra + 1] == 'l')
			minor_length = va_arg(param, int **);
		else if (extra[check_extra + 1] == 'd') {
			is_delim = va_arg(param, int (*)(char, char *));
			multi_delims = va_arg(param, char *);
		} else if (extra[check_extra + 1] == 'r') {
			is_range = va_arg(param, int (*)(char));
		} else if (extra[check_extra + 1] == 'c')
			should_lowercase = 0;
	}

	int arr_index = 0;
	*arr_len = 8;
	char **arr = malloc(sizeof(char *) * *arr_len);

	if (minor_length)
		*minor_length = realloc(*minor_length, sizeof(int) * *arr_len);

	int *max_curr_sub_word = malloc(sizeof(int)), curr_sub_word_index = 0;
	*max_curr_sub_word = 8;
	arr[arr_index] = malloc(sizeof(char) * *max_curr_sub_word);
	arr[arr_index][0] = '\0';

	for (int read_string = 0; full_string[read_string]; read_string++) {
		if ((is_delim && is_delim(full_string[read_string], multi_delims)) || full_string[read_string] == delimeter) { // next phrase
			// check that current word has some length
			if (arr[arr_index][0] == '\0')
				continue;

			// quickly copy curr_sub_word_index into minor_length if minor_length is defined:
			if (minor_length) {
				(*minor_length)[arr_index] = curr_sub_word_index;
			}

			arr_index++;

			while (arr_index >= *arr_len) {
				*arr_len *= 2;
				arr = realloc(arr, sizeof(char *) * *arr_len);

				if (minor_length)
					*minor_length = realloc(*minor_length, sizeof(int *) * *arr_len);
			}

			curr_sub_word_index = 0;
			*max_curr_sub_word = 8;
			arr[arr_index] = malloc(sizeof(char) * *max_curr_sub_word);
			arr[arr_index][0] = '\0';

			continue;
		}

		// if not in range, skip:
		if (is_range && !is_range(full_string[read_string]))
			continue;

		// if a capital letter, lowercase
		if (should_lowercase && (int) full_string[read_string] <= 90 && (int) full_string[read_string] >= 65)
			full_string[read_string] = (char) ((int) full_string[read_string] + 32);

		arr[arr_index][curr_sub_word_index] = full_string[read_string];
		curr_sub_word_index++;

		arr[arr_index] = resize_array(arr[arr_index], max_curr_sub_word, curr_sub_word_index, sizeof(char));

		arr[arr_index][curr_sub_word_index] = '\0';
	}

	if (arr[arr_index][0] == '\0') { // free position
		free(arr[arr_index]);

		arr_index--;
	} else if (minor_length)
			(*minor_length)[arr_index] = curr_sub_word_index;

	*arr_len = arr_index + 1;

	free(max_curr_sub_word);

	return arr;
}

// helper function for to_bit_pattern
int convert_to_2(int num, u_char *binary_set, int binary_set_offset) {
	u_char *binary_set = malloc(sizeof(u_char) * 8);

	int reverse_counter = 7;
	for (int i = 0; i < 8; i++) {
		int power = _pow(2, reverse_counter);

		binary_set[(binary_set_offset * 8) + i] = num - power >= 0;

		num -= num - power >= 0 ? power : 0;
		reverse_counter--;
	}

	return 0;
}
/*
	Takes in `char_num` number of characters (max of 3)
	and returns the given u_char * bits that represent each character
	if the input is: "to_bit_pattern(3, 'M', 'a', 'n')", the response would be:

	[0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0]
*/
u_char *to_bit_pattern(int char_num, ...) {
	va_list char_to_bit;
	va_start(char_to_bit, char_num);

	u_char *return_bits = malloc(sizeof(u_char) * (char_num * 8));

	for (int read_char = 0; read_char < char_num; read_char++) {
		int num_char = (int) va_arg(char_to_bit, char);

		convert_to_2(num_char, return_bits, read_char);
	}

	return return_bits;
}


int _pow(int num, int pow) {
	if (num == 0)
		return 0;

	if (pow == 0)
		return 1;

	int final = num;

	for (int multi = 1; multi < pow; multi++) {
		final *= num;
	}

	return final;
}

// classic
int convert_to_10(int _0, int _1, int _2, int _3, int _4, int _5) {
	int pow0 = _pow(_0 ? 2 : 0, 5);
	int pow1 = _pow(_1 ? 2 : 0, 4);
	int pow2 = _pow(_2 ? 2 : 0, 3);
	int pow3 = _pow(_3 ? 2 : 0, 2);
	int pow4 = _pow(_4 ? 2 : 0, 1);
	int pow5 = _pow(_5 ? 2 : 0, 0);

	return pow0 + pow1 + pow2 + pow3 + pow4 + pow5;
}

// converts ASCII string to a base64 string
char *base64_encode(char *original) {
	int original_len = strlen(original_len);

	char *encoded = malloc(sizeof(char) * (ceil(original_len / 3) * 4));

	// loop through 3 characters at a time
	for (int en = 0; en < original_len; en += 3) {
		// create base2 representation
		u_char *base2 = to_bit_pattern(original_len - en > 3 ? 3 : original_len - en,
			original[en], original_len - en > 2 ? original[en + 1] : 0,
			original_len - en > 3 ? original[en + 2] : 0);

		// now pull out groups of 6 to compute the base64 value
		for (int read_base2 = original_len - en > 3 ? 2 : original_len - en - 1; read_base2 >= 0; read_base2--) {
			int base2_index = read_base2 * 6;

			encoded[en + read_base2] = (char) convert_to_10(base2[read_base2 - 5],
				base2[read_base2 - 4], base2[read_base2 - 3], base2[read_base2 - 2],
				base2[read_base2 - 1], base2[read_base2]);
		}
	}

	return encoded;
}