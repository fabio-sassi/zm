#include <stdlib.h>
#include <zm.h>

zm_State *s1 = NULL;


ZMTASKDEF(sub1)
{
	ZMSTART

	zmstate 1:
		printf("    sub1: loop\n");
		zmyield zmSUB(s1, NULL) | 1;

	zmstate ZM_TERM:
		printf("    sub1: end\n");
		zmyield zmEND;

	ZMEND
}



ZMTASKDEF(root)
	ZMSTART

	zmstate ZM_INIT: {
		printf("root: init\n");
		s1 = zmNewSubTasklet(sub1, NULL);
		zmyield zmDONE;
	}

	zmstate 1: {
		printf("root: yield to sub1\n");

		zmyield zmSUB(s1, NULL) | 2;
	}


	zmstate 3:
		printf("root: term\n");
		zmyield zmTERM;

ZMEND



int main()
{
	zm_VM *vm = zm_newVM("test");
	zm_State* t = zm_newTask(vm, root, NULL);
	zm_resume(vm, t, NULL);

#if UNEXP
	{
	zm_Exception e;

	e.kind = 1000;
	zm_uFree(vm, &e);
	}
#endif

	printf("#main: go...\n");
	while(zm_go(vm, 1, NULL)) {}

	zm_freeTask(vm, t);
	zm_closeVM(vm);
	zm_go(vm, 100, NULL);
	zm_freeVM(vm);
	return 0;
}
