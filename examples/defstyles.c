#include <stdio.h>
#include <zm.h>

/* task definition without {} */
ZMTASKDEF( mycoroutine ) ZMSTATES
	zmstate 1:
		printf("my task: init\n");
		zmyield 2;

	zmstate 2:
		printf("my task: 1\n");
		zmyield 3;

	zmstate 3:
		printf("my task: 2\n");
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("my task: end\n");
ZMEND


/* a bit more c style definition */
ZMTASKDEF( mycoroutine2 )
{
	ZMSTART

	zmstate 1:
		printf("{my task2}: init\n");
		zmyield 2;

	zmstate 2:
		printf("{my task2}: 1\n");
		zmyield 3;

	zmstate 3:
		printf("{my task2}: 2\n");
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("{my task2}: end\n");

	ZMEND
}



int main()
{
	zm_VM *vm = zm_newVM("test VM");
	zm_resume(vm, zm_newTasklet(vm, mycoroutine, NULL), NULL);
	zm_resume(vm, zm_newTasklet(vm, mycoroutine2, NULL), NULL);
	zm_go(vm, 100, NULL);
	zm_closeVM(vm);
	zm_go(vm, 100, NULL);
	zm_freeVM(vm);
	return 0;
}
