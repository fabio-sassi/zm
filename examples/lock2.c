#include <stdlib.h>
#include <zm.h>

/* An implementation of task lock (mutex) with owner check and abort lock */

#define NTASKS 8

#define taskLOCK(lock, abrt) \
	((acqlck_(vm, (lock)) ? (0) : (zmEVENT(lock->ev) | zmUNBIND(abrt))))


typedef struct {
	int id;
} TaskData;

typedef struct {
	zm_Event *ev;
	zm_State *owner;
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


int acquirecb(zm_VM *vm, int scope, zm_Event* event, zm_State *s, void *arg)
{
	Lock *lock = (Lock*)(event->data);

	if (!s) {
		if (lock->locked <= 1) {
			lock->locked = 0;
			printf("callback > count locked task = 0\n");
			return ZM_EVENT_REFUSED;
		}

		return ZM_EVENT_ACCEPTED;
	}

	printf("callback > task %d acquire lock (waiting=%d)\n", getID(s),
	       lock->locked - 1);


	lock->owner = s;
	lock->locked--;

	return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
}



int acqlck_(zm_VM *vm, Lock *lock)
{
	zm_State *s = zm_getCurrent(vm);

	if (lock->locked) {
		printf("taskLOCK > task %d ... waiting\n", getID(s));
		lock->locked++;
		return 0;
	}

	printf("taskLOCK > task %d ... acquire lock (first)\n", getID(s));

	lock->owner = s;
	lock->locked = 1;

	return 1;
}


void releaseLock(zm_VM *vm, Lock *lock)
{
	zm_State *s = zm_getCurrent(vm);

	if (lock->locked <= 1)
		return;


	if (s != lock->owner) {
		printf("FATAL in releaseLock: cannot unlock!\n");
		printf("\t request by: task %d\n", getID(s));
		printf("\t lock owner: task %d\n", getID(lock->owner));

		exit(0);
	}

	zm_trigger(vm, lock->ev, NULL);
}


void unlock_all(zm_VM *vm, Lock *lock)
{
	printf("unlock all locked tasks:\n");
	zm_unbindAll(vm, lock->ev, NULL);
}


Lock *newLock(zm_VM *vm)
{
	Lock* lock = malloc(sizeof(Lock));
	lock->ev = zm_newEvent(lock);
	zm_setEventCB(vm, lock->ev, acquirecb, ZM_TRIGGER);
	lock->owner = NULL;
	lock->locked = 0;
	return lock;
}

void freeLock(zm_VM *vm, Lock* lock)
{
	zm_freeEvent(vm, lock->ev);
	free(lock);
}


void open_resource()
{
	if (shared) {
		printf("RACE CONDITION ERROR: concurrent access to a "
		       "shared resource\n");
		exit(0);
	}

	shared = 1;
}

void close_resource()
{
	shared = 0;
}


ZMTASKDEF( mycoroutine )
{
	TaskData *self = zmdata;

	enum {INIT = 1, ACQUIRE, PROCESS, RELEASE, ABORT};

	ZMSTART

	zmstate INIT:
		zmdata = self = malloc(sizeof(TaskData));
		self->id = counter++;
		printf("    * task %d: -init-\n", self->id);
		zmyield ACQUIRE;

	zmstate ACQUIRE:
		printf("    * task %d: lock...\n", self->id);
		zmyield taskLOCK(lock, ABORT) | PROCESS;

	zmstate PROCESS:
		printf("    * task %d: lock aquired\n", self->id);
		printf("    * task %d: open shared resource\n", self->id);
		open_resource();
		zmyield RELEASE;

	zmstate RELEASE:
		printf("    * task %d: close shared resource\n", self->id);
		close_resource();

		if (self->id == 5) {
			printf("    * task 5: HERE WE ARE ... UNLOCK ALL\n");
			unlock_all(vm, lock);
		}

		printf("    * task %d: release lock\n", self->id);
		releaseLock(vm, lock);
		zmyield zmTERM;

	zmstate ABORT:
		printf("    * task %d: lock aborted\n", self->id);
		zmyield zmTERM;


	zmstate ZM_TERM:
		printf("    * task %d: -end-\n", ((self) ? (self->id) : -1));
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
			zm_State *s = zm_newTasklet(vm, mycoroutine, NULL);
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


