#include <stdlib.h>
#include <zm.h>


typedef struct {
	int id;
} TaskData;

typedef struct {
	zm_Event *ev;
	int locked;
} Lock;

#define NTASKS 4

int shared = 0;

int counter = 1;

Lock* lock;


#define LOCK(lock) \
	((acquire((lock), zm_getCurrentState(vm)) ? (0) : (zmEVENT(lock->ev))))


int getID(zm_State *s)
{
	return ((TaskData*)(s->data))->id;
}


int acquirecb(zm_VM *vm, int scope, zm_Event* event, zm_State *s, void **arg)
{
	Lock *lock = (Lock*)(event->data);

	if (!s) {
		/* trigger pre fetch */
		if (lock->locked <= 1) {
			lock->locked = 0;
			printf("\tlocked task = 0\n");
			return ZM_EVENT_REFUSED;
		}

		return ZM_EVENT_ACCEPTED;
	}

	printf("\ttask %d acquire lock (n=%d)\n", getID(s), lock->locked);

	lock->locked--;

	return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
}


int acquire(Lock *lock, zm_State *s)
{
	if (lock->locked) {
		printf("\ttask %d wait\n", getID(s));
		lock->locked++;
		return 0;
	}

	printf("\ttask %d acquire lock (first)\n", getID(s));

	lock->locked = 1;

	return 1;
}

void unlock(zm_VM *vm, Lock *lock)
{
	zm_trigger(vm, lock->ev, NULL);
}

Lock *newLock()
{
	Lock* lock = malloc(sizeof(Lock));
	lock->ev = zm_newEvent(acquirecb, ZM_TRIGGER, lock);
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

	ZMSTART

	zmstate 1:
		self = malloc(sizeof(TaskData));
		self->id = counter++;
		zmData(self);
		printf("* task %d: -init-\n", self->id);
		zmyield 2;

	zmstate 2:
		printf("* task %d: lock...\n", self->id);
		zmyield LOCK(lock) | 3;

	zmstate 3:
		printf("* task %d: lock aquired\n", self->id);
		open_resource();
		zmyield 4;

	zmstate 4:
		printf("* task %d: unlock\n", self->id);
		close_resource();
		unlock(vm, lock);
		zmyield zmTERM;

	zmstate ZM_TERM:
		printf("* task %d: -end-\n", ((self) ? (self->id) : -1));
		if (self)
			free(self);

	ZMEND
}


int main() {
	int i, j;
	zm_VM *vm = zm_newVM("test VM");

	lock = newLock();

	for (j = 0; j < 2; j++) {
		for (i = 0; i < NTASKS; i++) {
			zm_State *s = zm_newTasklet(vm, mycoroutine, NULL);
			zm_resume(vm, s, NULL);
		}

		while(zm_go(vm, 1));
		if (j == 0)
			printf("\n ------------------ \n\n");
	}


	zm_closeVM(vm);
	zm_go(vm, 1000);
	zm_freeVM(vm);

	freeLock(vm, lock);

	return 0;
}

