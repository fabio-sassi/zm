#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <zm.h>

zm_VM* vm = NULL;

#define PTR2INT(p) ((int)((size_t)(p)))
#define INT2PTR(n) ((void*)((size_t)(n)))

const char *text =
	"    abc cdef cde bc abcd a c efgh\n"
	"    efg a bcde bcde abcd defg e de\n"
	"    defg cde abcd cdef ef de defg d\n"
	"    efgh de efgh b defg\n";


int textlen;

char *utext;
char *pattern;
int patternlen;

typedef struct {
	int start;
	int len;
} Match;

/*
 * Write the name of the current task before the message.
 * The task name is indent as its execution-stack deep.
 */
void zout(const char *m, ...)
{
	zm_State *c = zm_getCurrent(vm);
	va_list args;

	if (c) {
		const char *name = zm_getMachine(vm)->name;
		size_t n = zm_getDeep(c);

		for (int i = 0; i < n; i++)
			printf("    ");

		printf("\x1b[0;32m%s:\x1b[0;40m ", name);
	}

	va_start(args, m);
	vfprintf(stdout, m, args);
	va_end(args);

	fprintf(stdout, "\n");
	fflush(stdout);
}

/*
 * Get the longest match (between text and pattern) starting from the
 * argument (zmarg) position.
 */
ZMTASKDEF(IterMatch)
{
	Match* self = zmdata;

	ZMSTART

	enum {INIT = 1, SEARCH, ENDMATCH, ACCEPTED, TERM};

	zmstate INIT: {
	    int pos = PTR2INT(zmarg);
		zout("init - pos = %d", pos);
		zmdata = self = malloc(sizeof(Match));
		self->start = pos;
		self->len = 0;
	}

	zmstate SEARCH:
		if (self->len >= patternlen)
			zmyield ENDMATCH;

		if (pattern[self->len] == '\0')
			zmyield ENDMATCH;

		if (pattern[self->len] != text[self->start + self->len])
			zmyield ENDMATCH;

		self->len++;

	    zmyield SEARCH;

	zmstate ENDMATCH:
		/* raise a continue exception, Upper-task will resume only the
		   first two task with the better match */
		zout("match len = %d", self->len);
		zmraise zmCONTINUE(0, "match", self) | ACCEPTED;

	zmstate ACCEPTED:
		zout("accepted match [%d:%d]", self->start, self->len);
		zmresult = self;
		/* "self" is used as response so this task cannot be free
		   until message is received */
		zmyield zmSUSPEND | TERM;

	zmstate TERM:
	zmstate ZM_TERM:
		if (!self)
			zmyield zmEND;

		free(self);

	ZMEND
}

/*
 * Search from the position (passed as argument zmarg) all the character
 * of pattern that match the ones in text.
 * If Upper-task accept this match (see continue exception raise in IterMatch
 * subtask) SearchTask will replace all the matching subtring in utext with
 * an upper case version.
 */
ZMTASKDEF(SearchTask)
{
	struct Data {
		zm_State *iter;
		int pos;
	} *self = zmdata;

	enum {INIT = 1, REPLACE};

	ZMSTART

	zmstate INIT: {
	    int pos = PTR2INT(zmarg);
		zout("init search from pos = %d", pos);
		zmdata = self = malloc(sizeof(struct Data));
		self->iter = zmNewSubTask(IterMatch, NULL);
		self->pos = pos;
	    zmyield zmSUB(self->iter, INT2PTR(pos)) | REPLACE;
	}

	zmstate REPLACE: {
		Match *m = (Match*)zmarg;
		int i;

		zout("replace match substring:");

		for (i = 0; i < m->len; i++) {
			printf("\treplace %c ", utext[m->start + i]);
			utext[m->start + i] = toupper(text[m->start + i]);
			printf("with %c\n", utext[m->start + i]);
		}

		zmyield zmTERM;
	}

	zmstate ZM_TERM:
		if (!self)
			zmyield zmEND;

		zm_freeSubTask(vm, self->iter);
		free(self);

	ZMEND
}

