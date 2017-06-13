
# The Core:
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

### Resume a task:
The different behaviour between ptask and subtask implies that a task 
can resume:

- only a subtask at a time and only within `zmyield` operator 
- many ptask with `zm_resume` (or one with `zmTO` within `zmyield`)

```
	zm_resume(vm, task1, NULL); 
	zm_resume(vm, task2, NULL); 
	zm_resume(vm, task3, NULL); 
	yield zmSUB(subtask, NULL) | 5
```

### Suspend a task:
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

#Using ZM:

## Command context and case convention:

The library define some operator, functions and macro to write and control
a task class. 

The main case convention is: commands relative to task class context 
only don't have underscore after *zm* prefix, while commands that can 
be used in every context have it.

### Task-class context:

1. **ZMABC**: task-class-define macro operator.
   - `ZMTASKDEF`
   - `ZMSTART`
   - `ZMEND`
2. **zmAbcXyz**: inside (only) task functions:
    - `zmNewSubTask()`
	- `zmCatch()` ...
3. **zmabc**: inside task operator and library variable.
    - `zmyield` (`yield` with fast syntax)
    - `zmraise` (`raise` with fast syntax)
    - `zmstate` 
    - `zmdata`
    - `zmop`
4. **zmABC**: yield-operators, must be placed after the `yield`:
   - `zmSUB()`
   - `zmNEXT()`
   - `zmCATCH()`
   - `zmTERM` ...

### Generic context:

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




### Task definition syntax:

Each task (ptask or subtask) have this structure:


	ZMTASKDEF(foo) { 
	
		/* [global definition] */

		ZMSTART
		
		zmstate 1:
			/* [code]  */

		ZMEND
	}

The *\[global definition\]* is an header where is possibile to define 
variable or write piece of code that will executed before any zmstate.

The zmstate 1 is the default resume point for every new instanced task.
For this reason it must always be present and rappresent the init zmstate.

The library have a constant that can be used in place of 
the numerical value: `ZM_INIT`.

This zmstate can be used as a constructor to allocate the resource for
the task. On the other side the zmstate that define the end of a 
zmstate is `ZM_TERM`:

	ZMTASKDEF(foo) {
	
		ZMSTART
	
		zmstate ZM_INIT: 
			/* [code] */	
			zmyield zmTERM;

		zmstate ZM_TERM: /* distructor [optional] */
			/* [code] */	
			yield zmEND;

		ZMEND 
	}


ZMTASKDEF(x) is a macro that create a pointer to `zm_Machine` named 
`x`. This machine is associated to the function defined after this macro. 

The function have 4 important parameters that can be used inside the 
task definition:

- **vm**: the task manager instance
- **zmdata**: is the data associated to a task instance (can defined inside
  task or as the last parameter in `zm_newTask` and `zmNewSubTask`)
- **zmop**: is the current zmstate
- **zmarg**: is the argument passed to the resume function (`zm_resume`,
  `zmSUB`, `zmUNRAISE`, ...)


#### Other syntax:

There are some equivalent syntax in task class definition for example `zmyield` 
can be repleaced with `yield` (defining `ZM_FAST_SYNTAX`) and `ZMSTATES` 
can be used in place of `ZMSTART`. Moreover `ZMTASKDEF` and `ZMEND` 
macros implicit use curly braces `{}`. All this features allow to define 
many syntax combinations, for example the task `foo2`:

	ZMTASKDEF(foo2) 
	{
		ZMSTART
		zmstate 1: 
			zmyield zmTERM; 
		ZMEND
	}

can also be written as:

	ZMTASKDEF(foo2) ZMSTATES
		zmstate 1: 
			yield zmTERM; 
	ZMEND


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


### Free tasks:

A task can be free only if it have just receive a *close*. The commands to
free a task are:

	/* free a ptask */
	zm_freeTask(vm, task1);

	/* free a subtask */
	zm_freeSubTask(vm, subtask2);

The free commands *cannot be perfomed in a sync way* because the task manager
should have to finish close operation before a task can be freed.


### Tasklet:

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



### The task distructor:
When a task go in **closing-mode** the task will resumed in `ZM_TERM` 
zmstate.

