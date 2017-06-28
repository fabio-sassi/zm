/*
Fabio Sassi 100% Public Domain
simple zm lib task/coroutine example
*/
#include <stdlib.h>
#include <zm.h>

ZMTASKDEF( Foo )
{
	const char* arg = ((zmarg) ? (const char*)(zmarg) : "<none>");
	printf("zmarg = %s\n", arg);
	ZMSTART

	zmstate 1:
		printf("foo: 1\n");
		zmyield 2;
	zmstate 2:
		printf("foo: 2\n");
		zmyield zmSUSPEND | 2;

	ZMEND
}

int main()
{
	zm_VM *vm = zm_newVM("test ZM");
	zm_State *f = zm_newTasklet(vm , Foo, NULL);

	zm_resume(vm, f, "Hello");
	zm_go(vm , 5);

	zm_resume(vm, f, "How are you?");
	zm_go(vm , 5);

	zm_closeVM(vm);
	zm_go(vm, 100);
	zm_freeVM(vm);
}
