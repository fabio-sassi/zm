#include <stdlib.h>
#include <zm.h>

/* A simple implementation of task lock (mutex) */

#define NTASKS 4

#define taskLOCK(lock) \
	((taskLock(vm, (lock)) ? (0) : (zmEVENT(lock->ev))))


typedef struct {
	int id;
} TaskData;

typedef struct {
	zm_Event *ev;
	int locked;
} Lock;


int shared = 0;
int counter = 1;
Lock* lock;



int getID(zm_State *s)
{
	if (!s)
		return -1000;

	return ((TaskData*)(s->data))->id;
}


int acquireCB(int scope, zm_Event* event, zm_State *s, void *arg)
{
	Lock *lock = (Lock*)(event->data);

	if (scope & ZM_UNBIND)
		return 0;

	if (!s) {
		if (lock->locked <= 1) {
			lock->locked = 0;
			printf("event-callback: count locked task = 0\n");
			return ZM_EVENT_REFUSED;
		}

		return ZM_EVENT_ACCEPTED;
	}

	printf("event-callback: task %d acquire lock (waiting=%d)\n", getID(s),
	       lock->locked - 1);

	lock->locked--;

	return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
}


int taskLock(zm_VM *vm, Lock *lock)
{
	zm_State *s = zm_getCurrent(vm);

	if (lock->locked) {
		printf("taskLOCK > task %d ... waiting\n", getID(s));
		lock->locked++;
		return 0;
	}

	printf("taskLOCK > task %d ... acquire lock (first)\n", getID(s));

	lock->locked = 1;

	return 1;
}

void releaseLock(zm_VM *vm, Lock *lock)
{
	zm_trigger(vm, lock->ev, NULL);
}


Lock *newLock(zm_VM *vm)
{
	Lock* lock = malloc(sizeof(Lock));
	lock->locked = 0;
	lock->ev = zm_newEvent(acquireCB, lock);
	return lock;
}

void freeLock(zm_VM *vm, Lock* lock)
{
	zm_freeEvent(vm, lock->ev);
	free(lock);
}


void openResource()
{
	if (shared) {
		printf("RACE CONDITION ERROR: concurrent access to a "
		       "shared resource\n");
		exit(0);
	}

	shared = 1;
}

void closeResource()
{
	shared = 0;
}


ZMTASKDEF( mytask )
{
	TaskData *self = zmdata;

	ZMSTART

	zmstate 1:
		zmdata = self = malloc(sizeof(TaskData));
		self->id = counter++;
		printf("  * task %d: -init-\n", self->id);
		zmyield 2;

	zmstate 2:
		printf("  * task %d: lock...\n", self->id);
		zmyield taskLOCK(lock) | 3;

	zmstate 3:
		printf("  * task %d: lock aquired\n", self->id);
		openResource();
		zmyield 4;

	zmstate 4:
		printf("  * task %d: release lock\n", self->id);
		closeResource();
		releaseLock(vm, lock);
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("  * task %d: -end-\n", ((self) ? (self->id) : -1));
		if (self)
			free(self);

	ZMEND
}


int main()
{
	int i, j;
	zm_VM *vm = zm_newVM("test VM");

	lock = newLock(vm);


	for (j = 1; j <= 2; j++) {
		printf("---------- %d/2 ----------\n", j);

		for (i = 0; i < NTASKS; i++) {
			zm_State *s = zm_newTasklet(vm, mytask, NULL);
			zm_resume(vm, s, NULL);
		}

		while(zm_go(vm, 1, NULL));
	}


	zm_closeVM(vm);
	zm_go(vm, 1000, NULL);
	zm_freeVM(vm);

	freeLock(vm, lock);

	return 0;
}

