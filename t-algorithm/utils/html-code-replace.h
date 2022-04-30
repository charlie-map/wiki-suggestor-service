#ifndef __HTML_CODE_REPLACE_L__
#define __HTML_CODE_REPLACE_L__

#include "hashmap.h"

int html_code_init();
int html_code_close();

char *html_code(char *original);

extern hashmap *html_code_stash;

#endif /* __HTML_CODE_REPLACE_L__ */