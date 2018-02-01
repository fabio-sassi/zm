#include <stdlib.h>
#include <stdio.h>
#include <zm.h>

typedef struct {
	int id;
} TaskData;

int counter = 1;

zm_Event * event;

int getID(zm_State *s)
{
	return ((TaskData*)(s->data))->id;
}

int eventcb(zm_VM *vm, int scope, zm_Event* e, zm_State *s, void *arg)
{
	const char *msg = ((arg) ? ((const char*)arg) : ("null"));
	printf("\tcallback: arg = `%s` scope = ", msg);

	if (scope & ZM_UNBIND_REQUEST)
		printf("UNBIND_REQUEST ");

	if (scope & ZM_UNBIND_ABORT)
		printf("UNBIND_ABORT ");

	if (scope & ZM_TRIGGER)
		printf("TRIGGER ");

	printf("\n");

	if (!s) {
		if (scope & ZM_TRIGGER)
			printf("\t\t-> pre-fetch\n");
		else
			printf("\t\t-> post-fetch (pre-free)\n");

		return ZM_EVENT_ACCEPTED;
	}

	printf("\t\t-> fetch task %d ", getID(s));

	if ((scope & ZM_TRIGGER) && (getID(s) == 1)) {
		printf("(accepted)\n");
		return ZM_EVENT_ACCEPTED;
	}

	if (scope & ZM_TRIGGER)
		printf("(accepted but stop other fetches)");

	printf("\n");
	return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
}




ZMTASKDEF( mycoroutine )
{
	TaskData *self = zmdata;

	ZMSTART

	zmstate 1:
		zmdata = self = malloc(sizeof(TaskData));
		self->id = counter++;
		printf("task %d: -init-\n", self->id);
		zmyield zmEVENT(event) | 2 | zmUNBIND(3);

	zmstate 2:
		printf("task %d: msg = `%s`\n", self->id, (const char*)zmarg);
		zmyield zmTERM;

	zmstate 3:
		printf("task %d: event aborted - ", self->id);
		printf("msg = `%s`\n", (const char*)zmarg);
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("task %d: -end-\n", ((self) ? (self->id) : -1));
		if (self)
			free(self);

	ZMEND
}


int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s1, *s2, *s3, *s4, *s5;

	event = zm_newEvent(NULL);
	zm_setEventCB(vm, event, eventcb, ZM_TRIGGER | ZM_UNBIND);

	s1 = zm_newTasklet(vm, mycoroutine, NULL);
	s2 = zm_newTasklet(vm, mycoroutine, NULL);
	s3 = zm_newTasklet(vm, mycoroutine, NULL);
	s4 = zm_newTasklet(vm, mycoroutine, NULL);
	s5 = zm_newTasklet(vm, mycoroutine, NULL);
	zm_resume(vm, s1, NULL);
	zm_resume(vm, s2, NULL);
	zm_resume(vm, s3, NULL);
	zm_resume(vm, s4, NULL);
	zm_resume(vm, s5, NULL);

	while(zm_go(vm, 1));

	printf("\n* trigger event:\n");
	zm_trigger(vm, event, "Hello");
	printf("\n* unbind s4:\n");
	zm_unbind(vm, event, s4, "I don't want to wait anymore");

	printf("\n\n");
	while(zm_go(vm, 1));

	printf("\n* close all tasks:\n\n");
	zm_closeVM(vm);
	zm_go(vm, 1000);
	zm_freeVM(vm);

	printf("\n* free event:\n\n");
	zm_freeEvent(vm, event);

	return 0;
}

