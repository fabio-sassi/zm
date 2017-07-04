#include <stdio.h>
#include <zm.h>

/* Extern task definition 2/2 */

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

