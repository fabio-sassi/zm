#include <stdlib.h>
#include <zm.h>

#define NLOOP 10
#define NTASKS 4

int counter = 1;
int water = 0;

ZMTASKDEF( mycoroutine )
{
	struct Data {
		int id;
		int n;
	} *self = zmdata;

	ZMSTART

	zmstate 1:
		zmdata = self = malloc(sizeof(struct Data));
		self->id = counter++;
		self->n = 0;
		printf("task %d: -init-\n", self->id);
		zmyield 2;

	zmstate 2:
		printf("task %d: walking\n", self->id);
		zmyield 3;

	zmstate 3:
		printf("task %d: drinking\n", self->id);

		if (--water <= 0) {
			printf("task %d: no more water ... BREAK!\n", self->id);
			zm_break(vm);
			zmyield 3;
		}

		zmyield 4;

	zmstate 4:
		printf("task %d: loop [%d/%d]\n", self->id, self->n, NLOOP);

		self->n++;

		if (self->n > NLOOP)
			zmyield zmTERM;

		zmyield 2;



	zmstate ZM_TERM:
		printf("task %d: -end-\n", ((self) ? (self->id) : -1));
		if (self)
			free(self);

	ZMEND
}


int main() {
	int i, status;
	zm_VM *vm = zm_newVM("test VM");
	for (i = 0; i < NTASKS; i++) {
		zm_State *s = zm_newTasklet(vm, mycoroutine, NULL);
		zm_resume(vm, s, NULL);
	}

	while((status = zm_go(vm, 1000))) {
		if (status && ZM_RUN_BREAK) {
			printf("++ restore water\n");
			water = 10;
		}
	}

	zm_closeVM(vm);
	zm_go(vm, 1000);
	zm_freeVM(vm);

	return 0;
}

