/*
 *  'ajduk' specific functionality, examples for low memory techniques
 */

#ifdef DUK_CMDLINE_AJSHEAP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ajs.h"
#include "ajs_heap.h"

/*
 *  Helpers
 */

static void safe_print_chars(const char *p, duk_size_t len, int until_nul) {
	duk_size_t i;

	printf("\"");
	for (i = 0; i < len; i++) {
		unsigned char x = (unsigned char) p[i];
		if (until_nul && x == 0U) {
			break;
		}
		if (x < 0x20 || x >= 0x7e || x == '"' || x == '\'' || x == '\\') {
			printf("\\x%02x", (int) x);
		} else {
			printf("%c", (char) x);
		}
	}
	printf("\"");
}

/*
 *  Heap initialization when using AllJoyn.js pool allocator (without any
 *  other AllJoyn.js integration).  This serves as an example of how to
 *  integrate Duktape with a pool allocator and is useful for low memory
 *  testing.
 *
 *  The pool sizes are not optimized here.  The sizes are chosen so that
 *  you can look at the high water mark (hwm) and use counts (use) and see
 *  how much allocations are needed for each pool size.  To optimize pool
 *  sizes more accurately, you can use --alloc-logging and inspect the memory
 *  allocation log which provides exact byte counts etc.
 *
 *  https://git.allseenalliance.org/cgit/core/alljoyn-js.git
 *  https://git.allseenalliance.org/cgit/core/alljoyn-js.git/tree/ajs.c
 */

static const AJS_HeapConfig ajsheap_config[] = {
	{ 8,      10,   AJS_POOL_BORROW,  0 },
	{ 12,     10,   AJS_POOL_BORROW,  0 },
	{ 16,     200,  AJS_POOL_BORROW,  0 },
	{ 20,     400,  AJS_POOL_BORROW,  0 },
	{ 24,     400,  AJS_POOL_BORROW,  0 },
	{ 28,     200,  AJS_POOL_BORROW,  0 },
	{ 32,     200,  AJS_POOL_BORROW,  0 },
	{ 40,     200,  AJS_POOL_BORROW,  0 },
	{ 48,     50,   AJS_POOL_BORROW,  0 },
	{ 52,     50,   AJS_POOL_BORROW,  0 },
	{ 56,     50,   AJS_POOL_BORROW,  0 },
	{ 60,     50,   AJS_POOL_BORROW,  0 },
	{ 64,     50,   AJS_POOL_BORROW,  0 },
	{ 128,    80,   AJS_POOL_BORROW,  0 },
	{ 256,    16,   AJS_POOL_BORROW,  0 },
	{ 512,    16,   AJS_POOL_BORROW,  0 },
	{ 1024,   6,    AJS_POOL_BORROW,  0 },
	{ 1352,   1,    AJS_POOL_BORROW,  0 },  /* duk_heap, with heap ptr compression */
	{ 2048,   5,    AJS_POOL_BORROW,  0 },
	{ 4096,   3,    0,                0 },
	{ 8192,   3,    0,                0 },
	{ 16384,  1,    0,                0 },
	{ 32768,  1,    0,                0 }
};

uint8_t *ajsheap_ram = NULL;

void ajsheap_init(void) {
	size_t heap_sz[1];
	uint8_t *heap_array[1];
	uint8_t num_pools, i;
	AJ_Status ret;

	num_pools = (uint8_t) (sizeof(ajsheap_config) / sizeof(AJS_HeapConfig));
	heap_sz[0] = AJS_HeapRequired(ajsheap_config,  /* heapConfig */
	                              num_pools,       /* numPools */
	                              0);              /* heapNum */
	ajsheap_ram = (uint8_t *) malloc(heap_sz[0]);
	if (ajsheap_ram == NULL) {
		fprintf(stderr, "Failed to allocate AJS heap\n");
		fflush(stderr);
		exit(1);
	}
	heap_array[0] = ajsheap_ram;

	fprintf(stderr, "Allocated AJS heap of %ld bytes, pools:", (long) heap_sz[0]);
	for (i = 0; i < num_pools; i++) {
		fprintf(stderr, " (sz:%ld,num:%ld,brw:%ld,idx:%ld)",
		        (long) ajsheap_config[i].size, (long) ajsheap_config[i].entries,
		        (long) ajsheap_config[i].borrow, (long) ajsheap_config[i].heapIndex);
	}
	fprintf(stderr, "\n");
	fflush(stderr);

	ret = AJS_HeapInit((void **) heap_array,   /* heap */
	                   (size_t *) heap_sz,     /* heapSz */
	                   ajsheap_config,         /* heapConfig */
	                   num_pools,              /* numPools */
	                   1);                     /* numHeaps */
	fprintf(stderr, "AJS_HeapInit() -> %ld\n", (long) ret);
	fflush(stderr);
}

