
# ZM:
ZM is a C library to handle [continuations](https://en.wikipedia.org/wiki/Continuation) ([coroutine](https://en.wikipedia.org/wiki/Coroutine), exception, [green thread](https://en.wikipedia.org/wiki/Green_threads)) with finite state machines.

The library is written in *C99* without external dependecy or 
machine-specific code but can also be compiled in *ansi-c* or *ansi-c++* 
with the minal effort to define two unsigned int type
(`uint8_t` and `uint32_t`).


This library is a part of a fulltextsearch-engine developed for a 
web-comic project: [vikbz](http://vikbz.com/).

## Portable:
*ZM* is implemented only with *c* control flow **without** any kind of 
assembly code or non-local-jumps functions like `setjump` and `ucontext`. 

Moreover library doesn't require any external or OS specific libraries.

## A little task with ZM:

	ZMTASKDEF(foo) {
		ZMSTART

		zmstate 1:
			printf("- step 1 - init\n");
			yield 2; /* yield to zmstate 2 */ 

		zmstate 3:
			printf("- step 3 - suspend\n");
			yield zmSUSPEND | 4; /* suspend and set resume 
			                        point to zmstate 4 */
		zmstate 2:
			printf("- step 2 - yield to 3\n");   
			yield 3; /* yield to zmstate 3 */

		zmstate 4:
			printf("- step 4 - yield to 2\n");
			yield 2; /* yield to zmstate 2 */

		ZMEND			
	}

This piece of code define the machine `foo` that rappresent a **task class**.

The code in this machine is split in 4 block labeled with 
the operator: `zmstate`. 

A *machine* allow to instance tasks: an instance of `foo` is a finite state 
machine with 4 state.

Every instance start with the `zmstate 1` and the execution of each zmstates 
is broken by yields that send info to a task manager on what come next.

For example in `zmstate 1` the command `yield 2` means:

- stop `zmstate 1`
- set next `zmstate` to `2`


To instance `foo` and run it:

		/* create a task manager instance */
		zm_VM *vm = zm_newVM("test");  
		
		/* create an instance of foo */
		zm_State* footask = zm_newTask(vm, foo, NULL);

		/* footask is supended: activate it */  
		zm_resume(vm , footask, NULL);

		/* run step by step */  
		while(zm_go(vm , 1)) {
			printf("-main-\n");
		}


This will produce the output:

	step 1 - init
	-main-
	step 2 
	-main-
	step 3
	-main-



## The idea behind ZM:
The idea behind ZM is to split code with `switch`-`case` 
and use `return` to yield to another piece of code. 

Example:


	ZMTASKDEF(foo) { 
		ZMSTART
	
		zmstate 1:
			printf("step 1 - init\n");
			yield 2; // yield to zmstate 2 

		zmstate 3:
			printf("step 3\n");
			yield zmSUSPEND | 2; // suspend and set resume 
			                         // point to zmstate=2

		zmstate 2:
			printf("step 2\n");
			yield 3

		ZMEND
	}


this will be converted in (semplificated version):

	int32 foo(zm_VM *zm, int zmop, zmdata) {
		switch(zmop) {
		case 1:
			printf("step 1 - init\n");
			return 2;

		case 3:
			printf("step 3\n");
			return TASK_SUSPEND | 2;

		case 2:
			printf("step 2\n");
			return 3

		default:
			if (zmop == ABORT)
				return TASK_END
			else
				fatalUndefState(zmop);
		}
	}


A `foo` instance return a 4 bytes integer: one byte rappresent a 
command for the task manager while others bytes are arguments for 
this command.

When  `footask` return the value 2, the command is 0 (inner yield) 
with the only argument 2. Task manager process returned command saving
the value 2 in a resume field that will be used as `zmop` in the next 
machine step.


The same concept allow to deal with external yield (yield to other tasks):

	yield zmTO(foo2task) | 4;

this is converted in:

	return resume_task(vm , foo2task) | 4;


where `resume_task` resume `foo2task` and return the command 
`TASK_SUSPEND`. 


Task manager get four kind of information from the int32 returned by 
yield-operator:

1. The vm  command (0 = inner yield)
2. normal resume zmstate 
3. iter resume zmstate
4. catch resume zmstate

The iter and catch resume values are shifted by 1 and 2 bytes with the macros:
`zmNEXT(n)` and `zmCATCH(n)` while zm-commands are 
costants greater than 0xFFFFFF. 

This allow to use them simultaneously, using *OR* operator, in a single yield:

	yield TASK_SUSPEND | 4 | zmNEXT(5) | zmCATCH(7);

The involved endianess issue is worked around with endianess-indipendent  
procedure (but can also forced defining: `ZM_LITTLE_ENDIAN` or `ZM_BIG_ENDIAN`).



## Other corotuine library:

A famous article with an implementation of coroutine in C by Simon Tatham can
be found [here](http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html).

Many popular coroutine library are listed [here](https://github.com/baruch/libwire/wiki/Other-coroutine-libraries).
