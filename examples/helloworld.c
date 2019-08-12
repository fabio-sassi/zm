#include <stdlib.h>
#include <zm.h>

ZMTASKDEF( mytask )
{
	ZMSTART

	zmstate 1:
		printf("my task: -init-\n");
		zmyield 2;

	zmstate 2:
		printf("my task: hello\n");
		zmyield 3;

	zmstate 3:
		printf("my task: world\n");
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("my task: -end-\n");

	ZMEND
}


int main()
{
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s = zm_newTask(vm, mytask, NULL);
	zm_resume(vm, s, NULL);

	while(zm_go(vm, 1, NULL))
		printf("(step)\n");

	zm_freeTask(vm, s);
	zm_freeVM(vm);

	return 0;
}

