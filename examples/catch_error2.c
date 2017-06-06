/*
Fabio Sassi 100% Public Domain
Error Exception catch example
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zm.h>

void out(const char *m)
{
	printf("\x1b[0;32mPRG - %s\x1b[0;40m\n", m);
}



ZMTASKDEF( subtask2 ) ZMSTATES
	zmstate 1:
		out("* subtask2: init");
		zmraise zmERROR(0, "example message", NULL);

	zmstate ZM_TERM:
		out("* subtask2: TERM");


ZMEND



ZMTASKDEF( subtask ) ZMSTATES
	zmstate 1: {
		zm_State *s = zmNewSubTasklet(subtask2, NULL);
		out("* subtask: init");
		zmyield zmSUB(s) | 2;
	}

	zmstate 2:
		out("* subtask: IMPOSSIBLE");
		zmyield zmTERM;

	zmstate ZM_TERM:
		out("* subtask: TERM");


ZMEND



ZMTASKDEF( task ) ZMSTATES
	zmstate 1: {
		zm_State *s = zmNewSubTasklet(subtask, NULL);
		out("* task: yield to subtask");
		zmyield zmSUB(s) | 3; //| zmCATCH(2);
	}
	zmstate 2: {
		zm_Exception *e = zmCatch();
		if (e) {
			out("* task: catch exception");
			if (zmIsError(e))
				zm_printError(NULL, e, true);
			out("---------------------------");
			zmFreeException();
			zmyield 3;
		}
		zmyield 3;
	}
	zmstate 3:
		out("* task: 3");
		zmyield zmTERM;

	zmstate ZM_TERM:
		out("* task: TERM");


ZMEND



void go(zm_VM *vm)
{
	int status;
	do {
		status = zm_go(vm, 100);
		switch(status) {
		case ZM_RUN_EXCEPTION: {
			out("~~~ zm_go: CATCH EXCEPTION");
			zm_Exception *e = zm_catch(vm);
			zm_printError(NULL, e, true);
			zm_freeError(vm, e);
			break;
		}

		case ZM_RUN_AGAIN:
			out("~~~ zm_go: RUN AGAIN");
			break;

		case ZM_RUN_IDLE:
			out("~~~ zm_go: IDLE");
			//zm_printVM(NULL, vm);
			break;
		}
	} while(status);
}


int main() {
	zm_VM *vm = zm_newVM("test ZM");
	zm_resume(vm, zm_newTasklet(vm, task, NULL));
	go(vm);
	zm_closeVM(vm);
	go(vm);
	zm_freeVM(vm);
	return 0;
}