/* AjsHeap.dump(), allows Ecmascript code to dump heap status at suitable
 * points.
 */
duk_ret_t ajsheap_dump_binding(duk_context *ctx) {
	AJS_HeapDump();
	fflush(stdout);
	return 0;
}

void ajsheap_dump(void) {
	AJS_HeapDump();
	fflush(stdout);
}

void ajsheap_register(duk_context *ctx) {
	duk_push_object(ctx);
	duk_push_c_function(ctx, ajsheap_dump_binding, 0);
	duk_put_prop_string(ctx, -2, "dump");
	duk_put_global_string(ctx, "AjsHeap");
}

/*
 *  Example pointer compression functions.
 *
 *  'base' is chosen so that no non-NULL pointer results in a zero result
 *  which is reserved for NULL pointers.
 */

duk_uint16_t ajsheap_enc16(void *p) {
	duk_uint32_t ret;
	char *base = (char *) ajsheap_ram - 4;

	if (p == NULL) {
		ret = 0;
	} else {
		ret = (duk_uint32_t) (((char *) p - base) >> 2);
	}
#if 0
	printf("ajsheap_enc16: %p -> %u\n", p, (unsigned int) ret);
#endif
	if (ret > 0xffffUL) {
		fprintf(stderr, "Failed to compress pointer\n");
		fflush(stderr);
		abort();
	}
	return (duk_uint16_t) ret;
}
void *ajsheap_dec16(duk_uint16_t x) {
	void *ret;
	char *base = (char *) ajsheap_ram - 4;

	if (x == 0) {
		ret = NULL;
	} else {
		ret = (void *) (base + (((duk_uint32_t) x) << 2));
	}
#if 0
	printf("ajsheap_dec16: %u -> %p\n", (unsigned int) x, ret);
#endif
	return ret;
}

/*
 *  Simplified example of an external strings strategy where incoming strings
 *  are written sequentially into a fixed, memory mapped flash area.
 *
 *  The example first scans if the string is already in the flash (which may
 *  happen if the same string is interned multiple times), then adds it to
 *  flash if there is space.
 *
 *  This example is too slow to be used in a real world application: there
 *  should be e.g. a hash table to quickly check for strings that are already
 *  present in the string data (similarly to how string interning works in
 *  Duktape itself).
 */

static uint8_t ajsheap_strdata[65536];
static size_t ajsheap_strdata_used = 0;

