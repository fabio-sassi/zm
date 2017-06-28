

# ZM:
ZM use a finite state machines to handle *concurrency*. The library support
three kind of *continuations*:

- *ptask* (independend task)
- *subtask* (dependent task)
- exception

Tasks can also interact with virtual events.

## The Core:
The idea behind ZM is to split code of a task with a `switch` 
and use `return` to yield to another piece of code (state or zmstate).

A raw implementation of this schema can be something like this:

	#define END 100

	int foo(int state)
	{
		switch(state) {
			case 1:
				printf("step 1 - init\n");
				return 2;

			case 3:
				printf("step 3\n");
				return END;

			case 2:
				printf("step 2\n");
				return 3;
		}
	}


	int main() {
		int s1 = 1;
		while(s1 != END)
			s1 = foo(s1);
	}

In this example `return` act like a yield-operator, `foo` represent
a task-class, `s1` a task instance and `while` is the scheduler.

In the same way, with the help of some macro, is possible to define 
a ZM task as:

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


The C preprocessor convert this code in something like:

	zm_Machine *foo = ...

	foo->callback = foo__;

	int32 foo__(zm_VM *zm, int zmop, void *zmdata, void *zmarg) {
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

`foo` is a task class that allow to instance tasks (`zm_State`).

`zmstate` define an integer for labeling a blocks of (atomic) execution.

A `foo` instance return a 4 bytes integer: one byte represent a 
command for the task manager while others bytes are arguments for 
this command.

When an instance return the value 2, the command is 0 (inner yield) 
with the only argument 2. Task manager process returned command storing 
the argument 2 as a resume point for next step.


The same concept allow to deal with external yield (yield to other tasks):

	yield zmTO(foo2task) | 4;

this is converted in:

	return resume_task(vm , foo2task) | 4;

where `resume_task` resume `foo2task` and return the command 
`TASK_SUSPEND`. 


Task manager extract the 4 bytes returned by yield-operator, each one
have a particular meaning:

1. The zm-command (0 = inner yield)
2. normal resume zmstate 
3. iter resume zmstate
4. catch resume zmstate

The iter and catch resume values are shifted by 1 and 2 bytes with the
macros: `zmNEXT(n)` and `zmCATCH(n)` while zm-commands are constants 
greater than 0xFFFFFF. 

This allow to use them simultaneously, using *OR* operator, in a single yield:

	yield TASK_SUSPEND | 4 | zmNEXT(5) | zmCATCH(7);


The involved endianess issue is worked around with endianess-independent  
procedure.

### zmstate and the task manager:
Task manager cycle through active tasks and process them step by step.
A step (also called **machine step**) is the piece of code between the
current `zmstate` and the first yield or raise operator.
The main purpose of task manager in ZM is to map the current
and successive states (`zmstate`) of a task and excute them, for
this reason is also called virtual mapper:

	zm_VM *vm = zm_newVM("test VM");

To perform a single machine step:

	zm_go(vm, 1);



## Hello Word:

	/* task class definition */
	ZMTASKDEF( mycoroutine )
	{
		ZMSTART

		zmstate 1:
			printf("my task: -init-\n");
			zmyield 2;

		zmstate 3:
			printf("my task: world\n");
			zmyield zmTERM;

		zmstate 2:
			printf("my task: Hello\n");
			zmyield 3;

		zmstate ZM_TERM:
			printf("my task: -end-\n");

		ZMEND
	}


	int main()
	{
		zm_VM *vm = zm_newVM("test VM");
		zm_State *s = zm_newTask(vm, mycoroutine, NULL);
		zm_resume(vm, s, NULL);
		while(zm_go(vm, 1))
			printf("(step)\n");
		
		return 0;
	}

output:

	my task: -init-
	(step)
	my task: Hello
	(step)
	my task: world
	(step)
	my task: -end-
	(step)
	(step)


The same task class `mycoroutine` can be used to instance more tasks:  

	int main()
	{
		zm_VM *vm = zm_newVM("test VM");
		zm_State *s1 = zm_newTask(vm, mycoroutine, NULL);
		zm_State *s2 = zm_newTask(vm, mycoroutine, NULL);
		zm_resume(vm, s1, NULL);
		zm_resume(vm, s2, NULL);
		while(zm_go(vm, 1))
			printf("(step)\n");
		
		return 0;
	}

output:
	
	my task: -init-
	(step)
	my task: -init-
	(step)
	my task: hello
	(step)
	my task: hello
	(step)
	my task: world
	(step)
	my task: world
	(step)
	my task: -end-
	(step)
	my task: -end-
	(step)
	(step)
	(step)

The last two `(step)` refer to internal close operations of `s1` and `s2`.

## Tasks:
A **ptask** or *process task* is a [green thread](https://en.wikipedia.org/wiki/Green_threads) while a **subtask** is a special task, child of a ptask or 
another subtask. 

The term **task** is used, in this document, to identify a *generic task*:
a ptask and/or a subtask.

### Subtask dependencies:
Subtask have two kind of dependencies: one about the execution and one about
the context in witch can be instanced and invoked.

In fact while ptask execution is independent a ptask, **subtask execution 
is dependent**: when a task (A) yield to a subtask (B) the task manager
keep track of this and (A) is suspended in a busy-waiting mode. 
When the subtask (B) yield to end or suspend, the caller (A) will be 
resumed.

This is the same behaviour of a function from an abstracted point of view:

1. stop the current code
2. active function code
3. wait the function processing 
4. resume the code after the function call

Ptasks, on the other side, have independent executions: task manager 
don't keep track about who resume a ptask (this is true also if the
resume is accomplished with `zmTO` macro).

Due to this dependencies a subtask must always have a parent
so a **subtask can be instanced and invoked only inside another task**.

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
mean also the automaticaly resume of the task that are waiting for it.

	yield zmSUSPEND | 5;


### Execution-context:
A ptask and the set of its subtasks create an **execution-context**. 
In an execution-context only one between root-ptask or child-subtasks 
can be active at a specific time. 


Any operation performed inside the same execution-context can be splitted 
between different tasks and zmstates but is syncronous.


### Execution-stack:
Inside an execution-context a ptask can yield to a subtask and subtask to 
other subtasks. This can be represented as a stack: the **execution-stack**.

All elements in the execution-stack, with the only exception of the last one, 
are waiting another subtask.


### Data-tree:

The *execution-stack* keep track of yields but there is another important
stored relation: the association between a task and its subtasks:

**each task keep the list of its subtask instances**

The set of this relations can be represented as a tree where ptask is
the root and subtasks are branches and leaves: this is the **data-tree**.

The main purpose of *data-tree* is to deal with the task resource deallocation.

User task-data allocation follow the *data-tree* structure from root to 
leaves direction so the deallocation must follow the same structure in the 
reversed direction.


#Guide:

## Task definition syntax:

Each task (ptask or subtask) have this structure:


	ZMTASKDEF(foo) { 
	
		/* [global definition] */

		ZMSTART
		
		zmstate 1:
			/* [code]  */

		ZMEND
	}

ZMTASKDEF(x) is a macro that create a pointer to a `zm_Machine` named 
  `x` that define the task class `x`.
`ZMSTART` is equivalent to a `switch`, `zmstate` to `case` and 
`ZMEND` to the `default`. 

The `[global definition]` is the place where is possibile to define 
variable or write piece of code that will executed before any zmstate.

### The init zmstate:
The zmstate 1 is the resume point for every new instanced task: it must
always be present.

This zmstate can be used as a constructor to allocate the resource for
the task. 

The library have a constant that can be used in place of 
the numerical value: `ZM_INIT`.


### Local variables:

Each task instance (`zm_State` pointer) has a `data` field used to
to store local variable.

	typedef struct {
		/* ... */
		void *data;
	} zm_State;

This field can be easly reach in task class definition with the variable
**zmdata**:

	#include <zm.h>
	#include <stdio.h>

	struct FooLocal {
		int i;
	};

	ZMTASKDEF(foo) {
		struct FooLocal* self = zmdata;
		int i = 0;

		ZMSTART

		zmstate 1:
			self->i = 0;

		zmstate 2:
			i++;
			self->i++;
			printf("i = %d  self->i = %d\n", i, self->i);
			zmyield 2;
		ZMEND
	}

	int main()
	{
		zm_VM *vm = zm_newVM("test ZM");
		void *d = malloc(sizeof(struct FooLocal));
		zm_resume(vm , zm_newTasklet(vm , foo, d), NULL);
		zm_go(vm , 5);
	}

output:

	i = 1  self->i = 1
	i = 1  self->i = 2
	i = 1  self->i = 3
	i = 1  self->i = 4
	i = 1  self->i = 5

In this example `i` is a temporary variable that exists only in a machine 
step and will be lost when a `zmyield` is reach.
On the other side `zmdata` can store variables without any restriction.

`zmdata` is a copy of the pointer in `data` field inside `zm_State` struct.

The `data` field in `zm_State` can be set in task instantiation:

	void *userlocaldata = ...
	zm_newTask(vm, task, userlocaldata)
	zm_newTasklet(vm, task, userlocaldata)

in explicit way:

	void *userlocaldata = ...
	zm_State *t = zm_newTask(vm, task, NULL)
	t->data = userlocaldata;

or with the `zmData` macro:

	zmstate 1:
		void *userlocaldata = ...
		zmData(userlocaldata);

As explained before `zmdata` is a copy of a pointer so modify the
`task->data` (for example with `zmData`) during the task excution
don't modify the `zmdata` until next machine step.

A second example show how to use `zmData`:

	#include <stdio.h>
	#include <zm.h>


	ZMTASKDEF(foo) {
		struct FooLocal {
			int i;
		} *self = zmdata;

		ZMSTART

		zmstate 1: {
			self = malloc(sizeof(struct FooLocal));
			self->i = 0;
			zmData(self);
		}
		zmstate 2:
			self->i++;
			printf("self->i = %d\n", self->i);
			zmyield 2;
		ZMEND
	}

	int main()
	{
		zm_VM *vm = zm_newVM("test ZM");
		zm_resume(vm , zm_newTasklet(vm , foo, NULL), NULL);
		zm_go(vm , 5);
	}

### Task Resume Argument:

The local variables system above is a way to store data but can 
also be used to exchange messages.
Anyway there is a more flexible feature to exchange variable between tasks:
the *resume argument*. The resume argument allow to set a `void *` pointer
that will be accessible in resumed task as **zmarg** in the first
machine step.

	ZMTASKDEF( Foo )
	{
		const char* arg = ((zmarg) ? (const char*)(zmarg) : "<none>");
		printf("zmarg = %s\n", arg);
		ZMSTART

		zmstate 1:
			printf("foo: 1\n");
			zmyield 2;
		zmstate 2:
			printf("foo: 2\n");
			zmyield zmSUSPEND | 2;

		ZMEND
	}

	int main()
	{
		zm_VM *vm = zm_newVM("test ZM");
		zm_State *f = zm_newTasklet(vm , Foo, NULL);

		zm_resume(vm, f, "Hello, I am the resume argument");
		zm_go(vm , 5);

		zm_resume(vm, f, "How are you?");
		zm_go(vm , 5);

		zm_closeVM(vm);
		zm_go(vm, 100);
		zm_freeVM(vm);
	}

output:
	zmarg = Hello, I am the resume argument
	foo: 1
	zmarg = <none>
	foo: 2
	zmarg = How are you?
	foo: 2
	zmarg = <none>

Each resume functions or operators allow to set this argument (`arg`):

	- `zm_resume(vm, task, arg)`
	- `zmSUB(task, arg)`
	- `zmSSUB(task, arg)`
	- `zmUNRAISE(task, arg)`
	- `zmTO(task, arg)`
	- `zmLAST(task, arg)`
	- `zm_trigger(vm, event, arg)`

Moreover this can be set in a task defintion before any suspend operation with
`zmResponse(arg)`



### Other syntax:

There are some equivalent syntax in task class definition for example `zmyield` 
can be repleaced with `yield` (defining `ZM_FAST_SYNTAX`) and `ZMSTATES` 
can be used in place of `ZMSTART`. 
Moreover `ZMTASKDEF` and `ZMEND` implicit use curly braces `{}` making
possible to don't use. All this features allow to define 
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


## Command context and case convention:

The code of a task class (everything between `ZMTASKDEF` and `ZMEND`)
is a special context with some specific rules. 
In fact some function and operator have meaning only inside 
the **task-class-context** while others can stay everywhere 
(**generic-context**).

`zmstate`, `zmyield`, and `zmData` are examples of functions and operators
that can be used only inside task-class-context while `zm_resume` can 
be used everywhere.

The main case convention is that commands of *task-class-context*  
don't have underscore after *zm* prefix, while commands that can 
be used in every context have it.

### Task-class context:

1. **ZMABC**: task class definintion operators
   - `ZMTASKDEF`
   - `ZMSTART`
   - `ZMEND`
2. **zmAbcXyz**: inside only functions
    - `zmNewSubTask()`
	- `zmCatch()`
	- `zmData()`
	- ...
3. **zmabc**: reserved operators or variables
    - `zmyield` (`yield` with fast syntax)
    - `zmraise` (`raise` with fast syntax)
    - `zmstate` 
    - `zmdata`
	- `zmarg`
    - `zmop`
4. **zmABC**: zmyield modifiers
   - `zmSUB()`
   - `zmNEXT()`
   - `zmCATCH()`
   - `zmTERM` ...

### Generic-context:

1. **zm\_abcDefg**: functions that can be used inside or outside task
   definition.
    - `ZM_resume()`
    - `ZM_newTask()`
    - `ZM_trigger()`
	- `ZM_abort()` ...
2. **zm\_ABC\_XYZ**: library constants
   - `ZM_INIT`
   - `ZM_TERM`, ...
3. **zm\_AbcXyz**: library structures
   - `zm_State`
   - `zm_Event`, ...



### Instance tasks:

To instance a new task:

	/* instance a ptask relative to machine foo */
	zm_State *task = zm_newTask(vm , foo, userdata);

	/* instance a subtask relative to machine foo2 (can be done only 
	   inside a ZMTASKDEF - ZMEND task definition, vm parameter
	   is implicit) */
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

Tasklets are useful to perform one-shot operations (operations
without any suspend) where no reference is needed.


### The task destructor:
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

	/* A task definition rappresent a generic task so can be instanced
	 * as a ptask (pt) or as a subtask (sb)
	 */
	ZMTASKDEF(task2)

		const char *suffix = (const char*)zmdata;
		const char *arg = (const char*)zmarg;

		ZMSTATES

		zmstate 1:
			printf("    task2 %s: init %s\n", suffix, arg);
			yield zmTERM;

		zmstate ZM_TERM:
			printf("    task2 %s: end\n", suffix);
			yield zmEND;
	ZMEND


	ZMTASKDEF(task1) ZMSTATES
		zmstate 1: {
			zm_State *sb = zmNewSubTasklet(task2, "as sub");

			printf("task1: yield to sub\n");

			/* This yield put this task in busy-waiting mode.
			 * When sb will yield to zmEND this task will be 
			 * resumed (in zmstate 2)
			 */
			yield zmSUB(sb, "(sub)") | 2;
		}
		zmstate 2: {
			zm_State *pt = zm_newTasklet(vm, task2, "as ptask");

			printf("task1: yield to ptask\n");

			/* This yield resume pt and simply suspend this task */

			yield zmTO(pt, "(to)") | 3;
		}

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
	    task2 as sub: init (sub)
	    task2 as sub: end
	task1: yield to ptask
	    task2 as ptask: init (to)
	    task2 as ptask: end
	#main: no more to do...sure?
	task1: term
	#main: now there is no more to do


## Exception:


### Raise: 

There are two kind of exception in ZM:

- **error exception**
- **continue exception**

An exception is raised with `zmraise` operator followed by:

1. `zmERROR(int code, const char* msg, void *data)` for error exception
2. `zmCONTINUE(int code, const char* msg, void *data)` for continue exception

### Catch:

The exception catching is performed declaring the exception-zmstate
resume point with `zmCATCH` (equals to "try") and fetching the exception
with `zmCatch` in the exception-zmstate (equals to "catch"):

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
- In the catch zmstate, exception must be catch with `zmCatch()`
  (before the next yield). 


#### Error Exception:
Raising an error-exception implies that all tasks between the 
raise and the task before the catch will be aborted.

NOTE: This is not true if there is an error reset (see below).

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
*Error-reset* allow to avoid the exception-error abort behaviour, 
defining a zmstate as resume point.

The reset point is defined with this syntax:

	yield 4 | zmRESET(7)

To define a resent point in the raise itself use instead: 

	raise zmERROR(0, "test", NULL) | 5;

zmRESET cannot be applied to a ptask.

#### Continue Exception:
Continue exception have a very different behaviour from error exception. 

*A continue exception is a kind of (big) suspend-resume block.*

The continue exception suspend the execution of a subtask in the raise 
state and resume the state with the catch.

In this situation all the subtask between the raise and the subtask
before catch are a suspended block.

This block can be resumed using:

	yield zmUNRAISE(sub1, NULL) | 5;

This commands *don't resume* `sub1` but the state who raised the 
continue exception.

Instead of `zmUNRAISE` is possible also to use:

	yield zmSSUB(sub1, NULL) | 5;

`zmSSUB` act like `zmSUB` if sub1 is a suspended subtask or like `zmUNRAISE`
if `sub1` is a continue-exception suspended block.


Example:

	zm_State *sub2;

	ZMTASKDEF(task3)
	{
		ZMSTART

		zmstate 1:
			printf("\t\ttask3: init\n");
			printf("\t\ttask3: raise continue exception (*)\n");
			zmraise zmCONTINUE(0, "continue-test]", NULL) | 2;

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
			const char *msg = (e) ? e->msg : "no exception";
			printf("task1: catching...%s\n", msg);
			zmyield 4;
		}

		zmstate 4:
			printf("task1: some operation\n");
			zmyield 5;

		zmstate 5:
			/* this resume the subtask that raise 
			   zmCONTINUE -> task3 */
			printf("task1: resuming continue-exception-block\n");
			zmyield zmUNRAISE(sub2, "I-am-task1") | 2;

		ZMEND
	}


