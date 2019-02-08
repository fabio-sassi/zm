#include <stdio.h>
#include <stdlib.h>
#include <zm.h>

/* Continue exception raise/unraise example */

zm_State *sub2;

ZMTASKDEF(task3)
{
	ZMSTART

	zmstate 1:
	    printf("    task3: init\n");
	    printf("    task3: raise continue exception (*) ...\n\n");
	    zmraise zmCONTINUE(0, "pause3", NULL) | 2;

	zmstate 2:
	    printf("    task3: (*) ... \n");
	    printf("    task3: argument = `%s`\n", (const char*)zmarg);
	    zmyield zmTERM;

	ZMEND
}


ZMTASKDEF(task2)
{
	ZMSTART

	zmstate 1:{
	    zm_State *s = zmNewSubTasklet(task3, NULL);
	    printf("  task2: init\n");
	    zmyield zmSUB(s, NULL) | 2;
	}

	zmstate 2:
	    printf("  task2: term\n");
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
		printf("task1: catch...\n");

		if (e)
			printf("task1: catch exception='%s'\n", e->msg);
		else
			printf("task1: no-exception\n");

	    zmyield 4;
	}

	zmstate 4:
	    /* this resume subtask that raise zmCONTINUE */
	    printf("task1: resume continue-exception block\n\n");
	    zmyield zmUNRAISE(sub2, "go_on3") | 2;

	ZMEND
}

int main() {
	zm_VM *vm = zm_newVM("test");
	zm_resume(vm , zm_newTasklet(vm , task1, NULL), NULL);
	zm_go(vm , 100, NULL);
	zm_closeVM(vm);
	zm_go(vm, 1000, NULL);
	zm_freeVM(vm);
	return 0;
}

