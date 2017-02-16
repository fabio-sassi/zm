/*
Fabio Sassi 100% Public Domain
simple zm lib task/coroutine example
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zm.h>


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


/* this use {} in a bit more c style */
ZMTASKDEF( mycoroutine2 ) { ZMSTATES
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
ZMEND }



int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_resume(vm, zm_newTasklet(vm, mycoroutine, NULL));
	zm_resume(vm, zm_newTasklet(vm, mycoroutine2, NULL));
	zm_go(vm, 100);
	zm_closeVM(vm);
	zm_go(vm, 100);
	zm_freeVM(vm);
	return 0;
}
