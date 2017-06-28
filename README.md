
# ZM:
ZM is a C library to handle [continuations](https://en.wikipedia.org/wiki/Continuation) ([coroutine](https://en.wikipedia.org/wiki/Coroutine), exception, [green thread](https://en.wikipedia.org/wiki/Green_threads)) with finite state machines.

The library is written in *C99* without external dependecy or 
machine-specific code but can also be compiled in *ansi-c* or *ansi-c++* 
with the minal effort to define two unsigned int type
(`uint8_t` and `uint32_t`).


This library is a part of a full text search event driven engine
developed for a web-comic project: [vikbz](http://vikbz.com/).

## Portable:
*ZM* is implemented only with *c* control flow **without** any kind of 
assembly code or non-local-jumps functions like `setjump` and `ucontext`. 

Moreover library doesn't require any external or OS specific libraries.

## A little task with ZM:

	ZMTASKDEF(foo) {
		ZMSTART

		zmstate 1:
			printf("- step 1 - init\n");
			zmyield 2; /* yield to zmstate 2 */ 

		zmstate 3:
			printf("- step 3 - suspend\n");
			zmyield zmSUSPEND | 4; /* suspend and set resume 
			                        point to zmstate 4 */
		zmstate 2:
			printf("- step 2 - yield to 3\n");   
			zmyield 3; /* yield to zmstate 3 */

		zmstate 4:
			printf("- step 4 - yield to 2\n");
			zmyield 2; /* yield to zmstate 2 */

		ZMEND			
	}

This piece of code define the machine `foo` that rappresent a **task class**
or **machine**.

The code in this task class is split in 4 blocks (`zmstate`). 
The `zmyield` operator suspend the current task execution and 
send a command to the task manager.

For example `zmyield 2` in `zmstate 1` means:

- stop `zmstate 1`
- set next `zmstate` to `2`


To instance `foo` and execute it:

		/* create a task manager instance */
		zm_VM *vm = zm_newVM("test");  
		
		/* create an instance of foo */
		zm_State* footask = zm_newTask(vm, foo, NULL);

		/* footask is supended: resume it */  
		zm_resume(vm , footask, NULL);

		/* run step by step */  
		while(zm_go(vm , 1)) {
			printf("-main-\n");
		}


output:

	step 1 - init
	-main-
	step 2 
	-main-
	step 3
	-main-

## ZM Feature:
- Instance many task (or coroutine) with small memory overhead.
- Process many task concurrently (with finite state machine engine).
- Process async-task (green thread) and sync-task (coroutine).
- Yield to other tasks (in suspend or busy-waiting-response mode).
- Yield back to the task caller.
- Yield to events.
- Suspend, resume, term a task.
- Raise error exceptions.
- Raise continue exception: this freeze all tasks between the 
  raise and the catch point and threats this freezed block as a single 
  suspended task that can be resumed with an *unraise* operation.
- It is developed keeping in mind an event driven model and C10K problem
  (but it has never been tested in a real heavy multi concurrent enviroment).

## The idea behind ZM:
The idea behind ZM is to split code in `switch`-`case` blocks 
and use `return` to yield to others blocks.

ZM extend this schema to make possible more advanced features. 

For more detail see [MANUAL.md](MANUAL.md).

## Other corotuine library:

A famous article with an implementation of coroutine in C by Simon Tatham can
be found [here](http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html).

Many popular coroutine library are listed [here](https://github.com/baruch/libwire/wiki/Other-coroutine-libraries).
