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
	printf("\x1b[0;32mPRG\x1b[0;40m - %s\n", m);
}

void outgo(const char *p, const char *m)
{
	printf("\x1b[0;32mPRG - zm_go:\x1b[0;40m %s ~.- %s\n", p, m);
}



ZMTASKDEF( subtask2 ) ZMSTATES
	zmstate 1:
		out("* subtask2: init");
		out("* subtask2: raise");
		zmraise zmERROR(0, "example message", NULL) | 2;

	zmstate 2:
		out("* subtask2: RESET OK!");
		zmyield zmTERM;

	zmstate ZM_TERM:
		out("* subtask2: TERM");


ZMEND


zm_State *sub2, *sub;

ZMTASKDEF( subtask ) ZMSTATES
	zmstate 1: {
		sub2 = zmNewSubTasklet(subtask2, NULL);
		out("* subtask: init");
		zmyield zmSUB(sub2) | 2 |  zmRESET(3);
	}

	zmstate 2:
		out("* subtask: IMPOSSIBLE");
		zmyield zmTERM;

	zmstate 3:
		out("* subtask: RESET OK ... go deep");
		zmyield zmSUB(sub2) | 4;

	zmstate 4:
		out("* subtask: caller have reset too");
		zmyield zmTERM;

	zmstate ZM_TERM:
		out("* subtask: TERM");


ZMEND



ZMTASKDEF( task ) ZMSTATES
	zmstate 1: {
		sub = zmNewSubTasklet(subtask, NULL);
		out("* task: yield to subtask");
		zmyield zmSUB(sub) | 3 | zmCATCH(2);
	}
	zmstate 2: {
		zm_Exception *e = zmCatch();
		if (e) {
			out("* task: catch exception");
			if (zmIsError(e))
				zm_printError(NULL, e, true);
			out("---------------------------");
			zmFreeException();

			zmyield zmSUB(sub) | 4;
		}
		zmyield 3;
	}
	zmstate 3:
		out("* task: 3???");
		zmyield zmTERM;

	zmstate 4:
		out("* task: 4");

		zmyield zmTERM;
	zmstate ZM_TERM:
		out("* task: TERM");


ZMEND



void go(zm_VM *vm, const char *prefix)
{
	int status;
	do {
		status = zm_go(vm, 100);
		switch(status) {
		case ZM_RUN_EXCEPTION: {
			outgo(prefix, "CATCH EXCEPTION");
			zm_Exception *e = zm_catch(vm);
			zm_printError(NULL, e, true);
			zm_freeError(vm, e);
			break;
		}

		case ZM_RUN_AGAIN:
			outgo(prefix, "RUN AGAIN");
			break;

		case ZM_RUN_IDLE:
			outgo(prefix, "IDLE");
			break;
		}
	} while(status);
}


int main() {
	zm_VM *vm = zm_newVM("test ZM");
	zm_resume(vm, zm_newTasklet(vm, task, NULL));
	go(vm, "running");
	zm_closeVM(vm);
	go(vm, "closing");
	zm_freeVM(vm);
	return 0;
}
