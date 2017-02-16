/*
Fabio Sassi 100% Public Domain
Error Exception catch example
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zm.h>



ZMTASKDEF( subtask2 ) ZMSTATES
	zmstate 1:
		printf("\t\tsubtask2: init\n");
		zmraise zmERROR(0, "example message", NULL);

	zmstate 2:
		printf("\t\tsubtask2: TERM");
		zmyield zmTERM;
ZMEND



ZMTASKDEF( subtask ) ZMSTATES
	zmstate 1: {
		zm_State *s = zmNewSubTasklet(subtask2, NULL);
		printf("\tsubtask: init\n");
		zmyield zmSUB(s) | 2;
	}
	zmstate 2:
		printf("\tsubtask: TERM");
		zmyield zmTERM;
ZMEND



ZMTASKDEF( task ) ZMSTATES
	zmstate 1: {
		zm_State *s = zmNewSubTasklet(subtask, NULL);
		printf("task: yield to subtask\n");
		zmyield zmSUB(s) | 2 | zmCATCH(2);
	}
	zmstate 2: {
		zm_Exception *e = zmCatch();
		if (e) {
			printf("task: catch exception\n");
			if (zmIsError(e))
				zmPrintError(stdout, e, 1);
			zmFreeException();
			zmyield zmTERM;
		}
		printf("task: end\n");
		zmyield zmTERM;
	}
ZMEND




int main() {
	zm_VM *vm = zm_newVM("test ZM");
	zm_resume(vm, zm_newTasklet(vm, task, NULL));
	zm_go(vm, 100);
	zm_closeVM(vm);
	zm_go(vm, 100);
	zm_freeVM(vm);
	return 0;
}
