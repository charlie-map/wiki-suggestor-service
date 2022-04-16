#ifndef __YOMU_L__
#define __YOMU_L__

typedef struct Yomu yomu_t;
typedef struct Store hashmap;

typedef struct Attr {
	int (*set)(yomu_t *, char *, char *);
	char *(*get)(yomu_t *, char *);
} attr_t;

typedef struct YomuFunctions {
	// takes either a file ("file.html", etc.) or a string("<div>hello</div>")
	// and creates a yomu representation
	yomu_t *(*parse)(char *);

	// internal functions
	hashmap *forbidden_close_tags;
	int (*close_forbidden)(hashmap *, char *);

	// finder functions:
	// children takes in the current yomu level and a char * to search for matches
	// the char * can be any of the following:
	/*
		-- tag: this would be represented by just writing the tag ("div", "h1", etc.)
		-- id: access this by writing a "#" first ("#myid", etc.)
		-- class: write a "." before the search term (".myclass", etc.)

		if you start the char * with a "-" you can add some parameters like the following:
		"-m": add a match function that takes in int (*)(yomu_t *)
			-- returns 1 if it matches, and 0 if it does not
		"-n": add a maximum number of return values:
			-- add a single int parameter to the function call

		an example of using all of the above with a id parameter would look like:
		children(yomu, "-m-n#myid", children_len, int (*)(yomu_t *), int);
	*/
	// the int * parameter (called children_len in example above) will be filled
	// with the length of the returned array when the function finishes running
	// this will return an array of tokens that can be parsed
	yomu_t **(*children)(yomu_t *, char *, int *);
	// same as children function but recursively searches every sub path
	yomu_t **(*find)(yomu_t *, char *, int *);

	// merge takes in any size yomu_t **
	// and connects them into a single yomu_t *
	// the int parameter defines the number of yomu_t *'s
	// within the yomu_t **
	yomu_t *(*merge)(int, yomu_t **);

	yomu_t *(*first)(yomu_t *, char *);
	yomu_t *(*last)(yomu_t *, char *);

	// returns the parent of the given yomu
	yomu_t *(*parent)(yomu_t *);

	attr_t attr; // see above for attr_t
	// has class searches the inputted token for a class name that matches
	// the inputted char *
	int (*hasClass)(yomu_t *, char *);

	// updates the data within the yomu_t value
	// this will disconnect the lower data points unless
	// "%s" is used within the inputted char *, which will then find
	// occurrences of children that will stay within the yomu children structure
	int (*update)(yomu_t *, char *);

	// takes in a yomu and reads the data within -- the char *decides if the read
	// will also search children or just shallowly read:
	// -d -- makes yomu search the entire sub tree
	// -s -- means only shallow copying
	// ----- DEFAULTs to deep read if no parameter is given

	// also can add some more optional parameters:
	// -m -- allows for a match param add on that uses
	// 		the same pattern as .children() and .find()
	// 		where each match param is separated by a space
	char *(*read)(yomu_t *, char *, ...);

	// recursively destroys all allocated data within a yomu
	int (*destroy)(yomu_t *);

	void (*init)();
	void (*close)();
} yomu_func_t;

extern yomu_func_t yomu_f;

#endif /* __YOMU_L__ */