const void *ajsheap_extstr_check_1(const void *ptr, duk_size_t len) {
	uint8_t *p, *p_end;
	uint8_t initial;
	uint8_t *ret;
	size_t left;

	(void) safe_print_chars;  /* potentially unused */

	if (len <= 3) {
		/* It's not worth it to make very small strings external, as
		 * they would take the same space anyway.  Also avoids zero
		 * length degenerate case.
		 */
		return NULL;
	}

	/*
	 *  Check if we already have the string.  Be careful to compare for
	 *  NUL terminator too, it is NOT present in 'ptr'.  This algorithm
	 *  is too simplistic and way too slow for actual use.
	 */

	initial = ((const uint8_t *) ptr)[0];
	for (p = ajsheap_strdata, p_end = p + ajsheap_strdata_used; p != p_end; p++) {
		if (*p != initial) {
			continue;
		}
		left = (size_t) (p_end - p);
		if (left >= len + 1 &&
		    memcmp(p, ptr, len) == 0 &&
		    p[len] == 0) {
			ret = p;
#if 0
			printf("ajsheap_extstr_check_1: ptr=%p, len=%ld ",
			       (void *) ptr, (long) len);
			safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
			printf(" -> existing %p (used=%ld)\n",
			       (void *) ret, (long) ajsheap_strdata_used);
#endif
			return ret;
		}
	}

	/*
	 *  Not present yet, check if we have space.  Again, be careful to
	 *  ensure there is space for a NUL following the input data.
	 */

	if (ajsheap_strdata_used + len + 1 > sizeof(ajsheap_strdata)) {
#if 0
		printf("ajsheap_extstr_check_1: ptr=%p, len=%ld ", (void *) ptr, (long) len);
		safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
		printf(" -> no space (used=%ld)\n", (long) ajsheap_strdata_used);
#endif
		return NULL;
	}

	/*
	 *  There is space, add the string to our collection, being careful
	 *  to append the NUL.
	 */

	ret = ajsheap_strdata + ajsheap_strdata_used;
	memcpy(ret, ptr, len);
	ret[len] = (uint8_t) 0;
	ajsheap_strdata_used += len + 1;

#if 0
	printf("ajsheap_extstr_check_1: ptr=%p, len=%ld -> ", (void *) ptr, (long) len);
	safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
	printf(" -> %p (used=%ld)\n", (void *) ret, (long) ajsheap_strdata_used);
#endif
	return (const void *) ret;
}

void ajsheap_extstr_free_1(const void *ptr) {
	(void) ptr;
#if 0
	printf("ajsheap_extstr_free_1: freeing extstr %p -> ", ptr);
	safe_print_chars((const char *) ptr, DUK_SIZE_MAX, 1 /*until_nul*/);
	printf("\n");
#endif
}

/*
 *  Simplified example of an external strings strategy where a set of strings
 *  is gathered during application compile time and baked into the application
 *  binary.
 *
 *  Duktape built-in strings are available from duk_build_meta.json, see
 *  util/duk_meta_to_strarray.py.  There may also be a lot of application
 *  specific strings, e.g. those used by application specific APIs.  These
 *  must be gathered through some other means, see e.g. util/scan_strings.py.
 */

