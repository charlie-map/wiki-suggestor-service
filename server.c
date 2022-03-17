#include <stdio.h>
#include <stdlib.h>

// add connections to t-algorithm:
#include "t-algorithm/serialize/vecrep.h"
#include "t-algorithm/nearest-neighbor/kd-tree.h"
#include "t-algorithm/nearest-neighbor/k-means.h"
#include "t-algorithm/nearest-neighbor/deserialize.h"
#include "t-algorithm/utils/hashmap.h"

#include "teru.h"

#define HOST "localhost"
#define PORT "8888"

int main() {
	teru_t app = teru();

	int status = app_listen(HOST, PORT, app);

	return 0;
}