output:

	task1: init
		task2: init
			task3: init
			task3: raise continue exception (*)
	task1: catching...continue-test
	task1: some operation
	task1: resuming continue-exception-block
			task3: (*) unraised ... OK
			task3: msg = `hello-I-am-task1`
			task3: no more to do ... term
		task2: term
	task1: term


## EVENT:
A task can be suspended waiting a virtual event. This is done with:

	zmyield zmEVENT(e) | 5;

A virtual event keep one or more task in a waiting state until a trigger
or an unbind resume them. 


### Create an Event:
To create an event:

	zm_Event *event = zm_newEvent(trigcb, flags, data);

Where:

- trigcb can be NULL or a callback function with this arguments:
  `int trigcb(zm_VM *vm, int scope, zm_Event* event, zm_State *s, void **arg)`

- flags are a combination of:
	- `ZM_TRIGGER`: trigcb will be used for trigger operation 
	- `ZM_UNBIND_REQUEST`: trigcb will be used for unbind operation due to
	  the functions: `zm_unbind` or `zm_unbindAll`.
	- `ZM_UNBIND_ABORT`: trigcb will be used for event unbind in close
	  operation.

	A shortcut for all unbind operation (except the unbind relative to a
	trigger) is `ZM_UNBIND` that is equivalent to 
	`ZM_UNBIND_REQUEST | ZM_UNBIND_ABORT`.

- `data` is `void *` pointer that can be accesible from `event->data`

### Trigger an event:

	size_t zm_trigger(zm_VM *vm, zm_Event *event, void *arg);

If the trigger callback is NULL or the event hasn't `ZM_TRIGGER` flag 
each task binded to this event will be resumed (with resume argument `arg`). 

On the other side an event with a trigger callback and the `ZM_TRIGGER` flag
will use this callback for two purpose:

- filter task that can receive this event
- accomplish syncronous operation

So the trigger callback can control what tasks can be resumed and can
modify the resume argument.

In both situations `zm_trigger` return the number of resumed task.

### Unbind an event:

	size_t zm_unbindAll(zm_VM *vm, zm_Event *e, void *argument);

	size_t zm_unbind(zm_VM *vm, zm_Event *e, zm_State* s, void *argument);

Unbind, like trigger, can invoke the trigcb callback if this is not NULL 
and the event has the `ZM_UNBIND_REQUEST` flag.

The callback for the unbind operations (also `ZM_UNBIND_ABORT`) is invoked
only to perform syncronous operation and cannot filter anything.

### The callback:


## Run:

The main command to run a task is `zm_go`




