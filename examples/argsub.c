#include <stdlib.h>
#include <zm.h>

zm_State *sub = NULL;

#define STR(x) ((x) ? (const char*)(x) : "<no msg>")

ZMTASKDEF( John )
{
	printf("John inbox:\n\t%s\n", STR(zmarg));
	ZMSTART

	zmstate 1:
	zmstate 2:
		zmResponse("Hello my name is John");
		zmyield zmCALLER | 2;


	ZMEND
}


ZMTASKDEF( Bob )
{
	printf("Bob inbox:\n\t%s\n", STR(zmarg));
	ZMSTART
	zmstate 1:
		sub = zmNewSubTasklet(John, NULL);
		zmyield zmSUB(sub, "Hello my name is Bob") | 2;

	zmstate 2:
		zmyield zmSUB(sub, "How are you?") | 3;

	zmstate 3:
		zmyield zmSUB(sub, "uhm ... nice to meet you") | 4;

	zmstate 4:
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("Bob: (I think John is an only one state machine)\n");

	ZMEND
}


int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s = zm_newTasklet(vm, Bob, NULL);
	zm_resume(vm, s, "Hi Bob, write something to John");
	zm_go(vm, 100);
	zm_closeVM(vm);
	zm_go(vm, 1000);
	zm_freeVM(vm);
	return 0;
}
