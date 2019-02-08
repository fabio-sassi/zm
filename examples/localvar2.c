#include <stdio.h>
#include <stdlib.h>
#include <zm.h>

ZMTASKDEF(foo) {
	struct FooLocal {
		int i;
	} *self = zmdata;

	ZMSTART

	zmstate ZM_INIT: {
		int *n0 = zmdata;
		printf("foo: constructor(%d)\n", *n0);
		zmdata = self = malloc(sizeof(struct FooLocal));
		self->i = *n0;
		zmyield zmDONE;
	}

	zmstate 1:
		printf("foo: self->i = %d\n", self->i++);
		zmyield 1;

	zmstate ZM_TERM:
		printf("foo: distructor\n");
		free(self);

	ZMEND
}

int main()
{
	zm_VM *vm = zm_newVM("test ZM");
	zm_State *task;
	int n0 = 4;
	task = zm_newTasklet(vm , foo, &n0);
	zm_resume(vm , task, NULL);
	zm_go(vm, 5, NULL);
	zm_closeVM(vm);
	zm_go(vm, 100, NULL);
	zm_freeVM(vm);
}
