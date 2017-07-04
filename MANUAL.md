

# ZM:
ZM use a finite state machines to handle *concurrency*. It allow to create
many independent concurrent task that can interact with other task or
event. The library support three kind of *continuations*:

- *ptask* (independent task)
- *subtask* (dependent task)
- exception

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
and successive states (`zmstate`) of a task and execute them, for
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
mean also the automatically resume of the task that are waiting for it.

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

`ZMTASKDEF(x)` is a macro that create a pointer to a `zm_Machine` named 
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
`task->data` (for example with `zmData`) during the task execution
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

### The Resume Argument:

The local variables system above is a way to store data and can 
also be used to exchange messages.
Anyway there is a more flexible feature to exchange variable between tasks:
the *resume argument*. The resume argument allow pass a `void` pointer to a 
resume function/operator that will be accessible in resumed task as **zmarg**.
This pointer will be avaible only after a resume in the first machine step.

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

#### As a parameter:

Each resume functions or operators allow to set this argument.
In these functions the resume argument act like a function parameter:

	- `zm_resume(vm, task, resumearg)`
	- `zmSUB(task, resumearg)`
	- `zmSSUB(task, resumearg)`
	- `zmUNRAISE(task, resumearg)`
	- `zmTO(task, resumearg)`
	- `zmLAST(task, resumearg)`
	- `zm_trigger(vm, event, resumearg)`

#### As a return:

Resume argument can also be used as "return value". A subtask can set it
before suspend or term:

	zmResponse(resumearg)


### Task class syntaxes:

There are some equivalent syntax in task class definition for example `zmyield` 
can be replaced with `yield` (defining `ZM_FAST_SYNTAX`) and `ZMSTATES` 
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

1. **ZMABC**: task class definition operators
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

`zmEND` can be used only in closing-mode.


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
- An error-exception without a catch, cause `zm_go` stop and return
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


#### Uncaught Error Exception:
An error exception without a catch will be caught by `zm_go` (or `zm_mGo`).
This function suddenly return a `ZM_RUN_EXCEPTION`. 

This particular exception is outside task definition and must be catch
and free with special functions. 

To catch an uncaught exception use:

	zm_Exception *zm_ucatch(zm_VM *vm)

instead to free an uncaught exception:

	void zm_freeUncaughtError(zm_VM *vm, zm_Exception *e);


**Note:** A second uncaught exception (before the first one is catch and 
free) cause a fatal error. 

##### Example:

	int status;
	do {
		status = zm_go(vm, 100)

		if (status == ZM_RUN_EXCEPTION) {
			zm_Exception *e = zm_ucatch(vm);
			zm_printError(NULL, e, true);
			zm_freeUncaughtError(vm, e);	
		}

	} while(status)


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
A task can be suspended waiting a virtual event:

	zmyield zmEVENT(e) | 5;

A virtual event keep one or more task in a waiting state until a trigger
or an unbind resume them. 


### Create an Event:
To create an event:

	zm_Event *event = zm_newEvent(data);

`data` is `void *` pointer that can be accessible from `event->data`


### Bind an event to a task:

	zmyield zmEVENT(event) | EVTRIG | zmUNBIND(EVUNBIND);

When the task receive the event it will be resumed in `EVTRIG` zmstate.
If an unbind is performed before trigger, task will be resume in the
zmstate defined by `zmUNBIND()`.

If the unbind resume point is not defined the trigger resume point is used
also for unbind operation:

	zmyield zmEVENT(event) | EVTRIG;

### Trigger an event:

	size_t zm_trigger(zm_VM *vm, zm_Event *event, void *arg);

This command trigger the event `event`. A trigger resume 
all tasks binded to the event with `arg` as resume argument.

The command return the number of resumed task.

### Unbind a task:

	size_t zm_unbind(zm_VM *vm, zm_Event *e, zm_State* s, void *arg);

Unbind the task `s` with resume argument `arg`. 

Return:
	
- 0 if `s` is not binded to `e`
- 1 if `s` is is binded to `e` 

### Unbind all tasks:
	
	size_t zm_unbindAll(zm_VM *vm, zm_Event *e, void *arg);

Unbind all task binded to event `e` with resume argument `arg`.

### The event callback:

The event callback allow to filter trigger event and to accomplish 
syncronous operation. An event callback can be set with: 

	void zm_setEventCB(zm_VM *vm, zm_Event* e, zm_event_cb cb, int scope);

The scope-flag allow to define the contexts where the callback will be 
activated:

- `ZM_TRIGGER` callback will be used (also) for trigger operation.
- `ZM_UNBIND_REQUEST` callback will be used (also) for unbind request:
   (`zm_unbind`, `zm_unbindAll`).
- `ZM_UNBIND_ABORT` callback will be used (also) for event unbind in close
   operations. 

