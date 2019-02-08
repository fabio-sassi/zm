#include <stdio.h>
#include <stdlib.h>
#include <zm.h>

/* iterator example */

ZMTASKDEF( myiter )
{
	ZMSTART

	zmstate 1:
		printf("    iter: init\n");
		zmyield zmCALLER | 2;

	zmstate 2:
		printf("    iter: 1\n");
		zmyield zmCALLER | 3;

	zmstate 3:
		printf("    iter: 2\n");
		zmyield zmCALLER | 4;

	zmstate 4:
		printf("    iter: last\n");
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("    iter: end\n");

	ZMEND
}


ZMTASKDEF( mycoroutine )
{

	struct Self {
		zm_State *iter;
	} *self = (struct Self*)zmdata;

	ZMSTART

	zmstate 1:
		printf("my task: init\n");
		zmdata = self = (struct Self*)malloc(sizeof(struct Self));
		self->iter = zmNewSubTask(myiter, NULL);

	zmstate 2:
		printf("my task: iter\n");
		zmyield zmSUB(self->iter, NULL) | zmNEXT(2) | 3;

	zmstate 3:
		printf("my task: iter end\n");
		zmyield zmTERM;

	zmstate ZM_TERM:
		/* check if zmdata != NULL in this program is useless but in
		 * more complex program (with this kind of data initialization)
		 * allow to avoid to destroy a task that has not just been
		 * initializated */
		if (!zmdata)
			zmyield zmEND;

		zm_freeSubTask(vm, self->iter);
		free(self);

	ZMEND
}



int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_resume(vm, zm_newTasklet(vm, mycoroutine, NULL), NULL);
	zm_go(vm, 100, NULL);
	zm_closeVM(vm);
	zm_go(vm, 100, NULL);
	zm_freeVM(vm);
	return 0;
}
