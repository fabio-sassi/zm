/*
Fabio Sassi 100% Public Domain
Continue Exception raise/unraise example
*/
#include <stdlib.h>
#define ZM_FAST_SYNTAX 1
#include <zm.h>


/* A task definition rappresent a generic task so can be instanced
   as a ptask or as a subtask */
ZMTASKDEF(task2)
	const char *suffix = (const char*)zmdata;
	ZMSTATES
	zmstate 1:
		printf("    task2 %s: init %s\n", suffix, ((const char*)zmarg));
		yield zmTERM;

	zmstate ZM_TERM:
		printf("    task2 %s: end\n", suffix);
		yield zmEND;
ZMEND


ZMTASKDEF(task1) ZMSTATES
	zmstate 1:
		printf("task1: yield to sub\n");
		/* this yield suspend task1 but subtask task2 will resume it
		   yielding to end */
		yield zmSUB(zmNewSubTasklet(task2, "as sub"), "(sub)") | 2;

	zmstate 2:
		printf("task1: yield to ptask\n");
		/* this yield suspend task1 and active task2 */
		yield zmTO(zm_newTasklet(vm,task2, "as ptask"), "(to)") | 3;

	zmstate 3:
		printf("task1: term\n");
		yield zmTERM;

ZMEND


int main() {
	zm_VM *vm = zm_newVM("test");
	zm_State* t = zm_newTask(vm, task1, NULL);
	zm_resume(vm, t, NULL);

	printf("#main: process all jobs...\n");
	while(zm_go(vm, 1)) {}

	printf("#main: no more to do...sure?\n");
	zm_resume(vm, t, NULL);
	while(zm_go(vm, 1)) {}

	printf("#main: now there is no more to do\n");
	zm_freeTask(vm, t);
	zm_closeVM(vm);
	zm_go(vm, 100);
	zm_freeVM(vm);
	return 0;
}
