#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t-algorithm/serialize/yomu.h"

int main() {
	FILE *fp = fopen("274.dat", "r");

	int *page_len_max = malloc(sizeof(int)), page_index = 0;
	*page_len_max = 8;
	char *full_page = malloc(sizeof(char) * 8);
	full_page[0] = '\0';

	size_t buffer_page_size = sizeof(char);
	char *buffer_page = malloc(sizeof(char));
	int line_len = 0;
	while ((line_len = getline(&buffer_page, &buffer_page_size, fp)) != -1) {
		printf("%d %s\n", line_len, buffer_page);
		while ((page_index + line_len) >= *page_len_max) {
			*page_len_max *= 2;
			full_page = realloc(full_page, sizeof(char) * *page_len_max);
		}

		page_index += line_len;
		strcat(full_page, buffer_page);
	}

	printf("%s\n", full_page);

	free(buffer_page);
	fclose(fp);

	free(page_len_max);

	yomu_t *yomu_test = yomu_f.parse(full_page);

	char *yomu_test_data = yomu_f.read(yomu_test, "");
	printf("%s\n", yomu_test_data);

	free(yomu_test_data);

	yomu_f.destroy(yomu_test);
	free(full_page);

	return 0;
}