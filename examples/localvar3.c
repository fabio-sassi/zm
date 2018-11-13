#include <stdlib.h>
#include <zm.h>

int pint(void *value)
{
	return (value) ? *((int*)value) : 0;
}


ZMTASKDEF(SumA)
{
	struct Local {
		int n;
	} *self = zmdata;

	ZMSTART

	zmstate 1: {
		int n0 = pint(zmdata);
		zmdata = self = malloc(sizeof(struct Local));
		self->n = n0;
		printf("    SumA: async instance task n0 = %d\n", n0);
		zmyield zmSUSPEND | 2;
	}
	zmstate 2:
		self->n += pint(zmarg);
		printf("    SumA: self->n = %d\n", self->n);
		zmyield zmSUSPEND | 2;

	zmstate ZM_TERM:
		printf("    SumA: term\n");
		if (self)
			free(self);

	ZMEND
}


ZMTASKDEF(SumB)
{
	struct Local {
		int n;
	} *self = zmdata;

	ZMSTART

	zmstate ZM_INIT: {
		int n0 = pint(zmdata);
		zmdata = self = malloc(sizeof(struct Local));
		self->n = n0;
		printf("SumB: constructor instance task (sync) n0 = %d\n", n0);
		zmyield zmDONE;
	}

	zmstate 1:
		self->n += pint(zmarg);
		printf("    SumB: self->n = %d\n", self->n);
		zmyield zmSUSPEND | 1;

	zmstate ZM_TERM:
		printf("    SumB: term\n");
		free(self);

	ZMEND
}

int main()
{
	zm_VM *vm = zm_newVM("test ZM");
	zm_State *a, *b;
	int i, *nptr;
	int n0 = 1;

	a = zm_newTasklet(vm , SumA, &n0);
	b = zm_newTasklet(vm , SumB, &n0);
	n0 = -1000;

	/* NOTE: async init must be used carefully. For exampe use a
	 * stack variable as a temporary argument for initalize task
	 * is not a good idea. The stack variable can be relased or
	 * modified and it's behaviour can change with compiler
	 * optimization.
	 * In this example (b) is initialized with n0 = 1 while (a)
	 * with n0 = -1000
	 */

	nptr = (int*)malloc(sizeof(int));

	for (i = 0; i < 4; i++) {
		*nptr = i;
		printf("main: add %d\n", *nptr);
		zm_resume(vm, a, nptr);
		zm_resume(vm, b, nptr);
		zm_go(vm, 20);
	}

	printf("main: end\n");

	zm_closeVM(vm);
	zm_go(vm, 100);
	free(nptr);
	zm_freeVM(vm);
}