`ZM_TERM` is the special zmstate to perform all close operation 
on task data (e.g. task data resource free) before yield to end.

The only permitted yield in `ZM_TERM` is: 

	yield zmEND;

This yield is permitted also in *normal-mode* and allow to bypass 
`ZM_TERM`.


### Yield to task example:
This example show the difference between yield to subtask `zmSUB` and yield
to ptask `zmTO`:

	#define ZM_FAST_SYNTAX 1
	#include <zm.h>

	/* A task definition rappresent a generic task so can be instanced
	   as a ptask or as a subtask */
	ZMTASKDEF(task2) ZMSTART
		zmstate 1:
			printf("    task2: init\n");
			yield zmTERM;

		zmstate ZM_TERM:
			printf("    task2: end\n");
			yield zmEND;
	ZMEND


	ZMTASKDEF(task1) ZMSTART
		zmstate 1:
			printf("task1: yield to sub\n");
			/* this yield suspend task1 but subtask task2 will resume it
			   yielding to end */
			yield zmSUB(zmNewSubTasklet(task2, NULL), NULL) | 2;

		zmstate 2:
			printf("task1: yield to ptask\n");
			/* this yield suspend task1 and active task2 */
			yield zmTO(zm_newTasklet(vm,task2, NULL)) | 3;

		zmstate 3:
			printf("task1: term\n");
			yield zmTERM;

	ZMEND


	int main() {
		zm_VM *vm = zm_newVM("test");
		zm_State* t = zm_newTask(vm, task1, NULL);
		zm_resume(vm, t, NULL);

		printf("#main: process all jobs...\n");
		while(zm_go(vm, 1)) {}

		printf("#main: no more to do...sure?\n");
		zm_resume(vm, t, NULL);
		while(zm_go(vm, 1)) {}

		printf("#main: now there is no more to do\n");
		zm_freeTask(vm, t);
		zm_closeVM(vm);
		zm_go(vm, 100);
		zm_freeVM(vm);
		return 0;
	}

output:

	#main: process all jobs...
	task1: yield to sub
	    task2: init
	    task2: end
	task1: yield to ptask
	    task2: init
	    task2: end
	#main: no more to do... sure?
	task1: term
	#main: now there no more to do




## Exception:


### Raise: 

There are two kind of exception in ZM:

- **error exception**
- **continue exception**

An exception is raised with `zmraise` operator followed by:

1. `zmERROR(int code, const char* msg, void *data)` for error exception
2. `zmCONTINUE(int code, const char* msg, void *data)` for continue exception

### Catch:

