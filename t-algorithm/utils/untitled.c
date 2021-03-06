#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <math.h>

#include "html-code-replace.h"

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

// helper function for to_bit_pattern
int convert_to_2(int num, u_char *binary_set, int binary_set_offset) {

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

	u_char *return_bits = malloc(sizeof(u_char) * 24);

	for (int read_char = 0; read_char < 3; read_char++) {
		if (read_char >= char_num) {
			for (int i = 0; i < 8; i++)
				return_bits[read_char * 8 + i] = 0;
		}

		int num_char = va_arg(char_to_bit, int);

		convert_to_2(num_char, return_bits, read_char);
	}

	return return_bits;
}

// classic
int convert_to_10(int _0, int _1, int _2, int _3, int _4, int _5) {
	int pow0 = _pow(_0 ? 2 : 0, 5);
	int pow1 = _pow(_1 ? 2 : 0, 4);
	int pow2 = _pow(_2 ? 2 : 0, 3);
	int pow3 = _pow(_3 ? 2 : 0, 2);
	int pow4 = _pow(_4 ? 2 : 0, 1);
	int pow5 = _pow(_5 ? 2 : 0, 0);

	// printf("power (%d, %d, %d, %d, %d, %d): %d\n", pow0, pow1, pow2, pow3, pow4, pow5, pow0 + pow1 + pow2 + pow3 + pow4 + pow5);
	return pow0 + pow1 + pow2 + pow3 + pow4 + pow5;
}

// converts ASCII string to a base64 string
char *base64_encode(char *original, char base64[64]) {
	int original_len = strlen(original);

	int expected_length = ceil(original_len / 3.0) * 4;
	char *encoded = malloc(sizeof(char) * (expected_length + 1));

	// loop through 3 characters at a time
	int encoded_index = 0;
	for (int en = 0; en < original_len; en += 3) {
		// create base2 representation
		u_char *base2 = to_bit_pattern(original_len - en > 3 ? 3 : original_len - en,
			(int) original[en], original_len - en >= 2 ? (int) original[en + 1] : 0,
			original_len - en >= 3 ? (int) original[en + 2] : 0);

		printf("LINE: \n");
		for (int i = 0; i < 24; i++) {
			printf("%d", base2[i]);
		}
		printf("\n");

		// now pull out groups of 6 to compute the base64 value
		for (int read_base2 = 0; read_base2 < 4; read_base2++) {
			int base2_index = read_base2 * 6 + 5;
			if (read_base2 > original_len - en)
				continue;

			//printf("\nCOMPUTE INDEX:\n");
			int final_index = convert_to_10(base2[base2_index - 5],
				base2[base2_index - 4], base2[base2_index - 3], base2[base2_index - 2],
				base2[base2_index - 1], base2[base2_index]);
			//printf("index: %d --> %c\n", final_index, base64[final_index]);

			encoded[encoded_index] = base64[final_index];
			encoded_index++;
		}

		if (original_len - en <= 2) {
			encoded[encoded_index] = '=';
			encoded_index++;
			if (original_len - en <= 1) {
				encoded[encoded_index] = '=';
				encoded_index++;
			}
		}

		free(base2);
	}

	encoded[encoded_index] = '\0';
	return encoded;
}

int test(char base64[64]) {
	// testing unstructured inputs with different character sets
	// char *hello_world = base64_encode("Hello world.", base64);
	// assert(strcmp(hello_world, "SGVsbG8gd29ybGQu") == 0);
	// free(hello_world);

	// char *ABS_abs_num = base64_encode("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", base64);
	// assert(strcmp(ABS_abs_num, "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVphYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ejAxMjM0NTY3ODk=") == 0);
	// free(ABS_abs_num);

	// extended ASCII testing
	// char *special_characters0 = base64_encode(" !\"#$%&'()*+,-./:;<=>?@", base64);
	char *special_characters1 = base64_encode("[\\]^_`{|}~??????????????????????????????????????????????", base64);
	printf("%s\n", special_characters1);
	// char *special_characters2 = base64_encode("?????????????????????????????????????????????????????", base64);
	// char *special_characters3 = base64_encode("????????????????????????????????????????????????", base64);
	// char *special_characters4 = base64_encode("????????????????????????????????????????????????", base64);
	// char *special_characters5 = base64_encode("????????????????????????????????????????????????", base64);
	// char *special_characters6 = base64_encode("????????????????", base64);

	// assert(strcmp(special_characters0, "ICEiIyQlJicoKSorLC0uLzo7PD0+P0A=") == 0);
	assert(strcmp(special_characters1, "W1xdXl9ge3x9fuKCrOKAmsaS4oCe4oCm4oCg4oChy4bigLDFoOKAucWSxb3igJjigJnigJzigJ0=") == 0);
	// assert(strcmp(special_characters2, "4oCi4oCT4oCUy5zihKLFoeKAusWTxb7FuMKhwqLCo8KkwqXCpsKnwqjCqcKqwqvCrMKuwq8=") == 0);
	// assert(strcmp(special_characters3, "wrDCscKywrPCtMK1wrbCt8K4wrnCusK7wrzCvcK+wr/DgMOBw4LDg8OEw4XDhsOH") == 0);
	// assert(strcmp(special_characters4, "w4jDicOKw4vDjMONw47Dj8OQw5HDksOTw5TDlcOWw5fDmMOZw5rDm8Ocw53DnsOf") == 0);
	// assert(strcmp(special_characters5, "w6DDocOiw6PDpMOlw6bDp8Oow6nDqsOrw6zDrcOuw6/DsMOxw7LDs8O0w7XDtsO3") == 0);
	// assert(strcmp(special_characters6, "w7jDucO6w7vDvMO9w77Dvw==") == 0);

	// free(special_characters0);
	free(special_characters1);
	// free(special_characters2);
	// free(special_characters3);
	// free(special_characters4);
	// free(special_characters5);
	// free(special_characters6);

	return 0;
}

int main() {
	// char base64[64] = {
	// 	'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S',
	// 	'T','U','V','W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','l',
	// 	'm','n','o','p','q','r','s','t','u','v','w','x','y','z','0','1','2','3','4',
	// 	'5','6','7','8','9','+','/'
	// };

	html_code_init();

	char *test_arr = malloc(sizeof(char) * 22);
	strcpy(test_arr, "The euro symbol: ???");

	printf("%s\n", test_arr);

	char *new_test_arr = html_code(test_arr);

	printf("%s\n", new_test_arr);

	free(test_arr);
	free(new_test_arr);

	html_code_close();

	return 0;
}