static const char *strdata_duk_builtin_strings[] = {
	/*
	 *  These strings are from util/duk_meta_to_strarray.py
	 */

	"Logger",
	"Thread",
	"Pointer",
	"Buffer",
	"DecEnv",
	"ObjEnv",
	"",
	"global",
	"Arguments",
	"JSON",
	"Math",
	"Error",
	"RegExp",
	"Date",
	"Number",
	"Boolean",
	"String",
	"Array",
	"Function",
	"Object",
	"Null",
	"Undefined",
	"{_func:true}",
	"{\x22" "_func\x22" ":true}",
	"{\x22" "_ninf\x22" ":true}",
	"{\x22" "_inf\x22" ":true}",
	"{\x22" "_nan\x22" ":true}",
	"{\x22" "_undef\x22" ":true}",
	"toLogString",
	"clog",
	"l",
	"n",
	"fatal",
	"error",
	"warn",
	"debug",
	"trace",
	"raw",
	"fmt",
	"current",
	"resume",
	"compact",
	"jc",
	"jx",
	"base64",
	"hex",
	"dec",
	"enc",
	"fin",
	"gc",
	"act",
	"info",
	"version",
	"env",
	"modLoaded",
	"modSearch",
	"errThrow",
	"errCreate",
	"compile",
	"\xff" "Regbase",
	"\xff" "Thread",
	"\xff" "Handler",
	"\xff" "Finalizer",
	"\xff" "Callee",
	"\xff" "Map",
	"\xff" "Args",
	"\xff" "This",
	"\xff" "Pc2line",
	"\xff" "Source",
	"\xff" "Varenv",
	"\xff" "Lexenv",
	"\xff" "Varmap",
	"\xff" "Formals",
	"\xff" "Bytecode",
	"\xff" "Next",
	"\xff" "Target",
	"\xff" "Value",
	"pointer",
	"buffer",
	"\xff" "Tracedata",
	"lineNumber",
	"fileName",
	"pc",
	"stack",
	"ThrowTypeError",
	"Duktape",
	"id",
	"require",
	"__proto__",
	"setPrototypeOf",
	"ownKeys",
	"enumerate",
	"deleteProperty",
	"has",
	"Proxy",
	"callee",
	"Invalid Date",
	"[...]",
	"\x0a" "\x09",
	" ",
	",",
	"-0",
	"+0",
	"0",
	"-Infinity",
	"+Infinity",
	"Infinity",
	"object",
	"string",
	"number",
	"boolean",
	"undefined",
	"stringify",
	"tan",
	"sqrt",
	"sin",
	"round",
	"random",
	"pow",
	"min",
	"max",
	"log",
	"floor",
	"exp",
	"cos",
	"ceil",
	"atan2",
	"atan",
	"asin",
	"acos",
	"abs",
	"SQRT2",
	"SQRT1_2",
	"PI",
	"LOG10E",
	"LOG2E",
	"LN2",
	"LN10",
	"E",
	"message",
	"name",
	"input",
	"index",
	"(?:)",
	"lastIndex",
	"multiline",
	"ignoreCase",
	"source",
	"test",
	"exec",
	"toGMTString",
	"setYear",
	"getYear",
	"toJSON",
	"toISOString",
	"toUTCString",
	"setUTCFullYear",
	"setFullYear",
	"setUTCMonth",
	"setMonth",
	"setUTCDate",
	"setDate",
	"setUTCHours",
	"setHours",
	"setUTCMinutes",
	"setMinutes",
	"setUTCSeconds",
	"setSeconds",
	"setUTCMilliseconds",
	"setMilliseconds",
	"setTime",
	"getTimezoneOffset",
	"getUTCMilliseconds",
	"getMilliseconds",
	"getUTCSeconds",
	"getSeconds",
	"getUTCMinutes",
	"getMinutes",
	"getUTCHours",
	"getHours",
	"getUTCDay",
	"getDay",
	"getUTCDate",
	"getDate",
	"getUTCMonth",
	"getMonth",
	"getUTCFullYear",
	"getFullYear",
	"getTime",
	"toLocaleTimeString",
	"toLocaleDateString",
	"toTimeString",
	"toDateString",
	"now",
	"UTC",
	"parse",
	"toPrecision",
	"toExponential",
	"toFixed",
	"POSITIVE_INFINITY",
	"NEGATIVE_INFINITY",
	"NaN",
	"MIN_VALUE",
	"MAX_VALUE",
	"substr",
	"trim",
	"toLocaleUpperCase",
	"toUpperCase",
	"toLocaleLowerCase",
	"toLowerCase",
	"substring",
	"split",
	"search",
	"replace",
	"match",
	"localeCompare",
	"charCodeAt",
	"charAt",
	"fromCharCode",
	"reduceRight",
	"reduce",
	"filter",
	"map",
	"forEach",
	"some",
	"every",
	"lastIndexOf",
	"indexOf",
	"unshift",
	"splice",
	"sort",
	"slice",
	"shift",
	"reverse",
	"push",
	"pop",
	"join",
	"concat",
	"isArray",
	"arguments",
	"caller",
	"bind",
	"call",
	"apply",
	"propertyIsEnumerable",
	"isPrototypeOf",
	"hasOwnProperty",
	"valueOf",
	"toLocaleString",
	"toString",
	"constructor",
	"set",
	"get",
	"enumerable",
	"configurable",
	"writable",
	"value",
	"keys",
	"isExtensible",
	"isFrozen",
	"isSealed",
	"preventExtensions",
	"freeze",
	"seal",
	"defineProperties",
	"defineProperty",
	"create",
	"getOwnPropertyNames",
	"getOwnPropertyDescriptor",
	"getPrototypeOf",
	"prototype",
	"length",
	"alert",
	"print",
	"unescape",
	"escape",
	"encodeURIComponent",
	"encodeURI",
	"decodeURIComponent",
	"decodeURI",
	"isFinite",
	"isNaN",
	"parseFloat",
	"parseInt",
	"eval",
	"URIError",
	"TypeError",
	"SyntaxError",
	"ReferenceError",
	"RangeError",
	"EvalError",
	"break",
	"case",
	"catch",
	"continue",
	"debugger",
	"default",
	"delete",
	"do",
	"else",
	"finally",
	"for",
	"function",
	"if",
	"in",
	"instanceof",
	"new",
	"return",
	"switch",
	"this",
	"throw",
	"try",
	"typeof",
	"var",
	"void",
	"while",
	"with",
	"class",
	"const",
	"enum",
	"export",
	"extends",
	"import",
	"super",
	"null",
	"true",
	"false",
	"implements",
	"interface",
	"let",
	"package",
	"private",
	"protected",
	"public",
	"static",
	"yield",

	/*
	 *  These strings are manually added, and would be gathered in some
	 *  application specific manner.
	 */

	"foo",
	"bar",
	"quux",
	"enableFrob",
	"disableFrob"
	/* ... */
};

