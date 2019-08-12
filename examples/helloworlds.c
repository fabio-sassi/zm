#include <stdlib.h>
#include <string.h>
#include <zm.h>


ZMTASKDEF( hellotask )
{
	char *self = zmdata;

	enum { HELLO = 1, VENUS_NOTE };

	ZMSTART

	zmstate ZM_INIT:
		printf("open connection from world %s\n", self);
		#ifdef USE_STRDUP
		zmdata = self = strdup(self);
		#else
		zmdata = self = strcpy((char*)malloc(strlen(self) + 1), self);
		#endif
		zmyield zmDONE;

	zmstate HELLO:
		printf(" Hello from %s!\n", self);

		if (self[0] == 'V')
			zmyield VENUS_NOTE;

		zmyield zmTERM;

	zmstate VENUS_NOTE:
		printf("note by %s: It's warm here!\n", self);
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("close connection from word %s\n", self);
		free(self);

	ZMEND
}


int main()
{
	zm_VM *vm = zm_newVM("test VM");

	zm_resume(vm, zm_newTasklet(vm, hellotask, "Earth"), NULL);
	zm_resume(vm, zm_newTasklet(vm, hellotask, "Mars"), NULL);
	zm_resume(vm, zm_newTasklet(vm, hellotask, "Venus"), NULL);
	zm_resume(vm, zm_newTasklet(vm, hellotask, "Omicron Persei 8"), NULL);

	while(zm_go(vm, 1, NULL));

	zm_freeVM(vm);

	return 0;
}

