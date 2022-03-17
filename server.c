#include <stdio.h>
#include <stdlib.h>

#include "teru.h"

#define HOST "localhost"
#define PORT "8888"

int main() {
	teru_t app = teru();

	int status = app_listen(HOST, PORT, app);

	return 0;
}