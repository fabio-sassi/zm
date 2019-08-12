
# ZM:

*ZM* is C library to handle concurrency throught finite state machines.

It allow to create many concurrent tasks with small footprints that
interact with events or other tasks.

Task execution is splitted in code chunks, this allow
to suspend, resume, run step by step but also 
raise, catch and also recover from an exceptions.

Library is written in C99 without external dependecy or
machine-specific code.

## Portable:

ZM is implemented only with *C* control flow **without** any kind of
assembly code or non-local-jumps functions like *setjump* and *ucontext*.

ZM can be compiled in C99 without any requirements or in *ansi-c*,
*ansi-c++* defining `uint8_t` and `uint32_t` if `stdint.h` is not
avaible.


## Self-Contained:

It doesn't require any external, specific or non-standard
libraries, everything is inside two files: *zm.h*, *zm.c*.

## Hello world:

	#include <stdlib.h>
	#include <zm.h>

	ZMTASKDEF( mytask )
	{
		ZMSTART

		zmstate 1:
			printf("my task: -init-\n");
			zmyield 2;

		zmstate 2:
			printf("my task: Hello world\n");
			zmyield zmTERM;

		zmstate ZM_TERM:
			printf("my task: -end-\n");

		ZMEND
	}


	int main()
	{
		zm_VM *vm = zm_newVM("test VM");
		zm_State *s = zm_newTask(vm, mytask, NULL);
		zm_resume(vm, s, NULL);

		while(zm_go(vm, 1, NULL))
			printf("(step)\n");

		zm_freeTask(vm, s);
		zm_freeVM(vm);

		return 0;
	}


Output:

	my task: -init-
	(step)
	my task: Hello world
	(step)
	my task: -end-
	(step)
	(step)

This example define the task class `mytask`, instance task `s` and execute
it step by step. The `zmyield` operator break task execution and define
the next operation as change *zmstate* or term task. The same operator
can also be used to: suspend, wait other task, wait event, catch exception,
undo an exception ...


## Hello (some) worlds:

	#include <stdlib.h>
	#include <string.h>
	#include <zm.h>


	ZMTASKDEF( He )
	{
		char *self = zmdata;

		enum { HELLO = 1, VENUS_NOTE };

		ZMSTART

		zmstate ZM_INIT:
			printf("open connection from world %s\n", self);
			zmdata = self = strdup(self);
			zmyield zmDONE;

		zmstate HELLO:
			printf(" Hello from %s!\n", self);

			if (self[0] == 'V')
				zmyield VENUS_NOTE;

			zmyield zmTERM;

		zmstate VENUS_NOTE:
			printf("note by %s: It's warm here!\n", self);
			zmyield zmTERM;

		zmstate ZM_TERM:
			printf("close connection from word %s\n", self);
			free(self);

		ZMEND
	}


	int main()
	{
		zm_VM *vm = zm_newVM("test VM");

		zm_resume(vm, zm_newTasklet(vm, He, "Earth"), NULL);
		zm_resume(vm, zm_newTasklet(vm, He, "Mars"), NULL);
		zm_resume(vm, zm_newTasklet(vm, He, "Venus"), NULL);
		zm_resume(vm, zm_newTasklet(vm, He, "Omicron Persei 8"), NULL);

		while(zm_go(vm, 1, NULL));

		zm_freeVM(vm);

		return 0;
	}

This example instance 4 task and execute it:


	open connection from world Earth
	open connection from world Mars
	open connection from world Venus
	open connection from world Omicron Persei 8
	 Hello from Earth!
	 Hello from Mars!
	 Hello from Venus!
	 Hello from Omicron Persei 8!
	close connection from word Earth
	close connection from word Mars
	note by Venus: It's warm here!
	close connection from word Omicron Persei 8
	close connection from word Venus




## The idea behind ZM:
The idea behind ZM is to split code with `switch`-`case`
and use `return` to send directives to task manager as the
next block of code to execute or what the current task must
wait for.

For more detail see [MANUAL.md](MANUAL.md).


## Feature:
- Instance and execute tasks (green threads) with small memory footprint.
- Instance and execute subtask (iterator, coroutine).
- Tasks can be suspended, resumed, wait other tasks or events.
- Task can raise and catch exceptions.
- Task can raise, catch and *unraise* continuable exception (return to 
  the point that signaled the exception).
- It is developed for event driven applications.


## Levin:

This library has been created to give a concurrency model
to Levin server. Levin is an event driven server that store
keys with approximate search cababilities.

ZM is implemented to handle concurrency in Levin project. Levin is an event
driven server that store keys with approximate search cababilities.



## References:

- [Levin](https://github.com/fabio-sassi/levin) 
- [Continuations](https://en.wikipedia.org/wiki/Continuation)
- [Coroutine](https://en.wikipedia.org/wiki/Coroutine)
- [Green thread](https://en.wikipedia.org/wiki/Green_threads)
- [VikBZ web comic](http://vikbz.com/).
- [an implementation of coroutine in C by Simon Tatham](http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html).
- [other coroutine libraries](https://github.com/baruch/libwire/wiki/Other-coroutine-libraries).




