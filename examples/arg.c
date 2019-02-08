#include <stdlib.h>
#include <zm.h>

/* simple zm lib task/coroutine example */

ZMTASKDEF( Foo )
{
	const char* arg = ((zmarg) ? (const char*)(zmarg) : "<none>");
	ZMSTART

	zmstate 1:
		printf("    Foo: state 1 zmarg = %s\n", arg);
		zmyield 2;

	zmstate 2:
		printf("    Foo: state 2 zmarg = %s\n", arg);
		zmyield zmSUSPEND | 2;

	zmstate ZM_TERM:
		printf("    Foo: state term zmarg = %s\n", arg);

	ZMEND
}

int main()
{
	zm_VM *vm = zm_newVM("test ZM");
	zm_State *f = zm_newTasklet(vm, Foo, NULL);

	printf("task 1: resume with 'Hello'\n");
	zm_resume(vm, f, "Hello");
	zm_go(vm, 5, NULL);

	printf("task 1: resume with 'How are you?'\n");
	zm_resume(vm, f, "How are you?");
	zm_go(vm, 5, NULL);

	printf("task 1: close\n");
	zm_abort(vm, f);
	zm_go(vm, 5, NULL);

	printf("task 2: resume (but not process) with argument 'Bye'\n");
	f = zm_newTasklet(vm, Foo, NULL);
	zm_resume(vm, f, "Bye");

	zm_closeVM(vm);
	zm_go(vm, 100, NULL);
	zm_freeVM(vm);
}
