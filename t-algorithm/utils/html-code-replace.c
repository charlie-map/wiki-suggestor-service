#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html-code-replace.h"
#include "helper.h"

hashmap *html_code_stash;

int html_code_init() {
	html_code_stash = make__hashmap(0, printCharKey, destroyCharKey);

	FILE *fp = fopen("charToHTML.txt", "r");

	size_t buffer_page_size = sizeof(char);
	char *buffer_page = malloc(sizeof(char));
	int line_len = 0, *HTMLmatch_len = malloc(sizeof(int));
	while ((line_len = getline(&buffer_page, &buffer_page_size, fp)) != -1) {
		char **HTMLmatch = split_string(buffer_page, ' ', HTMLmatch_len, "-c-r-d", all_is_range, delimeter_check, " \n");
	
		if (*HTMLmatch_len != 2) {
			for (int f = 0; f < *HTMLmatch_len; f++)
				free(HTMLmatch[f]);

			free(HTMLmatch);

			continue;
		}

		int should_free = insert__hashmap(html_code_stash, HTMLmatch[0], HTMLmatch[1], "", printCharKey, compareCharKey, destroyCharKey);

		if (should_free)
			free(HTMLmatch[0]);

		free(HTMLmatch);
	}

	free(buffer_page);
	fclose(fp);

	free(HTMLmatch_len);

	return 0;
}

int html_code_close() {
	deepdestroy__hashmap(html_code_stash);

	return 0;
}

/* The first byte of a UTF-8 character
 * indicates how many bytes are in
 * the character, so only check that
 * FROM: https://stackoverflow.com/a/38920662/16998523
 */
int numberOfBytesInChar(unsigned char val) {
    if (val < 128) {
        return 0;
    } else if (val < 224) {
        return 2;
    } else if (val < 240) {
        return 3;
    } else {
        return 4;
    }
}

// takes in possibly extended ASCII and replaces all
// extended ASCII with the HTML codes
char *html_code(char *original) {
	int original_len = strlen(original);

	int *htmled_max = malloc(sizeof(int)), htmled_index = 0;
	*htmled_max = 32;
	char *htmled = malloc(sizeof(char) * *htmled_max);
	htmled[0] = '\0';

	// search for chars that have a value higher than 128,
	// meaning they are extended ASCII
	for (int find_ext = 0; find_ext < original_len; find_ext++) {
		int bytes = numberOfBytesInChar((unsigned char) original[find_ext]);

		if (!bytes) {
			htmled[htmled_index] = original[find_ext];
			htmled_index++;

			htmled = resize_array(htmled, htmled_max, htmled_index, sizeof(char));
			htmled[htmled_index] = '\0';

			continue;
		}

		char *search_char = malloc(sizeof(char) * (bytes + 1));

		int add_bytes;
		for (add_bytes = 0; add_bytes < bytes; add_bytes++)
			search_char[add_bytes] = original[find_ext + add_bytes];

		search_char[add_bytes] = '\0';
		char *replace_char = get__hashmap(html_code_stash, search_char, "");

		free(search_char);

		if (!replace_char)
			replace_char = "";

		htmled = resize_array(htmled, htmled_max, htmled_index + bytes, sizeof(char));

		strcat(htmled, replace_char);

		htmled_index += add_bytes;
		find_ext += add_bytes;
	}

	free(htmled_max);

	return htmled;
}