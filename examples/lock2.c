#include <stdlib.h>
#include <zm.h>

/* An implementation of task lock (mutex) with owner check and abort lock */

#define NTASKS 8

#define taskLOCK(lock, abrt) \
	((taskLock(vm, (lock)) ? (0) : (zmEVENT(lock->ev) | zmUNBIND(abrt))))


typedef struct {
	zm_Event *ev;
	zm_State *owner;
	int locked;
} Lock;


typedef struct {
	int id;
	Lock *lock;
} TaskData;


int shared = 0;
int counter = 1;



int getID(zm_State *s)
{
	if (!s)
		return -1000;

	return ((TaskData*)(s->data))->id;
}


int acquireCB(int context, zm_Event* event, zm_State *s, void *arg)
{
	Lock *lock = (Lock*)(event->data);

	if (context & ZM_UNBIND) {
		if (!s) {
			printf("event-callback: before free event\n");
			free(lock);
			return 0;
		}

		printf("event-callback: force unbind task %d\n", getID(s));
		return 0;
	}

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


	lock->owner = s;
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


Lock *newLock(zm_VM *vm)
{
	Lock* lock = malloc(sizeof(Lock));
	lock->owner = NULL;
	lock->locked = 0;
	lock->ev = zm_newEvent(acquireCB, lock);
	return lock;
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

	enum {INIT = 1, ACQUIRE, PROCESS, RELEASE, ABORT};

	ZMSTART

	zmstate INIT:
		zmdata = self = malloc(sizeof(TaskData));
		self->id = counter++;
		self->lock = (Lock*)zmarg;

		printf("    * task %d: -init-\n", self->id);
		zmyield ACQUIRE;

	zmstate ACQUIRE:
		printf("    * task %d: lock...\n", self->id);
		zmyield taskLOCK(self->lock, ABORT) | PROCESS;

	zmstate PROCESS:
		printf("    * task %d: lock aquired\n", self->id);
		printf("    * task %d: open shared resource\n", self->id);
		openResource();
		zmyield RELEASE;

	zmstate RELEASE:
		printf("    * task %d: close shared resource\n", self->id);
		closeResource();

		if (self->id == 5) {
			printf("    * task 5: !!unbind all tasks!!\n");
			zm_unbind(vm, self->lock->ev, NULL, NULL);
		}

		printf("    * task %d: release lock\n", self->id);
		releaseLock(vm, self->lock);
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
	zm_VM *vm;
	Lock* lock;
	int i, j;

	vm = zm_newVM("test VM");
	lock = newLock(vm);

	for (j = 1; j <= 2; j++) {
		printf("---------- %d/2 ----------\n", j);

		for (i = 0; i < NTASKS; i++) {
			zm_State *s = zm_newTasklet(vm, mytask, NULL);
			zm_resume(vm, s, lock);
		}

		while(zm_go(vm, 1, NULL));
	}


	zm_closeVM(vm);
	zm_go(vm, 1000, NULL);
	zm_freeVM(vm);

	zm_freeEvent(vm, lock->ev);

	return 0;
}


