/*
Fabio Sassi 100% Public Domain
Extern task definition 2/2
*/
#include <stdio.h>
#include <zm.h>

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

