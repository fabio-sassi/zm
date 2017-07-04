#include <stdlib.h>
#include <zm.h>

/* simple zm lib task/coroutine example */

ZMTASKDEF( mycoroutine )
{
	ZMSTART

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
}


int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s = zm_newTask(vm, mycoroutine, NULL);
	zm_resume(vm, s, NULL);
	zm_go(vm, 100);
	return 0;
}