A shortcut for all unbind operation is `ZM_UNBIND` equivalent to 
`ZM_UNBIND_REQUEST | ZM_UNBIND_ABORT`.


The event callback is a function with this format:

	int (*zm_event_cb)(zm_VM *vm, int scope, zm_Event *e, zm_State *state,
	                                                           void **arg)

Where:

- `state` is the binded state 
- `scope` is the scope where the callback has just been invoked (it 
  can assume only one of the scope flag value).
- `arg` is:
	- a pointer to the resume argument of `zm_trigger`
	  in `ZM_TRIGGER` scope.
	- a pointer to the resume argument of unbind command 
	  in `ZM_UNBIND_REQUEST` scope.
	- null in `ZM_UNBIND_ABORT` scope.


This callback can manage only task data, event data and argument.
Use a ZM function inside the callback can produce impredicable 
behaviour.


#### The trigger callback:

The trigger callback (event callback with the scope flag `ZM_TRIGGER`) have 
two purpose:

- filter task that can receive this event
- accomplish syncronous operation

A call to `zm_trigger` produce from one callback call to many.

The first call is a generic call called **pre-fetch mode**. If the pre-fetch
accept the event the callback will be invoked for each task binded to 
the event: **fetch mode**.

In pre-fetch mode the binded task argument is null:

	int evcb(zm_VM *vm, int scope, zm_Event *e, zm_State *s,void **arg)
	{
		if (scope == ZM_TRIGGER) { 
			if ((s == NULL)) {
				/* pre-fetch mode */		
			} else {
				/* fetch mode */
			}
		}
	}

In pre-fetch mode the callback function can return only two value:

- `ZM_EVENT_ACCEPTED`: accept this event and go in fetch mode.
- `ZM_EVENT_REFUSED`: refuse this event.


Also in fetch mode the callback function must return one of this two value
but can also add `ZM_EVENT_STOP` modifier to stop to fetch any other binded
task.

	int randcb(zm_VM *vm, int scope, zm_Event *e, zm_State *s,void **arg)
	{
		if (scope != ZM_TRIGGER) 
			return 0;


		if ((s == NULL)) {
			/* pre-fetch mode */
			if (rand() % 2)
				return ZM_EVENT_ACCEPTED;
			else
				return ZM_EVENT_REFUSED;
		}

		/* fetch mode */
		switch(rand() % 4) {
			case 0:
				return ZM_EVENT_ACCEPTED;
			case 1:
				return ZM_EVENT_REFUSED;
			case 2:
				return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
			case 3:
				return ZM_EVENT_REFUSED | ZM_EVENT_STOP;
		}
	}


The resume argument pointer can be replaced with another pointer or nullified.
In fetch mode this modify affect only the resume of the current binded task
while in pre-fetch affect all the successive fetch requests.

#### The unbind callback:

An unbind callback is an event callback with the scope flag `ZM_UNBIND_REQUEST` 
and/or `ZM_UNBIND_ABORT`.

The unbind callback is used only to perform syncronous operation, it have
not filtering purpose (as trigger callback). For this reason the return 
value have no meaning.


Unbind callback is relative to these scopes:

- `ZM_UNBIND_REQUEST` is used to manage explicit unbind request
  (`zm_unbind`, `zm_unbindAll` and `zm_freeEvent`)
- `ZM_UNBIND_ABORT` is used during close operations

The unbind callback is invoked for each binded task and return the number
of unbinded task.

`zm_freeEvent` perform a last `ZM_UNBIND_REQUEST` with a null binded task.
This is a **post fetch** or **pre free** operation before free the event.

	int evcb(zm_VM *vm, int scope, zm_Event *e, zm_State *s,void **arg)
	{
		if (scope == ZM_UNBIND_REQUEST) { 
			if ((s == NULL)) {
				/* post-fetch (or pre-free) mode */		
			} else {
				/* fetch mode */
			}
		}
	}