const void *ajsheap_extstr_check_2(const void *ptr, duk_size_t len) {
	int i, n;

	(void) safe_print_chars;  /* potentially unused */

	/* Linear scan.  An actual implementation would need some acceleration
	 * structure, e.g. select a sublist based on first character.
	 *
	 * NOTE: input string (behind 'ptr' with 'len' bytes) DOES NOT have a
	 * trailing NUL character.  Any strings returned from this function
	 * MUST have a trailing NUL character.
	 */

	n = (int) (sizeof(strdata_duk_builtin_strings) / sizeof(const char *));
	for (i = 0; i < n; i++) {
		const char *str;

		str = strdata_duk_builtin_strings[i];
		if (strlen(str) == len && memcmp(ptr, (const void *) str, len) == 0) {
#if 0
			printf("ajsheap_extstr_check_2: ptr=%p, len=%ld ",
			       (void *) ptr, (long) len);
			safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
			printf(" -> constant string index %ld\n", (long) i);
#endif
			return (void *) strdata_duk_builtin_strings[i];
		}
	}

#if 0
	printf("ajsheap_extstr_check_2: ptr=%p, len=%ld ",
	       (void *) ptr, (long) len);
	safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
	printf(" -> not found\n");
#endif
	return NULL;
}

void ajsheap_extstr_free_2(const void *ptr) {
	(void) ptr;
#if 0
	printf("ajsheap_extstr_free_2: freeing extstr %p -> ", ptr);
	safe_print_chars((const char *) ptr, DUK_SIZE_MAX, 1 /*until_nul*/);
	printf("\n");
#endif
}

/*
 *  External strings strategy intended for valgrind testing: external strings
 *  are allocated using malloc()/free() so that valgrind can be used to ensure
 *  that strings are e.g. freed exactly once.
 */

const void *ajsheap_extstr_check_3(const void *ptr, duk_size_t len) {
	duk_uint8_t *ret;

	(void) safe_print_chars;  /* potentially unused */

	ret = malloc((size_t) len + 1);
	if (ret == NULL) {
#if 0
		printf("ajsheap_extstr_check_3: ptr=%p, len=%ld ",
		       (void *) ptr, (long) len);
		safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
		printf(" -> malloc failed, return NULL\n");
#endif
		return (const void *) NULL;
	}

	if (len > 0) {
		memcpy((void *) ret, ptr, (size_t) len);
	}
	ret[len] = (duk_uint8_t) 0;

#if 0
	printf("ajsheap_extstr_check_3: ptr=%p, len=%ld ",
	       (void *) ptr, (long) len);
	safe_print_chars((const char *) ptr, len, 0 /*until_nul*/);
	printf(" -> %p\n", (void *) ret);
#endif
	return (const void *) ret;
}

void ajsheap_extstr_free_3(const void *ptr) {
	(void) ptr;
#if 0
	printf("ajsheap_extstr_free_3: freeing extstr %p -> ", ptr);
	safe_print_chars((const char *) ptr, DUK_SIZE_MAX, 1 /*until_nul*/);
	printf("\n");
#endif
	free((void *) ptr);
}

#else  /* DUK_CMDLINE_AJSHEAP */

int ajs_dummy = 0;  /* to avoid empty source file */

#endif  /* DUK_CMDLINE_AJSHEAP */