/*
 * Upper instance a search subtask for each occurence of the first char
 * of pattern in text.
 *
 * Each subtask will go deep in the pattern search and raise a continue-
 * -exception at the end of the pattern match search.
 *
 * Upper take the two longest match and resume it to perform the upper case
 * replace of the matched substring.
 */
ZMTASKDEF(Upper)
{
	struct Search {
		zm_State *t;
		Match *match;
		int pos;
	};

	struct Data {
		struct Search **children;
		int nchildren;
		int current;
		int selected;
	} *self = zmdata;

	enum {INIT = 1, SEARCH, SELECT, MATCH};

	ZMSTART

	zmstate INIT: {
		zout("init");
		int i, n = 0;
		zmdata = self = malloc(sizeof(struct Data));

		for (i = 0; i < textlen; i++)
			if (text[i] == pattern[0])
				n++;

		zout("found %d matching position", n);
		self->children = malloc(sizeof(struct Search*) * n);
		self->nchildren = n;
		self->current = 0;
		self->selected = 0;

		n = 0;

		for (i = 0; i < textlen; i++) {
			struct Search *sc;
			char s[7];

			if (text[i] != pattern[0])
				continue;

			zout("positon = %d - %s...", i, strncpy(s, text+i, 7));

			sc = malloc(sizeof(struct Search));
			sc->t = zmNewSubTask(SearchTask, NULL);
			sc->match = NULL;
			sc->pos = i;
			self->children[n++] = sc;
		}
	}

	zmstate SEARCH: {
		struct Search *child;
		zout("search from pos = %d", self->current);
		if (self->current >= self->nchildren)
			zmyield SELECT;

		child = self->children[self->current++];

		zmyield zmSUB(child->t, INT2PTR(child->pos)) | zmCATCH(MATCH);
	}

	zmstate MATCH: {
	    zm_Exception* e = zmCatch();
	    zm_State *c = zmContinueHead(e);
		int i;

		/* search the child that raise the continue-exception */
		for (i = 0; i < self->nchildren; i++) {
			struct Search *child = self->children[i];

			if (child->t == c) {
				/* set the match in child */
				child->match = (Match*)e->data;
				zmyield SEARCH;
			}
		}

		zmraise zmABORT(0, "what?!", NULL);
	}

	zmstate SELECT: {
		struct Search *best = NULL;
		int i;

		if (self->selected++ >= 2)
			zmyield zmTERM;

		for (i = 0; i < self->nchildren; i++) {
			struct Search *s = self->children[i];

			if (!s->match)
				continue;


			if (!best) {
				best = s;
				continue;
			}

			if (best->match->len < s->match->len)
				best = s;
		}

		if (best) {
			best->match = NULL;
			zmyield zmUNRAISE(best->t, NULL) | SELECT;
		}

		zout("no more match found");
		zmyield zmTERM;
	}

	zmstate ZM_TERM:
		if (self) {
			int i;
			for (i = 0; i < self->nchildren; i++) {
				zm_freeSubTask(vm, self->children[i]->t);
				free(self->children[i]);
			}

			free(self->children);
			free(self);
		}

		zmyield zmEND;

	ZMEND
}


int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("\nusage:\n\t%s pattern\n", argv[0]);
		printf("\nThe application search for the first two longest\n"
		       "occurence of pattern and uppercase them.\n\n");
		printf("Search is performed on this text:\n\n");
		printf("%s\n", text);
		exit(0);
	}

	vm = zm_newVM("test");

	textlen = strlen(text);
	utext = malloc(sizeof(char) * (textlen+1));
	strcpy(utext, text);

	pattern = argv[1];
	patternlen = strlen(pattern);

	zm_resume(vm , zm_newTasklet(vm, Upper, NULL), NULL);
	while(zm_go(vm , 100, NULL));

	printf("\n---------------------------------\n");
	printf("\nsource:\n%s\n", text);
	printf("result:\n%s\n", utext);

	free(utext);

	zm_closeVM(vm);
	zm_go(vm, 1000, NULL);
	zm_freeVM(vm);
	return 0;
}