#### Event callback example:
	typedef struct {
		int id;
	} TaskData;

	int counter = 1;

	zm_Event * event;

	int getID(zm_State *s)
	{
		return ((TaskData*)(s->data))->id;
	}

	int eventcb(zm_VM *vm, int scope, zm_Event* e, zm_State *s, void **arg)
	{
		const char *msg = ((arg) ? ((const char*)(*arg)) : ("null"));
		msg = (msg) ? (msg) : "";
		printf("\tcallback: arg = `%s` scope = ", msg);

		if (scope & ZM_UNBIND_REQUEST)
			printf("UNBIND_REQUEST ");

		if (scope & ZM_UNBIND_ABORT)
			printf("UNBIND_ABORT ");

		if (scope & ZM_TRIGGER)
			printf("TRIGGER ");

		printf("\n");

		if (!s) {
			/* a modify of arg in pre-fetch affect all fetches */
			if (scope & ZM_TRIGGER)
				*arg = "two";

			if (scope & ZM_TRIGGER)
				printf("\t\t-> pre-fetch\n");
			else
				printf("\t\t-> pre-free\n");

			return ZM_EVENT_ACCEPTED;
		}

		printf("\t\t-> fetch task %d ", getID(s));


		if (getID(s) == 1) {
			/* a modify of arg in fetch affect only current fetch */
			*arg = "three";
			if (scope & ZM_TRIGGER)
				printf("(accepted)\n");
			return ZM_EVENT_ACCEPTED;
		}

		if (scope & ZM_TRIGGER)
			printf("(accepted but stop other fetches)");

		printf("\n");
		return ZM_EVENT_ACCEPTED | ZM_EVENT_STOP;
	}




	ZMTASKDEF( mycoroutine )
	{
		TaskData *self = zmdata;

		ZMSTART

		zmstate 1:
			self = malloc(sizeof(TaskData));
			self->id = counter++;
			zmData(self);
			printf("task %d: -init-\n", self->id);
			zmyield zmEVENT(event) | 2 | zmUNBIND(3);

		zmstate 2:
			printf("task %d: msg = `%s`\n", self->id, 
			       (const char*)zmarg);
			zmyield zmTERM;

		zmstate 3:
			printf("task %d: event aborted - ", self->id);
			printf("msg = `%s`\n", (const char*)zmarg);
			zmyield zmTERM;

		zmstate ZM_TERM:
			printf("task %d: -end-\n", ((self) ? (self->id) : -1));
			if (self)
				free(self);

		ZMEND
	}


	int main() {
		zm_VM *vm = zm_newVM("test VM");
		zm_State *s1, *s2, *s3, *s4, *s5;

		event = zm_newEvent(NULL);
		zm_setEventCB(vm, event, eventcb, ZM_TRIGGER | ZM_UNBIND);

		s1 = zm_newTasklet(vm, mycoroutine, NULL);
		s2 = zm_newTasklet(vm, mycoroutine, NULL);
		s3 = zm_newTasklet(vm, mycoroutine, NULL);
		s4 = zm_newTasklet(vm, mycoroutine, NULL);
		s5 = zm_newTasklet(vm, mycoroutine, NULL);
		zm_resume(vm, s1, NULL);
		zm_resume(vm, s2, NULL);
		zm_resume(vm, s3, NULL);
		zm_resume(vm, s4, NULL);
		zm_resume(vm, s5, NULL);

		while(zm_go(vm, 1));

		printf("\n* trigger event:\n");
		zm_trigger(vm, event, "one");
		printf("\n* unbind s4:\n");
		zm_unbind(vm, event, s4, "I don't wait anymore");

		printf("\n\n");
		while(zm_go(vm, 1));

		printf("\n* close all tasks:\n\n");
		zm_closeVM(vm);
		zm_go(vm, 1000);
		zm_freeVM(vm);

		printf("\n* free event:\n\n");
		zm_freeEvent(vm, event);

		return 0;
	}

output:

	task 1: -init-
	task 2: -init-
	task 3: -init-
	task 4: -init-
	task 5: -init-

	* trigger event:
		callback: arg = `one` scope = TRIGGER 
			-> pre-fetch
		callback: arg = `two` scope = TRIGGER 
			-> fetch task 1 (accepted)
		callback: arg = `two` scope = TRIGGER 
			-> fetch task 2 (accepted but stop other fetches)

	* unbind s4:
		callback: arg = `I don't wait anymore` scope = UNBIND_REQUEST 
			-> fetch task 4 


	task 1: msg = `three`
	task 2: msg = `two`
	task 4: event aborted - msg = `I don't wait anymore`
	task 1: -end-
	task 2: -end-
	task 4: -end-

	* close all tasks:

		callback: arg = `` scope = UNBIND_ABORT 
			-> fetch task 5 
		callback: arg = `` scope = UNBIND_ABORT 
			-> fetch task 3 
	task 5: -end-
	task 3: -end-

	* free event:

		callback: arg = `null` scope = UNBIND_REQUEST 
			-> pre-free

	


## Run:

There main command to run tasks is: 

	zm_mGo(zm_VM *vm, zm_Machine* machine, unsigned int nsteps)

where:

- `machine`: is a specific task class to be executed (if null all task classes
  will be used).
- nsteps: the number of machine step to perform.

`zm_go` is equivalent to `zm_mGo` with a null `machine` pointer.

	zm_go(vm, 100);
	/* equals to */
	zm_mGo(vm, NULL, 100);


These command return a combination of:

- `ZM_RUN_IDLE = 0` nothing to do.
- `ZM_RUN_AGAIN` there are still active tasks.
- `ZM_RUN_EXCEPTION` uncaught error exception.

