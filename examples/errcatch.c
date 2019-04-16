#include <stdio.h>
#include <stdlib.h>
#include <zm.h>

/* error exception catch example */

ZMTASKDEF( subtask2 ) ZMSTATES
	zmstate 1:
		printf("\t\t * subtask2: init\n");
		zmraise zmABORT(0, "example message", NULL);

	zmstate 2:
		printf("\t\t * subtask2: TERM");
		zmyield zmTERM;
ZMEND



ZMTASKDEF( subtask ) ZMSTATES
	zmstate 1: {
		zm_State *s = zmNewSubTasklet(subtask2, NULL);
		printf("\t * subtask: init\n");
		zmyield zmSUB(s, NULL) | 2;
	}
	zmstate 2:
		printf("\t * subtask: TERM");
		zmyield zmTERM;
ZMEND



ZMTASKDEF( task ) ZMSTATES
	zmstate 1: {
		zm_State *s = zmNewSubTasklet(subtask, NULL);
		printf("* task: yield to subtask\n");
		zmyield zmSUB(s, NULL) | 2 | zmCATCH(2);
	}
	zmstate 2: {
		zm_Exception *e = zmCatch();
		if (e) {
			printf("* task: catch exception\n");
			if (e->kind == ZM_EXCEPTION_ABORT)
				zm_printException(NULL, e, 1);
			printf("---------------------------\n");
			zmyield 3;
		}
		zmyield 3;
	}
	zmstate 3:
		printf("* task: 3 - end\n");
		zmyield zmTERM;
ZMEND




int main()
{
	zm_VM *vm = zm_newVM("test ZM");
	zm_resume(vm, zm_newTasklet(vm, task, NULL), NULL);
	zm_go(vm, 100, NULL);
	zm_closeVM(vm);
	zm_go(vm, 100, NULL);
	zm_freeVM(vm);
	return 0;
}
