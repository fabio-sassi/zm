
#ZM:
ZM is a C library to handle *continuations* (coroutine, 
exception, green thread) with finite state machines.

The library is written in *C99* without external dependecy or 
machine-specific code and can be compiled in *ansi-c* or *ansi-c++* 
with the minal effort to define two unsigned int type
(`uint8_t` and `uint32_t`).

*ZM* is a part of a fulltextsearch-engine developed for a 
web-comic project: [vikbz](http://vikbz.com/).


##A little task with ZM:

	ZMTASKDEF(foo) ZMSTATES
		zmstate 1:
			printf("step 1 - init\n");
			yield 2; /* yield to zmstate 2 */ 

		zmstate 3:
			printf("step 3\n");
			yield zmSUSPEND | 4; /* suspend and set resume 
			                        point to zmstate 4 */

		zmstate 2:
			printf("step 2\n");   
			yield 3; /* yield to zmstate 3 */

		zmstate 4:
			printf("step 4\n");
			yield 2; /* yield to zmstate 2 */

	ZMEND			


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
		zm_resume(vm , footask);

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



##The idea behind ZM:
The idea behind ZM is to split code with `switch`-`case` 
and use `return` to yield to another piece of code. 

Example:


	ZMTASKDEF(foo) ZMSTATES
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

##Command context and case convention:

The library define some operator, functions and macro to write and control
a task class. 

The main case convention is: commands relative to task class context 
only don't have underscore after *zm* prefix, while commands that can 
be used in every context have it.

###Task-class context:

1. **ZMABC**: task-class-define macro operator.
   - `ZMTASKDEF`
   - `ZMSTATES`
   - `ZMEND`
2. **zmAbcXyz**: inside (only) task functions:
    - `zmNewSubTask()`
	- `zmCatch()` ...
3. **zmabc**: inside task operator and library variable.
    - `zmyield` (`yield` with fast syntax)
    - `zmstate` (`zmstate` with fast syntax)
    - `zmraise` (`raise` with fast syntax)
    - `zmdata`
    - `zmop`
4. **zmABC**: yield-operators, must be placed after the `yield`:
   - `zmSUB()`
   - `zmNEXT()`
   - `zmCATCH()`
   - `zmTERM` ...

###Generic context:

1. **zm\_abcDefg**: functions that can be used inside or outside task 
   definition.
    - `ZM_resume()`
    - `ZM_newTask()`
    - `ZM_trigger()`
	- `ZM_abort()` ...
2. **zm\_ABC\_XYZ**: library constant
   - `ZM_INIT`
   - `ZM_TERM`, ...
3. **zm\_AbcXyz**: library structure 
   - `zm_State`
   - `zm_Event`, ...



#The Core:
ZM use a finite state machine to handle *concurrency*. The task manager (also 
called virtual stack mapper or *vm*) cycle throught active tasks and 
process them using the function (*machine*) associated to the task (in the 
example above `foo`).  

ZM support three kind of *continuations*:

- ptask: is a process task (green thread). 
- subtask: is a task child of a ptask or another subtask.
- exception: like exception in object oriented language. 

Task can also interact with virtual event.

## Task:
A **ptask** or *process task* is like a [green thread](https://en.wikipedia.org/wiki/Green_threads) while **subtasks** are like functions (can be nested 
too) invoked by ptask or by another subtasks.

The main difference between ptask and subtask is about execution:

**ptask execution is indipended** while **subtask execution is dependent**

When a task yield to a subtask the task manager keep track of this: it 
store the reference of the current task (caller) in subtask.
The caller is suspended in a busy-waiting mode: when the resumed subtask
yield to end or suspend, the caller will be informed about this and resumed. 

This is the same behaviour of a function from an abstracted point of view:

1. stop the current code
2. active function code
3. wait the function processing 
4. resume the code after the function call

Ptask, on the other side, have indipendent executions: task manager 
don't keep any track about who resume a ptask. 
This is true also if the resume is accomplished with `zmTO` macro.

NOTE: in this document *task* refer to *generic task* to avoid to 
repeat every time "ptask and/or subtask".

###Resume a task:
The different behaviour between ptask and subtask implies that a task 
can resume:

- only a subtask at a time and only within `yield` operator 
- many ptask and also outside from `yield` operator.

```
	zm_resume(task1); 
	zm_resume(task2); 
	zm_resume(task3); 
	yield zmSUB(subtask) | 5
```

###Suspend a task:
Yield to suspend for ptasks implies only a suspend while for a subtasks 
mean also the automaticaly resume of the task that are wainting for it.

	yield zmSUSPEND | 5;


### Execution-context:
A ptask and the set of its subtasks create an **execution-context**. 
In an execution-context only one between root-ptask or child-subtasks 
can be active at a specific time. 


Any operation perfomed inside the same execution-context can be splitted 
between different tasks and zmstates but is syncronous.


### Execution-stack:
Inside an execution-context a ptask can yield to a subtask and subtask to 
other subtasks. This can be rappresented as a stack: the **execution-stack**.

Any elements in the execution-stack, exept the last, 
are waiting another subtask.


### Data-tree:

The *execution-stack* keep track of yields but there is another important
stored relation: the association between a task and its subtasks:

**each task keep the list of its subtask instances**

The set of this relations can be represented as a tree with the ptask as 
root and the subtask as branches and leaves: the **data-tree**.

If user-task-data allocation follow the *data-tree* structure from root to 
leaves direction, the deallocation must follow the same structure in the 
reversed direction.

The main purpose of *data-tree* is to deal with the task resource deallocation.


### Task definition syntax:

Each task (ptask or subtask) have this structure:


	ZMTASKDEF(foo) 
	
	/* [global definition] */

	ZMSTATES
	
		zmstate 1:
			/* [code]  */
			
	ZMEND			

The *\[global definition\]* is an header where is possibile to define 
variable or write piece of code that will executed before any zmstate.

The zmstate 1 is the default resume point for every new instanced task.
For this reason it must always be present and rappresent the init zmstate.

The library have a constant that can be used in place of 
the numerical value: `ZM_INIT`.

This zmstate can be used as a constructor to allocate the resource for
the task. On the other side the zmstate that define the end of a 
zmstate is `ZM_TERM`:

	ZMTASKDEF(foo) ZMSTATES
	
		zmstate ZM_INIT: 
			/* [code] */	
			zmyield zmTERM;

		zmstate ZM_TERM: /* distructor [optional] */
			/* [code] */	
			yield zmEND;

	ZMEND 


ZMTASKDEF(x) is a macro that create a pointer to `zm_Machine` named 
`x`. This machine is associated to the function defined after this macro. 

The function have 3 important parameters that can be used inside the 
task definition:

- **vm**: the task manager instance
- **zmdata**: is the data associated to a task instance (can defined inside
  task or as the last parameter in `zm_newTask` and `zmNewSubTask`)
- **zmop**: is the current zmstate


####Other syntax:

There are some equivalent syntax for example this task:

	ZMTASKDEF(foo2)
		ZMVMSTATES
		zmstate 1: 
			zmyield zmTERM; 
	ZMEND

can be written in a bit more *C* style as:

	ZMTASKDEF(foo2) {
		ZMSTATES
		zmstate 1: 
			zmyield zmTERM; 
	ZMEND }

or enabling `ZM_FAST_SYNTAX` as:

	TASKDEF(foo2)
		VMSTATES
		zmstate 1: 
			yield zmTERM; 
	TASKEND


### Instance tasks:

To instance a new task:

	/* instance a ptask relative to machine foo */
	zm_State *task = zm_newTask(vm , foo, userdata);

	/* instance a subtask relative to machine foo2 (can be done 
	   only inside a ZMTASKDEF and vm parameter is implicit) */
	zm_State *subtask = zmNewSubTask(machinename, userdata);


### Close tasks:

When a task receive a *close* command it changes its operational mode
from *normal-mode* (default) to **closing-mode**.

This is the list of command that send a *close* to a task:

- `zm_abort(...)` 
- `yield zmTERM` 
- `yield zmCLOSE(...)`
- `raise zmERROR(...)`

This commands affect not only the target task but also its subtask and
recursively all relative *data-tree* branch. 

There are two important feature of the close system: 

*The order of execution of the close is from leaves to root (implosion).*

The allocation order of subtask resource is from root to leaves for this 
reason close use the inverse order.

*The execution of the close is done in a serial way as any other operation*
*in the same execution-context.*

This avoid race-condition during resource free.


###Free tasks:

A task can be free only if it have just receive a *close*. The commands to
free a task are:

	/* free a ptask */
	zm_freeTask(vm, task1);

	/* free a subtask */
	zm_freeSubTask(vm, subtask2);

The free commands *cannot be perfomed in a sync way* because the task manager
should have to finish close operation before a task can be freed.


###Tasklet:

A tasklet is a task that have not to be manually free, it will automatically 
free after the close operations.

	/* instance a ptasklet relative to machine foo */
	zm_State *tmp = zm_newTasklet(vm , foo, userdata);

	/* instance a subtasklet relative to machine foo2 */
	zm_State *tmp = zmNewSubTasklet(foo2, userdata);

The automatically free implies that it's not safe to store a tasklet 
pointer because can point to a free area of memory.

Tasklets are usefull to perform one-shot operations: operations
without any suspend.

In one-shot operations where no reference is needed, tasklet can be used
in place of task.



###The task distructor:
When a task go in **closing-mode** the task will resumed in `ZM_TERM` 
zmstate.

`ZM_TERM` is the special zmstate to perform all close operation 
on task data (e.g. task data resource free) before yield to end.

The only permitted yield in `ZM_TERM` is: 

	yield zmEND;

This yield is permitted also in *normal-mode* and allow to bypass 
`ZM_TERM`.


###Yield task example:
This example show the difference between yield to subtask and yield
to ptask:

	#define ZM_FAST_SYNTAX 1
	#include <zm.h>


	/* A task definition rappresent a generic task so can be instanced 
	   as a ptask or as a subtask */
	TASKDEF(task2) ZMSTATES
		zmstate 1:
			printf("    task2: init\n");
			yield zmTERM;
			
		zmstate ZM_TERM:
			printf("    task2: end\n");
			yield zmEND;
	TASKEND			


	TASKDEF(task1) ZMSTATES
		zmstate 1:
			printf("task1: yield to sub\n");
			/* this yield suspend task1 but subtask task2 will resume it
			   yielding to end */
			yield zmSUB(zmNewSubTasklet(task2, NULL)) | 2;

		zmstate 2: 
			printf("task1: yield to ptask\n");
			/* this yield suspend task1 and active task2 */
			yield zmTO(zm_newTasklet(vm ,task2, NULL)) | 3;

		zmstate 3:
			printf("task1: term\n");
			yield zmTERM;

	TASKEND			


	void main() {
		zm_VM *vm = zm_newVM("test");
		zm_State* t = zm_newTask(vm , task1, NULL);
		zm_resume(vm , t);
		
		printf("#main: process all jobs...\n"); 
		while(zm_go(vm , 1)) {}
		
		printf("#main: no more to do...not completly true\n"); 
		printf("#main: resume task1 instance\n"); 
		zm_resume(vm , t);
		while(zm_go(vm , 1)) {}
		
		printf("#main: now there no more to do!\n"); 
	}

output:

	#main: process all jobs...
	task1: yield to sub
	    task2: init
	    task2: end
	task1: yield to ptask
	    task2: init
	    task2: end
	#main: no more to do...not completly true
	#main: resume task1 instance
	task1: term
	#main: now there no more to do!




## Exception:

There are two kind of exception:

- **error exception**: zmERROR
- **continue exception**: zmCONTINUE

Exception feature and rule:

- Exceptions can be raised only inside a subtask and can be catch 
  in other subtasks or in the root-ptask.
- An exception without a catch will cause a fatal.
- An caught exception must be free before the next yield. 


####Error Exception:
Error exception close all the task between the raise and the task 
before the catch if an exception-reset is not set.

Example:

	ZMTASKDEF( subtask2 ) ZMSTATES
		
		zmstate 1:
			printf("\t\tsubtask2: init\n");
			zmraise zmERROR(0, "example message", NULL);

		zmstate 2:
			printf("\t\tsubtask2: TERM");
			zmyield zmTERM;
			
	ZMEND



	ZMTASKDEF( subtask ) ZMSTATES
		
		zmstate 1: {
			zm_State *s = zmNewSubTasklet(subtask2, NULL);
			printf("\tsubtask: init\n");
			zmyield zmSUB(s) | 2;
		}
		zmstate 2:
			printf("\tsubtask: TERM");
			zmyield zmTERM;
			
	ZMEND



	ZMTASKDEF( task ) ZMSTATES
		
		zmstate 1: {
			zm_State *s = zmNewSubTasklet(subtask, NULL);
			printf("task: yield to subtask\n");
			zmyield zmSUB(s) | 2 | zmCATCH(2);
		}

		zmstate 2: {
			zm_Exception *e = zmCatch();
			if (e) {
				printf("task: catch exception\n");
				if (zmIsError(e))
					zmPrintError(stdout, e, 1);
				zmFreeException();
				zmyield zmTERM;
			}
			printf("task: end\n");
			zmyield zmTERM;
		}
	ZMEND 




	void main() {
		zm_VM *vm = zm_newVM("test ZM");
		zm_resume(vm , zm_newTasklet(vm , task, NULL));
		zm_go(vm , 100);
		zm_closeVM(vm );
		zm_go(vm , 100);
		zm_freeVM(vm );
	}


output:

	task: yield to subtask
		subtask: init
			subtask2: init
	task: catch exception
	Exception:
	   (0) "example message"
	 in: subtask2 (file: exception_catch.c at line: 12)

	Trace: 
	   machine: subtask	[zmstate: 2]
	   filename: exception_catch.c
	   nline: 27
	   --------------------
	   machine: subtask2	[zmstate: 1]
	   filename: exception_catch.c
	   nline: 12



####Exception Reset:
*Exception-reset* allow to avoid the exception-error close beaviour, 
defining a zmstate as resume point.

The reset point is defined with this syntax:

	yield 4 | zmRESET(7)

To define a resent point in the raise itself use instead: 

	raise zmERROR(0, "test", NULL) | 5;


####Continue Exception:
Continue exception have a very different beavior from error exception. 

*A continue exception is a kind of (big) suspend-resume block.*

The continue exception suspend the execution of a subtask in the raise 
state and resume the state with the catch.

In this situations all the subtask between the raise and the subtask
before catch are a suspended block. 

This block can be resumed using:

	yield zmUNRAISE(sub1) | 5;
	yield zmSSUB(sub1) | 5;

This commands *don't resume* `sub1` but the state who raised the 
continue exception (a child sub1).

Example:

	ZMTASKDEF(task3) ZMSTATES
		zmstate 1:
			printf("task3: init\n");
			raise zmCONTINUE(0, "test", NULL) | 2;
		zmstate 2:
			printf("task3: term\n");
			yield TERM
	ZMEND			

	ZMTASKDEF(task2) ZMSTATES
		zmstate 1:{
			printf("task2: init\n");
			zm_State *s = zmNewSubTasklet(task3, NULL);
			yield zmSUB(s) | 2;
		}
		zmstate 2: 
			printf("task2: term\n");
			yield zmTERM;
	ZMEND			

	zm_State *sub2;

	ZMTASKDEF(task1) ZMSTATES
		zmstate 1: {
			printf("task1: init\n");
			sub2 = zmNewSubTasklet(task2, NULL);
			yield zmSUB(s) | 2 | zmCATCH(3);
		}
		zmstate 2: 
			printf("task1: term\n");
			yield zmTERM;
		zmstate 3: {
			printf("task1: catch\n");
			zm_Exception* e = zmCatch();
			if (e)
				zmFreeException();
			yield 4;
		}
		zmstate 4: 
			printf("task1: 1/2\n");
			yield 5;
		zmstate 5: 
			printf("task1: 2/2\n");
			// this resume the raise subtask: task3
			yield zmUNRAISE(sub2) | 2;
	ZMEND			

	void main() {
		zm_VM *vm = zm_newVM("test");
		zm_resume(vm , zm_newTasklet(vm , task1, NULL));
		zm_go(vm , 100);
	}

That produce this output:

	task1: init
	task2: init
	task3: init
	task1: catch
	task1: 1/2
	task1: 2/2
	task3: term 
	task2: term 
	task1: term 

## EVENT:
Virtual event that can keep a task in a waiting state 
until a trigger resume the task.

**[WORK IN PROGRESS: I will complete and correct this README as soon as possible** 


##Other corotuine library:

A famous article with an implementation of coroutine in C by Simon Tatham can
be found [here](http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html).

Some other popular coroutine library that use machine specific code are listed 
in [Wikipedia page](https://en.wikipedia.org/wiki/Coroutine#Implementations_for_C)


