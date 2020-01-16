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

int eventCB(int scope, zm_Event* event, zm_State *s, void *arg)
{
	const char *msg = ((arg) ? ((const char*)arg) : ("null"));
	printf("callback: arg = `%s` scope = ", msg);

	if (scope & ZM_UNBIND)
		printf("UNBIND ");

	if (scope & ZM_UNBIND_ABORT)
		printf("UNBIND_ABORT ");

	if (scope & ZM_TRIGGER)
		printf("TRIGGER ");

	if (!s) {
		if (scope & ZM_TRIGGER) {
			printf("(pre-fetch)\n");
			return ZM_EVENT_ACCEPTED;
		} else {
			printf("(pre-free)\n");
			return 0;
		}
	}

	printf("task %d ", getID(s));

	if (scope & ZM_TRIGGER) {
		if (getID(s) == 1) {
			printf("(accepted)\n");
			return ZM_EVENT_ACCEPTED;
		}

		printf("(accepted, stop fetch)\n");
		return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
	} else {
		printf("(unbinded)\n");
		return 0;
	}
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
		printf("task %d: (triggered) msg = `%s`\n",
		       self->id,
		       (const char*)zmarg);

		zmyield zmTERM;

	zmstate 3:
		printf("task %d: (aborted) msg = `%s`",
		       self->id,
		       (const char*)zmarg);

		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("task %d: -end-\n", ((self) ? (self->id) : -1));
		if (self)
			free(self);

	ZMEND
}


int main()
{
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s1, *s2, *s3, *s4, *s5;

	event = zm_newEvent(eventCB, NULL);

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

	printf("* GO *\n");
	while(zm_go(vm, 1, NULL));

	printf("* trigger event *\n");
	zm_trigger(vm, event, "Hello");
	printf("* unbind s4 *\n");
	zm_unbind(vm, event, s4, "too late");

	printf("* GO *\n");
	while(zm_go(vm, 1, NULL));

	printf("* close all tasks *\n");
	zm_closeVM(vm);
	zm_go(vm, 1000, NULL);
	zm_freeVM(vm);

	printf("* free event *\n");
	zm_freeEvent(vm, event);

	return 0;
}

