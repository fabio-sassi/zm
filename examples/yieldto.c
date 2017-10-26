#include <stdlib.h>
#define ZM_FAST_SYNTAX 1
#include <zm.h>


/* A task definition rappresent a generic task so can be instanced
 * as a ptask (pt) or as a subtask (sb)
 */
ZMTASKDEF(task2)

	const char *suffix = (const char*)zmdata;
	const char *arg = (const char*)zmarg;

	ZMSTATES

	zmstate 1:
		printf("    task2 %s: init %s\n", suffix, arg);
		yield zmTERM;

	zmstate ZM_TERM:
		printf("    task2 %s: end\n", suffix);
		yield zmEND;
ZMEND


ZMTASKDEF(task1) ZMSTATES
	zmstate 1: {
		zm_State *sb = zmNewSubTasklet(task2, "as sub");

		printf("task1: yield to sub\n");

		/* This yield put this task in busy-waiting mode.
		 * When sb will yield to zmEND this task will be resumed
		 * (in zmstate 2)
		 */
		yield zmSUB(sb, "(sub)") | 2;
	}
	zmstate 2: {
		zm_State *pt = zm_newTasklet(vm, task2, "as ptask");

		printf("task1: yield to ptask\n");

		/* This yield resume pt and simply suspend this task */

		yield zmTO(pt, "(to)") | 3;
	}

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

	printf("#main: now there are no more to do\n");
	zm_freeTask(vm, t);
	zm_closeVM(vm);
	zm_go(vm, 100);
	zm_freeVM(vm);
	return 0;
}
