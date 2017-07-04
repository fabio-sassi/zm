#include <stdio.h>
#include <stdlib.h>
#include <zm.h>

/* Continue exception raise/unraise example */

zm_State *sub2;

ZMTASKDEF(task3)
{
	ZMSTART

	zmstate 1:
	    printf("\t\ttask3: init\n");
	    printf("\t\ttask3: raise continue exception (*)\n");
	    zmraise zmCONTINUE(0, "continue-test", NULL) | 2;

	zmstate 2:
	    printf("\t\ttask3: (*) unraised ... OK\n");
	    printf("\t\ttask3: msg = `%s`\n", (const char*)zmarg);

	zmstate 3:
	    printf("\t\ttask3: no more to do ... term\n");
	    zmyield zmTERM;

	ZMEND
}


ZMTASKDEF(task2)
{
	ZMSTART

	zmstate 1:{
	    zm_State *s = zmNewSubTasklet(task3, NULL);
	    printf("\ttask2: init\n");
	    zmyield zmSUB(s, NULL) | 2;
	}

	zmstate 2:
	    printf("\ttask2: term\n");
	    zmyield zmTERM;

	ZMEND
}


ZMTASKDEF(task1)
{
	ZMSTART

	zmstate 1:
	    sub2 = zmNewSubTasklet(task2, NULL);
	    printf("task1: init\n");
	    zmyield zmSUB(sub2, NULL) | 2 | zmCATCH(3);

	zmstate 2:
	    printf("task1: term\n");
	    zmyield zmTERM;

	zmstate 3: {
	    zm_Exception* e = zmCatch();
	    const char *msg = (e) ? e->msg : "no exception";
		printf("task1: catching...%s\n", msg);
	    zmyield 4;
	}

	zmstate 4:
	    printf("task1: some operation\n");
	    zmyield 5;

	zmstate 5:
	    /* this resume the subtask that raise zmCONTINUE: task3 */
	    printf("task1: resuming continue-exception-block\n");
	    zmyield zmUNRAISE(sub2, "hello-I-am-task1") | 2;

	ZMEND
}

int main() {
	zm_VM *vm = zm_newVM("test");
	zm_resume(vm , zm_newTasklet(vm , task1, NULL), NULL);
	zm_go(vm , 100);
	zm_closeVM(vm);
	zm_go(vm, 1000);
	zm_freeVM(vm);
	return 0;
}

