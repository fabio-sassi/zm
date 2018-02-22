

# ZM:
ZM use a finite state machines to handle *concurrency*. It allow to create
many independent concurrent task that can interact with other task or
event. The library support three kind of *continuations*:

- *ptask* (independent task)
- *subtask* (dependent task)
- exception


ZM through some C macro define a new set of syntaxes to write coroutine,
task and exception.

## A simple task:

	/* task class */
	ZMTASKDEF( mycoroutine )
	{
		ZMSTART

		zmstate 1:
			printf("my task: -init-\n");
			zmyield 2;

		zmstate 2:
			printf("my task: Hello\n");
			zmyield 3;

		zmstate 3:
			printf("my task: world\n");
			zmyield zmTERM;

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


The last `(step)` refer to internal close operations of task `s`.

### Behind the macros:

The idea behind ZM is to label and split code in a function with 
`switch`-`case` and use `return` to yield to another piece of code.

A raw implementation of this schema is:

	#define END 100

	int foo(int state)
	{
		switch(state) {
			case 1:
				printf("step 1 - init\n");
				return 2;

			case 2:
				printf("step 2\n");
				return END;
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
			/* yield to zmstate 2 */
			zmyield 2;

		zmstate 3:
			/* suspend current task, resume in zmstate 2*/
			zmyield zmSUSPEND | 2;

		zmstate 2:
			zmyield 3

		ZMEND
	}


The C preprocessor convert this code in something like:

	zm_Machine *foo = zm_newMachine(__foo__);

	int32 __foo__(zm_VM *zm, int zmop, void *zmdata, void *zmarg) {
		switch(zmop) {
		case 1:
			return 2;

		case 3:
			return TASK_SUSPEND | 2;

		case 2:
			return 3

		default:
			return zm_default(zmop);
		}
	}

`foo` is a task class that allow to instance tasks (`zm_State`).

`zmstate` define an integer between 1 and 250 for labeling a blocks of 
(atomic) execution.

`zmyield` allows to return a 32 bit integer that contains a 
zm-directive with three optional arguments

Examples:

	/* 1 */
	zmyield 2;

	/* 2 */
	zmyield zmSUSPEND | 3;

	/* 3 */
	zmyield zmSUB(s9, NULL) | 4 | zmNEXT(5) | zmCATCH(7);


1. *yield to zmstate 2*: directive is implicit (inner yield) with argument `2`
2. *yield to suspend and resume in zmstate 3*: directive is suspend current 
   task and argument `3` define the point where task must restart when it
   will be resumed again.
3. *yield to s9, resume in 4, iter in 5 and catch in 7*: 
   directive is suspend current task and resume subtask (`s9`).
   The three arguments define the zmstate where current task must restart
   when subtask `s9`:
   - yield to end (zmstate 4)
   - yield to suspend (zmstate 5)
   - raise and exception (zmstate 7)
  

## Tasks:

In ZM there is two kind of task: **ptask** or *process task*
and **subtask**. 

The term **task** is used in this documentation to identify 
a *generic task*.

## Task class:

A task class define a `zm_Machine*` that can be used to 
instance tasks (ptask and subtask).

	ZMTASKDEF(foo) { 
	
		/* [common] */

		ZMSTART
		
		zmstate 1:
			/* [code]  */

		ZMEND
	}

`ZMTASKDEF(x)` create the `zm_Machine *x`, `ZMSTART` is equivalent 
to a `switch {`, `zmstate` to `case` while `ZMEND` contains
something like `default: process_default(); }`.

Between `ZMTASKDEF` and `ZMSTART` (in place of `/* [common] */`) 
is possibile to define variable or write piece of code that will executed
before any zmstate.

### Task states - zmstate:
zmtates define the atomic execution blocks in a task.

	zmstate 100:
		/* atomic execution block - begin */

		...

		zmyield ...
		/* atomic execution block - end */


`zmstate` is followed by a positive integer between 1 and 250 (value 
over 250 are reserved).

#### The first zmstate:
The zmstate 1 is the default resume point for every new instanced task: 
it must always be present.

This zmstate can be used as a constructor.

The library have a constant that can be used in place of 
the numerical value: `ZM_INIT`.



## Process Task:

A *ptask* is a [green thread](https://en.wikipedia.org/wiki/Green_threads):
many *ptasks* can be active at the same time, emulating concurrently 
execution.

### Create a ptask: 

`zm_newTask` is used to instance a new ptask:

	zm_State *task = zm_newTask(zm_VM *vm, zm_Machine *m, void *taskdata)

This instance a new ptask relative to the task manager `vm` and with `m` as
task class.

### Resume a ptask:

Every ptask is created in suspend mode, to resume it:

	zm_resume(zm_VM *vm, zm_State *task, void *argument);

This add `task` to the task manager list of active ptask. 

### ptask example:

	#include <zm.h>

	ZMTASKDEF(foo) { 
	
		ZMSTART
		
		zmstate 1:
			printf("here we go\n");
			zmyield zmTERM;

		ZMEND
	}

	int main() {
		/* instance a new task manager */
		zm_VM *vm = zm_newVM("test");

		/* instance a ptask relative to machine foo */
		zm_State *task = zm_newTask(vm , foo, NULL);

		/* resume the task */
		zm_resume(vm, task, NULL);

		/* execute 30 task manager step */
		zm_go(vm, 30);

		/* free the task */
		zm_freeTask(vm, task);

		/* free the task manager */
		zm_freeVM(vm);
	}


## Subtasks:

A **subtask** is a special task, child of another task.
Subtasks help to reuse code, improve readability and implement
other continuations structures like **Exception** and 
**Continue Exception**.

If a process task is like a thread, subtasks are like functions
executed inside a thread. 

When a ptask yield to a subtask the ptask is temporary suspended
in a busy-waiting mode until the subtask don't term or suspend
its execution. Moreover subtask can yield to another subtask,
like function call can be nested.

In a more formal way: when a generic task (A) yield to a 
subtask (B) the task manager suspended (A) in a busy-waiting mode
and resume it when (B) yield to end or suspend.

This is the same behaviour of a function call from an abstracted
point of view:

1. pause the current code
2. active function code
3. wait the function processing 
4. resume the code after the function call



### Create a subtask:

Inside a task class is possible to instance one or more subtasks
with:

	zm_State* subtask = zmNewSubTask(zm_Machine* machine, void *subtaskdata)
	
	/* or with short syntax */

	zm_State *subtask = zmNewSub(zm_Machine* machine, void *subtaskdata);

This can be done only in a task class (between `ZMTASKDEF`
and `ZMEND`).


### Yield to a subtask (resume a subtask):

While a ptask can be resumed with `zm_resume` a subtask can be resumed 
only within the `zmyield` operator by a resume yield-operator like `zmSUB`.

This operation is named *yield to a subtask*.

A subtask resume implies the definition of the current task resume 
point, for example:

	zmyield zmSUB(zm_State *subtask, void *argument) | resumepoint;

`resumepoint` is the `zmstate` where the current task will restart
when subtask will yield to end or to suspend.

It's possible also to define two different restart zmstate: one 
when `subtask` yield to suspend `ipoint` and one when yield to end
`rpoint`:

	zmyield zmSUB(zm_State *subtask, void *argument) | rpoint
	        zmNEXT(ipoint);




#### subtasks example:


	#include <zm.h>

	zm_State *tmp;

	ZMTASKDEF(subfoo)
		ZMSTATES

		zmstate 1:
			printf("subfoo: one\n");
			zmyield zmSUSPEND | 2;

		zmstate 2:
			printf("subfoo: two\n");
			zmyield zmTERM;
	ZMEND

	ZMTASKDEF(foo) {

		ZMSTART

		zmstate 1:
			printf("foo: init\n");
			tmp = zmNewSub(subfoo, NULL);
			/* yield to tmp and resume in zmstate 2 */
			zmyield zmSUB(tmp, NULL) | 2;

		zmstate 2:
			printf("foo: second subtask resume\n");
			/* yield to tmp and resume in zmstate 3 */
			zmyield zmSUB(tmp, NULL) | 3;

		zmstate 3:
			zmFreeSub(tmp);
			zmyield zmTERM;

		ZMEND
	}

	int main() {
		zm_VM *vm = zm_newVM("test");
		zm_State *task = zm_newTask(vm , foo, NULL);
		zm_resume(vm, task, NULL);
		zm_go(vm, 100);
		zm_freeTask(vm, task);
		zm_freeVM(vm);
	}



### Local variables:

Each task instance (`zm_State` pointer) has a `data` field used to
to store local variable.

	typedef struct {
		/* ... */
		void *data;
	} zm_State;

This field can be easly reach in task class with the macro-variable
**zmdata**:

	#include <zm.h>
	#include <stdio.h>

	struct FooLocal {
		int j;
	};

	ZMTASKDEF(foo) {
		struct FooLocal* self = zmdata;
		int i = 0;

		ZMSTART

		zmstate 1:
			self->j = 0;

		zmstate 2:
			i++;
			self->j++;
			printf("i = %d  self->j = %d\n", i, self->j);
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

	i = 1  self->j = 1
	i = 1  self->j = 2
	i = 1  self->j = 3
	i = 1  self->j = 4
	i = 1  self->j = 5

In this example `i` is a temporary variable that exists only in a machine
step and will be lost when a `zmyield` is reach.
On the other side `zmdata` can store persitent variables without any restriction.

`zmdata` act like a variable but is a macro that extract data from current 
executing state, for this reason is a reserved word.

	#define zmdata (zm_getCurrentState(vm)->data)

The task data can be set in task instantiation:

	zm_State *t = zm_newTask(vm, task, taskdata)
	zm_State *s = zmNewSub(task, taskdata)

through data field in `zm_State` struct: 

	t->data = taskdata;

or with macro `zmdata` in task class:

	zmstate 1:
		zmdata = get_taskdata();

This simple example show how to use `zmdata` to create persistents 
locals variables:

	#include <stdio.h>
	#include <zm.h>


	ZMTASKDEF(foo) {
		struct FooLocal {
			int i;
			int j;
		} *self = zmdata;

		ZMSTART

		zmstate 1:
			zmdata = self = malloc(sizeof(struct FooLocal));
			self->i = 0;
			self->j = 0;

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
also be used to exchange messages but there is a more specific
feature for this work.

The *resume argument* allow pass a pointer to a resume function/operator
that will be accessible in resumed task as **zmarg**.

NOTE: *resume argument* is avaible in a task only for a machine step after
resume.

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
before suspend or term using the reserved word `zmresult`.
`zmresult` is a macro that return the zmarg from the caller so it act like
a variable but can be used only inside a subtask.

	zmresult = (void*)somedata;


## Task classes and case convention:

The code of a task class (everything between `ZMTASKDEF` and `ZMEND`)
is a special context with some specific rules.
In fact some function and operator have meaning only inside
the **task-class-context** while others can stay everywhere
(**generic-context**).

`zmstate`, `zmyield`, and `zmCatch` are examples of operators and functions
that can be used only inside task-class-context while `zm_resume` in 
a generic-context.

The main case convention is that commands of *task-class-context*
don't have underscore after *zm* prefix, while commands that can
be used in every context have it.

### Task-class context:

1. **ZMABC**: task class operators
	- `ZMTASKDEF`
	- `ZMSTART`
	- `ZMEND`
2. **zmAbcXyz**: functions
	- `zmNewSubTask()`
	- `zmCatch()`
	- `zmRootData()`
	- ...
3. **zmabc**: reserved operators or variables
	- `zmyield`
	- `zmraise`
	- `zmstate`
	- `zmdata`
	- `zmresult`
	- `zmarg`
	- `zmop`
4. **zmABC**: zmyield modifiers
	- `zmSUB()`
	- `zmNEXT()`
	- `zmCATCH()`
	- `zmTERM` ...

### Generic-context:

1. **zm\_abcDefg**: functions that can be used inside or outside task class
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


### Task class syntaxes:

There are some equivalent syntax in task class for example `zmyield`
can be replaced with `yield` (defining `ZM_FAST_SYNTAX`) and `ZMSTATES`
can be used in place of `ZMSTART`.
Moreover `ZMTASKDEF` and `ZMEND` implicit use curly braces `{}` making
possible to don't use them. All this features allow to define
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
			zmyield zmTERM;
	ZMEND


### Tasklet:

A tasklet is a task that have not to be manually free, it will automatically 
free after the close operations.

	/* instance a ptasklet relative to machine foo */
	zm_State *s = zm_newTasklet(vm , foo, userdata);


	/* instance a subtasklet relative to machine foo2 */
	zm_State *s = zmNewSubTasklet(foo2, userdata);

	/* instance a subtasklet relative to machine foo2 (shortcut syntax) */
	zm_State *s = zmNewSu(foo2, userdata);

The automatically free implies that it's not safe to store a tasklet 
pointer because can point to a free area of memory.

Tasklets are useful when `zm_State` reference is useless, for example in 
one-shot operations (operations without any suspend).


### Run tasks:

#### The Task Manager:
Task manager or virtual mapper `vm` cycle through active tasks and 
process them step by step.
A step (also called **machine step**) is the piece of code between the
current `zmstate` and the first yield or raise operator.

The main purpose of task manager in ZM is to remap the execution flow
defined by `zmstate`, `zmyield` and `zmraise`. For this reason is 
called virtual mapper.

#### Instance a VM:

	zm_VM *vm = zm_newVM(const char *name);

`name` is used only for debug message and cannot be NULL.

#### Run:

The main "play" command to run any kind of tasks is 

	int zm_go(zm_VM *vm, unsigned int nstep);

while if you want to run only task of a given task class: 

	int zm_goMachine(zm_VM *vm, zm_Machine* machine, unsigned int nstep);

where:

- `vm`: the task manager to play with.
- `machine`: a task class to be executed (`NULL` to execute any active tasks).
- `nsteps`: number of machine step to perform (zero means: 
  do nothing and return `ZM_RUN_AGAIN`).


`zm_go` is equivalent to `zm_goMachine` with a null `machine` pointer.

	zm_go(vm, 100);
	/* equals to */
	zm_goMachine(vm, NULL, 100);


These functions return a combination of these flags:

`ZM_RUN_IDLE` nothing to do
`ZM_RUN_AGAIN` there are some active task
`ZM_RUN_EXCEPTION` an exception have been raised but not catched (see 
 `zm_ucatch`)
`ZM_RUN_BREK` a vm break as been set (see `vm_break`).







## ZM Concept

Before speak about exceptions and tasks closing is better to explain 
some concept behind ZM.


### Execution-context:
A ptask and the set of its subtasks create an **execution-context**. 
In an execution-context only one between root-ptask or child-subtasks 
can be active at a specific time, in other words in the same context
operations are syncronous.


### Execution-stack:
Inside an execution-context a ptask can yield to a subtask and subtask to 
other subtasks. This can be represented as a stack: the **execution-stack**.

All elements in the execution-stack, with the only exception of the last one, 
are busy-waiting another subtask.


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


### Close tasks:

When a task receive a *close* command it changes its operational mode
from *normal-mode* (default) to **closing-mode**.

This is the list of command that send a *close* to a task:

- `zm_abort(...)` 
- `zmyield zmTERM` 
- `zmyield zmCLOSE(...)`
- `zmraise zmABORT(...)`

This commands affect not only the target task but also its subtask and
recursively all relative *data-tree* branch. 

There are two important feature of the close system: 

*The order of execution of the close is from leaves to root (implosion).*

This is due to resource allocation order of subtask: from root to leaves,
close must go in reversed order.

*The execution of the close is done in a serial way as any other operation*
*in the same execution-context.*

This avoid race-condition during resource free.


### Free tasks:

A task can be free only if it has just receive a *close*. The commands to
free a task are:

	/* free a ptask */
	zm_freeTask(vm_VM *vm, zm_State *task1);

	/* free a subtask */
	zm_freeSubTask(vm_VM *vm, zm_State *subtask2);

The free commands cannot be perfomed in a *sync way* because the task manager
should have to finish close operation before a task can be freed.



### The task destructor:

Every task that receive a close command will be resumed in a special
zmstate used for deallocate user data resources: `ZM_TERM`.

It is not mandatory to define this zmstate so may be omitted. 

This zmstate cannot be used in a direct yield:

		zmyield ZM_TERM; /* wrong: fatal error */
		zmyield zmSUB(foo, NULL) | ZM_TERM; /* wrong: fatal error */

It's automaticaly set as a resume point after a close operation 
(e.g. `yield zmTERM`).

In `ZM_TERM` the only permitted yield is: `yield zmEND`.

#### Example:
	ZMTASKDEF(foo2)
	{
		ZMSTART
		
		zmstate ZM_INIT: 
			zmdata = malloc(sizeof(int));;
			zmyield zmTERM;

		zmstate ZM_TERM:
			if (zmdata)
				free(zmdata);

			zmyield zmEND;

		ZMEND
	}



## Exception:

### Raise: 

There are two kind of exception in ZM:

- **abort exception**
- **continue exception**

An exception is raised with `zmraise` operator followed by:

1. `zmABORT(int code, const char* msg, void *data)` for abort exception
2. `zmCONTINUE(int code, const char* msg, void *data)` for continue exception


### Exception struct:

	typedef struct {
		int code;        /* the exception user code */
		const char* msg; /* the user message string */
		void *data;      /* user data */
		/* private fields */ 
	} zm_Exception;


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
- An abort-exception without a catch, cause `zm_go` stop and return
  `ZM_RUN_EXCEPTION`. The relative exception *must* be catch
  with `zm_ucatch`.
- A continue-exception without a catch will cause a fatal.
- In the catch zmstate, exception must be catch with `zmCatch()`
  (before the next yield). 


#### Abort Exception:
Raising an abort-exception implies that all tasks between the 
raise and the task before the catch will be aborted.

NOTE: This is not true if there is an abort reset (see below).

Example:

	ZMTASKDEF( subtask2 ) ZMSTART
		
		zmstate 1:
			printf("\t\tsubtask2: init\n");
			zmraise zmABORT(0, "example message", NULL);

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
				if (e->kind == ZM_EXCEPT_ABORT)
					zm_printException(NULL, e, 1);
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



#### Abort Exception Reset:
*Abort-reset* allow to avoid the abort behaviour, 
defining a zmstate as resume point.

The reset point is defined with this syntax:

	zmyield 4 | zmRESET(7)

To define a reset point in the raise itself use instead: 

	zmraise zmABORT(0, "test", NULL) | 5;

zmRESET cannot be applied to ptask.


#### Uncaught Abort Exception:
An abort exception without a catch will be caught by `zm_go` (or `zm_mGo`).
This function suddenly return a `ZM_RUN_EXCEPTION`. 

This particular exception is outside task class and must be catch
and free with special functions. 

To catch an uncaught exception use:

	zm_Exception *zm_ucatch(zm_VM *vm)

Exceptions caught inside a task are automatically free by task manager, 
instead uncaught exception must manually free:

	void zm_ufree(zm_VM *vm, zm_Exception *e);


**Note:** If an uncaught exception is ignored, the next uncaught
exception cause a fatal error. 

##### Example:

	int status;
	do {
		status = zm_go(vm, 100)

		if (status == ZM_RUN_EXCEPTION) {
			zm_Exception *e = zm_ucatch(vm);
			zm_printException(NULL, e, true);
			zm_ufreeException(vm, e);	
		}
	} while(status);


#### Continue Exception:

Continue exception have a very different behaviour from abort exception. 

*A continue exception is a kind of suspend-resume block.*

The continue exception suspend the execution of a subtask in the raise 
state and resume the state with the catch.

In this situation all the subtask between the raise and the subtask
before catch are a suspended block.

This block can be resumed using:

	zmyield zmUNRAISE(sub1, NULL) | 5;

This commands *don't resume* `sub1` but the state who raised the 
continue exception.

Instead of `zmUNRAISE` is possible also to use:

	zmyield zmSSUB(sub1, NULL) | 5;

`zmSSUB` act like `zmSUB` if sub1 is a suspended subtask or like `zmUNRAISE`
if `sub1` rappresent the handler of continue-exception suspended block.


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

### Event struct:

	typedef struct {
		void *data;    /* event user data */
		size_t count;  /* binded tasks count */
		/* private fields */ 
	} zm_Event;

### Create an Event:

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

- `ZM_TRIGGER` callback will be invoked in trigger operation.
- `ZM_UNBIND_REQUEST` callback will be invoked in unbind request:
   (`zm_unbind`, `zm_unbindAll`).
- `ZM_UNBIND_ABORT` callback will be invoked in the automatic unbind 
   during close operations (`zm_abort`). 

A shortcut for all unbind operation is `ZM_UNBIND` equivalent to 
`ZM_UNBIND_REQUEST | ZM_UNBIND_ABORT`.


The event callback is a function with this format:

	int (*zm_event_cb)(zm_VM *vm, int scope, zm_Event *e, zm_State *state,
	                                                            void *arg)

Where:

- `state` is the binded state.
- `scope` is the scope where the callback has just been invoked (it 
  can assume only one of the scope flag value).
- `arg` is:
	- the resume argument of `zm_trigger` in `ZM_TRIGGER` scope
	- the resume argument of unbind command in `ZM_UNBIND_REQUEST` scope
	- null in `ZM_UNBIND_ABORT` scope

The return value depend by the `scope` and the `state` values. 

**WARNING:** 
Use a ZM function inside the callback can produce impredicable behaviour.
The only purpose of `zm_Event* e` and `zm_State *state` arguments is to 
work on event data `e->data` and state data `state->data`.
For example never use `zm_resume`, `zm_trigger`, `zm_unbind`, `zm_abort`
inside the callback.


#### The trigger callback:

The trigger callback (event callback with the scope flag `ZM_TRIGGER`) have 
two purpose:

- filter task that can receive this event
- accomplish syncronous operation

`zm_trigger` first invoke the trigger callback in **pre-fetch mode** with
the argument relative to the the task equals to NULL. This 
is a generic filter that can accept or refuse the whole event.

If the event is accepted trigger go in **fetch mode** and  
invoked again the callback for each task binded to the event.


Callback return values in *pre-fetch mode*:

- `ZM_EVENT_ACCEPTED`: accept the event and go in fetch mode.
- `ZM_EVENT_REFUSED`: refuse the event.


Callback return values in *fetch mode*:

- `ZM_EVENT_ACCEPTED`: accept the event for the current task that will be 
   resumed and unbinded to the event (this is a trasparent internal unbind 
   operation that have nothing to do with the `ZM_UNBIND` callback scope).
- `ZM_EVENT_REFUSED`: refuse the event for the current task that continue to
   be binded to the event.
- `ZM_EVENT_STOP`: modifier to stop any other fetch. 
	- `ZM_EVENT_ACCEPTED | ZM_EVENT_STOP`
	- `ZM_EVENT_REFUSED | ZM_EVENT_STOP`


Example:

	int randcb(zm_VM *vm, int scope, zm_Event *e, zm_State *s, void **arg)
	{
		if (scope != ZM_TRIGGER) 
			return 0;

		if (s == NULL) {
			/* pre-fetch mode */
			if (rand() % 2)
				return ZM_EVENT_ACCEPTED;
			else
				return ZM_EVENT_REFUSED;
		} else {
			/* fetch mode */
			int r;
			r = (rand() % 2) ? ZM_EVENT_ACCEPTED : ZM_EVENT_REFUSED;

			if (rand() % 2)
				r = r | ZM_EVENT_STOP; 

			return r;
		}
	}



#### The unbind callback:

An unbind callback is an event callback with the scope flag `ZM_UNBIND_REQUEST` 
and/or `ZM_UNBIND_ABORT`.

The unbind callback is used only to perform syncronous operation, it has
not filtering purpose (as trigger callback). For this reason the return 
value have no meaning.


Unbind callback is relative to these scopes:

- `ZM_UNBIND_REQUEST` is used to manage explicit unbind request
  (`zm_unbind`, `zm_unbindAll` and `zm_freeEvent`)
- `ZM_UNBIND_ABORT` is used during close operations

The unbind callback is invoked for each binded tasks and return the number
of unbinded tasks.

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


### Free an Event:

	zm_freeEvent(vm, event);

An event can be free only if it has not more binded tasks.


### Event callback example:

	typedef struct {
		int id;
	} TaskData;

	int counter = 1;

	zm_Event * event;

	int getID(zm_State *s)
	{
		return ((TaskData*)(s->data))->id;
	}

	int eventcb(zm_VM *vm, int scope, zm_Event* e, zm_State *s, void *arg)
	{
		const char *msg = ((arg) ? ((const char*)arg) : ("null"));
		printf("\tcallback: arg = `%s` scope = ", msg);

		if (scope & ZM_UNBIND_REQUEST)
			printf("UNBIND_REQUEST ");

		if (scope & ZM_UNBIND_ABORT)
			printf("UNBIND_ABORT ");

		if (scope & ZM_TRIGGER)
			printf("TRIGGER ");

		printf("\n");

		if (!s) {
			if (scope & ZM_TRIGGER)
				printf("\t\t-> pre-fetch\n");
			else
				printf("\t\t-> post-fetch (pre-free)\n");

			return ZM_EVENT_ACCEPTED;
		}

		printf("\t\t-> fetch task %d ", getID(s));

		if ((scope & ZM_TRIGGER) && (getID(s) == 1)) {
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
			zmdata = self = malloc(sizeof(TaskData));
			self->id = counter++;
			printf("task %d: -init-\n", self->id);
			zmyield zmEVENT(event) | 2 | zmUNBIND(3);

		zmstate 2:
			printf("task %d: msg = `%s`\n", self->id, (const char*)zmarg);
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
		zm_trigger(vm, event, "Hello");
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
		callback: arg = `Hello` scope = TRIGGER 
			-> pre-fetch
		callback: arg = `Hello` scope = TRIGGER 
			-> fetch task 1 (accepted)
		callback: arg = `Hello` scope = TRIGGER 
			-> fetch task 2 (accepted but stop other fetches)

	* unbind s4:
		callback: arg = `I don't wait anymore` scope = UNBIND_REQUEST 
			-> fetch task 4 


	task 1: msg = `Hello`
	task 2: msg = `Hello`
	task 4: event aborted - msg = `I don't wait anymore`
	task 1: -end-
	task 2: -end-
	task 4: -end-

	* close all tasks:

		callback: arg = `null` scope = UNBIND_ABORT 
			-> fetch task 5 
		callback: arg = `null` scope = UNBIND_ABORT 
			-> fetch task 3 
	task 5: -end-
	task 3: -end-

	* free event:

		callback: arg = `null` scope = UNBIND_REQUEST 
			-> post-fetch (pre-free)




