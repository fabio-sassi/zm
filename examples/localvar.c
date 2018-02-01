#include <zm.h>
#include <stdio.h>


ZMTASKDEF(foo) {
	struct FooLocal {
		int i;
	} *self = zmdata;

	ZMSTART

	zmstate 1:
		self = malloc(sizeof(struct FooLocal));
		self->i = 0;
		zmdata = self;

	zmstate 2:
		self->i++;
		printf("self->i = %d\n", self->i);
		zmyield 2;

	ZMEND
}

int main()
{
	zm_VM *vm = zm_newVM("test ZM");
	zm_resume(vm , zm_newTasklet(vm , foo, NULL), NULL);
	zm_go(vm , 5);
}
