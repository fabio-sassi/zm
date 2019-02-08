#include <stdlib.h>
#include <zm.h>

#define NTASKS 3

int counter = 1;

zm_Event * event;

ZMTASKDEF( mycoroutine )
{
	struct Data {
		int id;
	} *self = zmdata;

	ZMSTART

	zmstate 1:
		zmdata = self = malloc(sizeof(struct Data));
		self->id = counter++;
		printf("task %d: -init-\n", self->id);
		zmyield 2;

	zmstate 2:
		printf("task %d: Hello...\n", self->id);
		zmyield zmEVENT(event) | 3;

	zmstate 3:
		printf("task %d: event msg = <%s>\n", self->id, (const char*)zmarg);
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("task %d: -end-\n", ((self) ? (self->id) : -1));
		if (self)
			free(self);

	ZMEND
}


int main() {
	int i;
	zm_VM *vm = zm_newVM("test VM");

	event = zm_newEvent(NULL);

	for (i = 0; i < NTASKS; i++) {
		zm_State *s = zm_newTasklet(vm, mycoroutine, NULL);
		zm_resume(vm, s, NULL);
	}

	while(zm_go(vm, 1, NULL));

	printf("\n ** no more to do...trigger event\n\n");

	zm_trigger(vm, event, "worlds!");

	while(zm_go(vm, 1, NULL));

	zm_closeVM(vm);
	zm_go(vm, 1000, NULL);
	zm_freeVM(vm);

	zm_freeEvent(vm, event);

	return 0;
}