The exception catching is perfomed declaring the exception-zmstate
resume point with `zmCATCH` (equals to "try") and fetching the exception
with `zmCatch` in the exception-zmstate (equals to "catch":

	zmstate 1:
		zmyield zmSUB(sub1, NULL) | 2 |  zmCATCH(3);

	zmstate 2:
		printf("sub1 perfomed without any exception");
		zmyield zmTERM;

	zmstate 3: {
		zm_Exception* e = zmCatch();
		if (e)
			printf("exception: code=%d msg=%s", e->code, e->msg);
		zmyield zmTERM;
	}


###Exception feature and rule:

- Exceptions can be raised only inside a subtask and can be catch 
  in other subtasks or in the root-ptask.
- An error-exception without a catch, cause `zm_go` to return immediatly
  `ZM_RUN_EXCEPTION`. The relative exception *must* be catch
  with `zm_ucatch`.
- A continue-exception without a catch will cause a fatal.
- In exception-zmstate, exception must be catch with `zmCatch()`
  (before the next yield). 


#### Error Exception:
Raising an error-exception implies the closing of all tasks between the 
raise and the task before the catch if an exception-reset is not set.

Example:

	ZMTASKDEF( subtask2 ) ZMSTART
		
		zmstate 1:
			printf("\t\tsubtask2: init\n");
			zmraise zmERROR(0, "example message", NULL);

		zmstate 2:
			printf("\t\tsubtask2: TERM");
			zmyield zmTERM;
			
	ZMEND



	ZMTASKDEF( subtask ) ZMSTART
		
		zmstate 1: {
			zm_State *s = zmNewSubTasklet(subtask2, NULL);
			printf("\tsubtask: init\n");
			zmyield zmSUB(s, NULL) | 2;
		}
		zmstate 2:
			printf("\tsubtask: TERM");
			zmyield zmTERM;
			
	ZMEND



	ZMTASKDEF( task ) ZMSTART
		
		zmstate 1: {
			zm_State *s = zmNewSubTasklet(subtask, NULL);
			printf("task: yield to subtask\n");
			zmyield zmSUB(s, NULL) | 2 | zmCATCH(2);
		}

		zmstate 2: {
			zm_Exception *e = zmCatch();
			if (e) {
				printf("task: catch exception\n");
				if (zmIsError(e))
					zm_printError(NULL, e, 1);
				zmyield zmTERM;
			}
			printf("task: end\n");
			zmyield zmTERM;
		}
	ZMEND 




	void main() {
		zm_VM *vm = zm_newVM("test ZM");
		zm_resume(vm , zm_newTasklet(vm , task, NULL), NULL);
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



#### Error Exception Reset:
*Error-reset* allow to avoid the exception-error close beaviour, 
defining a zmstate as resume point.

The reset point is defined with this syntax:

	yield 4 | zmRESET(7)

To define a resent point in the raise itself use instead: 

	raise zmERROR(0, "test", NULL) | 5;

zmRESET cannot be applied to a ptask.

#### Continue Exception:
Continue exception have a very different beavior from error exception. 

*A continue exception is a kind of (big) suspend-resume block.*

The continue exception suspend the execution of a subtask in the raise 
state and resume the state with the catch.

In this situation all the subtask between the raise and the subtask
before catch are a suspended block.

This block can be resumed using:

	yield zmUNRAISE(sub1, NULL) | 5;
	yield zmSSUB(sub1, NULL) | 5;

This commands *don't resume* `sub1` but the state who raised the 
continue exception.

Example:
	zm_State *sub2;

	ZMTASKDEF(task3)
	{
		ZMSTART

		zmstate 1:
			printf("\t\ttask3: init\n");
			printf("\t\ttask3: stop this task by raising continue (*)\n");
			zmraise zmCONTINUE(0, "[continue exception test]", NULL) | 2;

		zmstate 2:
			printf("\t\ttask3: (*) unraised ... OK\n");
			printf("\t\ttask3: msg = `%s`\n", (const char*)zmarg);

		zmstate 3:
			printf("\t\ttask3: no more to do ... term\n");
			zmyield zmTERM;

		ZMEND
	}


	ZMTASKDEF(task2)
	{
		ZMSTART

		zmstate 1:{
			zm_State *s = zmNewSubTasklet(task3, NULL);
			printf("\ttask2: init\n");
			zmyield zmSUB(s, NULL) | 2;
		}

		zmstate 2:
			printf("\ttask2: term\n");
			zmyield zmTERM;

		ZMEND
	}


	ZMTASKDEF(task1)
	{
		ZMSTART

		zmstate 1:
			sub2 = zmNewSubTasklet(task2, NULL);
			printf("task1: init\n");
			zmyield zmSUB(sub2, NULL) | 2 | zmCATCH(3);

		zmstate 2:
			printf("task1: term\n");
			zmyield zmTERM;

		zmstate 3: {
			zm_Exception* e = zmCatch();
			printf("task1: catching...%s\n", (e) ? e->msg : "[no exception]");
			zmyield 4;
		}

		zmstate 4:
			printf("task1: some operation\n");
			zmyield 5;

		zmstate 5:
			/* this resume the subtask that raise zmCONTINUE: task3 */
			printf("task1: resuming continue-exception-block\n");
			zmyield zmUNRAISE(sub2, "I-am-task1") | 2;

		ZMEND
	}


output:

	task1: init
		task2: init
			task3: init
			task3: stop this task by raising continue (*)
	task1: catching...[continue exception test]
	task1: some operation
	task1: resuming continue-exception-block
			task3: (*) unraised ... OK
			task3: msg = `I-am-task1`
			task3: no more to do ... term
		task2: term
	task1: term


## EVENT:
Virtual event that can keep a task in a waiting state 
until a trigger resume the task.

**[WORK IN PROGRESS: I will complete and correct this README as soon as possible** 


## Run:

The main command to run a task is `zm_go`

