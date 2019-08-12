

# ZM:

ZM is a C99 library to manage concurrent tasks based on finite state machine.


## Basic Concept:

ZM allow to instance tasks from a task class. A task class is defined
throught some zm-macros as follow:

    #include <stdlib.h>
    #include <zm.h>

    /* define class foo */
    ZMTASKDEF(foo)
    {
        /* common operation code block */
        printf("- [common]\n");

        ZMSTART

        zmstate ZM_INIT:
            /* constructor code block */
            printf("- init task\n");
            zmyield zmDONE;
            
        zmstate 1:
            /* code block 1 */
            printf("- hello\n");
            /* define the next resume point as zmstate 5 */
            zmyield 5;

        zmstate 5:
            /* code block 2 */
            printf("- world\n");
            zmyield zmTERM;

        zmstate ZM_TERM:
            /* distructor code block */
            printf("- end task\n");
            zmyield zmEND;

        ZMEND
    }

    int main()
    {
        /* instance scheduler */
        zm_VM *vm = zm_newVM("test VM");
        zm_State *s;

        printf("* start\n");

        /* instance a suspended task from class foo */
        s = zm_newTask(vm, foo, NULL);

        /* resume it */
        zm_resume(vm, s, NULL);

        printf("* run tasks...\n");

        /* run scheduler step by step */
        while(zm_go(vm, 1, NULL))
            printf("* (step)\n");

        /* free task */
        zm_freeTask(vm, s);
        
        /* free scheduler */
        zm_freeVM(vm);

        return 0;
    }


Output:

    * start
    - [common]
    - init task
    * run tasks...
    - [common]
    - hello
    * (step)
    - [common]
    - world
    * (step)
    - [common]
    - end task
    * (step)
    * (step)


note: the last `(step)` refer to internal close operations of task `s`.

**Task execution flow:**

The task class split code execution throught `zmstate` and `zmyield`
directive. 

The task execution flow is defined as:

1. Instance: 
   when a task is instanced it execute the code between 
   `ZMTASKDEF` - `ZMSTART` and the piece of code between 
   `zmstate ZM_INIT` - `zmyield zmDONE` (if it has been 
   defined).

2. First step:
   when the scheduler execute a task for the first time it execute
   the code between `ZMTASKDEF`- `ZMSTART` and the code between 
   `zmstate 1` and the first `zmyield`.

3. Other steps:
   other steps depend by first step and by external
   task action.

The yield operator can be used to send directive to task manager 
as wait an event or end task.
A simple `zmyield` followed by a number define the next `zmstate`
in the same task instance to be execute at the next scheduler cycle.


# Tasks:

In ZM there are two kind of tasks: **ptask** or *process-task*
and **subtask**. 

The term **task** is used in this documentation to identify 
a *generic task* (both ptask and subtask).

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

`ZMTASKDEF` create the `zm_Machine *foo`, `ZMSTART` is equivalent 
to a `switch`, `zmstate` to `case` while `ZMEND` is a kind of `default`.
For more information see *ZM look into* at the end of this document.

Between `ZMTASKDEF` and `ZMSTART` is possibile to define variable
or write piece of code that will executed before any zmstate.

## zmstates:
zmtates define the atomic execution blocks in a task.

    zmstate 100:
        /* atomic execution block - begin */

        ...

        zmyield ...
        /* atomic execution block - end */


`zmstate` is followed by a positive integer between 1 and 250 (value 
over 250 are reserved)

### The first zmstate:
`zmstate 1` is the default resume point for every new instanced task: 
it must always be present.

### Reserved zmstates:
There are two reserved zmstate `ZM_INIT` and `ZM_TERM`, these cannot be
use in a yield statement directly.


## Task manager:

Task manager is a scheduler that process actives tasks by **machine steps**.

A machine step is the piece of code between the current `zmstate` and the
first yield or raise operator.

Task manager is also called virtual mapper `vm` because it remap the
execution flow through operator like: `zmstate`, `zmyield`, `zmraise`.

### Instance:

    zm_VM *zm_newVM(const char *name)

This instance a new task manager (`name` is only for debug purpose, it
can be NULL).

### Run:

The main "play" command to run tasks scheduled by vm is:

    int zm_go(zm_VM *vm, unsigned int nstep, zm_Machine *m);

where:

- `vm` is the task manager.
- `m` is a task class filter (`NULL` to execute any active tasks).
- `nsteps` is number of machine step to be performed. If nstep is
  0 the task manager only return a `ZM_RUN_AGAIN`.


This function return `ZM_RUN_IDLE` if there is nothing to do
or a combination of these flags:

- `ZM_RUN_AGAIN` there are still active tasks
- `ZM_RUN_EXCEPTION` an exception have been raised but not catched 
                     (see `zm_uCatch`)
- `ZM_RUN_BREK` a vm break as been set (see `vm_break`).


`ZM_RUN_IDLE` is numerical equals to 0 so `zm_go` can be used inside 
a `while`:

    while((status = zm_go(vm, 100, NULL))) {}

### Free:

    zm_freeVM(zm_VM *vm);

This operation can be perfomed only when there is no more task associated
to this `vm`. 

For this reason it's safer perform a global close operation before:

    /* send a close to all vm tasks */
    zm_closeVM(vm); 
    
    /* wait the end of task close operations */
    while(zm_go(vm, 100, NULL)) {}

    /* free vm */
    zm_freeVM(vm);




## Process-Task:

A *ptask* is a [green thread](https://en.wikipedia.org/wiki/Green_threads).
Many *ptasks* can be active at the same time, emulating concurrently 
execution.

### Create a ptask: 

    zm_State* zm_newTask(zm_VM *vm, zm_Machine *m, void *taskdata)

This Instance a new ptask `task` relative to the task manager `vm`
associated to task class `m`. 
`taskdata` can be used to associate user data to the task (see below).

### Create a ptasklet:

A ptasklet is a ptask that have not to be manually free, it will automatically 
free after the close operation.

    zm_State* zm_newTasklet(zm_VM *vm , zm_Machine *m, void *taskdata)

Tasklet will be described in the *Exception and closing operation* chapter. 


### Resume a ptask:

Every ptask is created in suspend mode, to resume it:

    zm_resume(zm_VM *vm, zm_State *task, void *argument);

### Example:

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

        /* execute 30 machine step */
        zm_go(vm, 30, NULL);

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

If a process-task is like a thread, subtasks are like functions
executed inside a thread.

When a ptask yield to a subtask the ptask is temporary suspended
in a busy-waiting mode until the subtask don't term or suspend
its execution. 

This is the same behaviour of a function call from an abstracted
point of view:

1. pause the current code
2. active function code
3. wait the function processing 
4. resume the code after the function call


Subtasks can yield to other subtask like function call can be nested.



### Create a subtask:

Inside a task class is possible to instance one or more subtasks
with:

    zm_State* zmNewSubTask(zm_Machine* machine, void *subtaskdata)
    
    /* or a bit shorter */

    zm_State* zmNewSub(zm_Machine* machine, void *subtaskdata);

This can be done only in a task class (between `ZMTASKDEF` and `ZMEND`).


### Yield to a subtask (resume a subtask):

While a ptask can be resumed with `zm_resume` a subtask can be resumed 
only within the `zmyield` operator by a resume-yield operator like `zmSUB`.

This operation is named *yield to a subtask*: 

    zmyield zmSUB(zm_State *subtask, void *argument) | resumepoint;

`resumepoint` is the `zmstate` where the current task will
restart when `subtask` yield back to the curren task.


### Subtasklet

A subtasklet is a subtask that have not to be manually free, it will
automatically free after the close operation.

    /* instance a subtasklet */
    zm_State* zmNewSubTasklet(zm_Machine* machine, void *subtaskdata);

    /* instance a subtasklet (shortcut syntax) */
    zm_State* zmNewSu(zm_Machine* machine, void *subtaskdata);

A subtasklet have the same behaviour of a subtask: yield to a subtasklet
is performed in the same way with zmSUB.

Anyway there is a shortcut syntax to instance and yield to a subtasklet
with only one command:

    zmyield zmSU(zm_Machine *m, void *data, void *argument) | resumepoint;

This syntax allow to rewrite this code:

    zm_State *sub = zmNewSubTasklet(foo2, data);
    zmyield zmSUB(sub, arg) | 4;

as:

    zmyield zmSU(foo2, data, arg) | 4;
    
    

### Yield to the caller (return):

Yield to a subtask seen above is a kind of function call: subtask is 
activated and current task (*the caller*) is suspended in a busy 
waiting mode.

When subtask want to suspend or term its execution it can perform two 
kind of yield:

- `zmyield zmTERM`: *yield to end* close current task and resume the caller
- `zmyield zmCALLER`: *yield to the caller* suspend current task and resume
                      the caller.

Apparently these two yields perform the same operation from the caller 
point of view but caller can distinguish them defining two resume point:

    zmyield zmSUB(zm_State *subtask, void *argument) | epoint
            zmNEXT(ipoint);


- `ipoint` zmstate where the current task will restart when `subtask` yield
           to the caller.
- `epoint`  zmstate where the current task will restart when `subtask` yield
           to end.


### Example:

    #include <zm.h>

    zm_State *tmp;

    ZMTASKDEF(subfoo)
    {
        ZMSTART

        zmstate 1:
            printf("subfoo: one\n");
            zmyield zmSUSPEND | 2;

        zmstate 2:
            printf("subfoo: two\n");
            zmyield zmTERM;

        ZMEND
    }

    ZMTASKDEF(foo) 
    {
        ZMSTART

        zmstate 1:
            printf("foo: init\n");
            tmp = zmNewSub(subfoo, NULL);
            /* yield to tmp and resume in 2 */
            zmyield zmSUB(tmp, NULL) | 2;

        zmstate 2:
            printf("foo: again\n");
            /* yield to tmp and resume in 3 */
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
        zm_go(vm, 100, NULL);
        zm_freeTask(vm, task);
        zm_freeVM(vm);
    }

output:

    foo: init
    subfoo: one
    foo: again
    subfoo: two



## Case convention:

The code of a task class (everything between `ZMTASKDEF` and `ZMEND`)
is a special context.
Some function and operator like `zmyield` or `zmSUB` have meaning only inside
the **task class context** while others like `zm_resume` have no limitation
(*generic context*).

The main case convention rule is that commands that can be used only in 
*task class context* don't have underscore after *zm* prefix.


### Task class context:

1. **ZMABC**: definition operators
    - `ZMTASKDEF`
    - `ZMSTART`
    - `ZMEND`
2. **zmAbcXyz**: functions
    - `zmNewSubTask()`
    - `zmCatch()`
    - `zmCurrent()`
    - ...
3. **zmabc**: operators or variables
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

### Generic context:

1. **zm\_abcXyz**: functions that can be used inside or outside task class
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



## Task data:

Each task instance has a `void *data` field used to store data.

    typedef struct {
        /* ... */
        void *data;
    } zm_State;

The task data can be set during task creation:

    zm_State* zm_newTask(vm_VM* vm, zm_Machine *m, void *taskdata);
    zm_State* zmNewSub(zm_Machine *m, void *taskdata);

using the `data` field:

    zm_State *task = zm_newTask(vm, foo, NULL);

    task->data = taskdata;

or inside task using *zmdata* macro.

The main purpose of task data is to define permanent *local variables*.

### Task data in class definition:

Inside class definition task data can be reached with 
the help of **zmdata** variable.

`zmdata` can be used to get and set task data (it act like a variable but 
is a macro that will be replaced with `zmCurrent()->data`).


#### Example:

    #include <stdio.h>
    #include <stdlib.h>
    #include <zm.h>

    ZMTASKDEF(foo) {
        struct FooLocal {
            int i;
        } *self = zmdata;

        ZMSTART

        zmstate 1:
            zmdata = self = malloc(sizeof(struct FooLocal));
            self->i = 0;

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
        zm_go(vm, 5, NULL);
    }



### Persistent and temporary variables:

Task data can be used to define persistent variables associated to a task.
These variables exists since they are not manually free.

On the other side normal variables exists only in a machine step and for
this reason have meaning only inside a zmstate block. 

#### Example

    #include <stdlib.h>
    #include <stdio.h>
    #include <zm.h>

    struct FooLocal {
        int j;
    };

    ZMTASKDEF(foo) {
        struct FooLocal* self = zmdata; /* local */
        int i = 0; /* temporary */

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
        zm_go(vm, 5, NULL);
    }

output:

    i = 1  self->j = 1
    i = 1  self->j = 2
    i = 1  self->j = 3
    i = 1  self->j = 4
    i = 1  self->j = 5

In this example `i` is a temporary variable (exists only in a machine
step its content will be lost when a `zmyield` is reach) while `zmdata`
store persitent variables.


### Task constructor:

Allocate local variable before task instance (through global 
shared struct) is a strong design but the task is less 
"self-contained" and code have an higher "struct pollution".

    struct FooData {
        int i;
        int j;
        int k;
    };


    /* [...] */

    ZMTASKDEF(foo) {
        struct FooData *self = zmdata;

        /* [...] */
    }

    /* [...] */

    void instance_foo(zm_VM *vm) {
        struct FooData *d = malloc(struct FooData);
        zm_State *task;
        d->i = 0;
        d->j = 0;
        d->k = 0;
        task = zm_newTask(vm, foo, d);
    }


On the other side define task data in task class have a bettere 
readability, reduce the struct pollution but have a 
weaker design caused by some syncronization problems.


    ZMTASKDEF(foo) {
        struct Data {
            int i;
            int j;
            int k;
        } *self = zmdata;

        zmstate 1:
            zmdata = self = malloc(struct Data);
            self->i = 0;
            self->j = 0;
            self->k = 0;
            /* [...] */
    
        zmstate ZM_TERM:
            /* self can be NULL if this task is aborted before the
               scheduler process zmstate 1 */
            free(self);
    }



Task constructor allow to perform a special zmstate in a sync way
to allocate resource. This remove the syncronization issue and add
more flexibility to the task instance.

If `zmstate ZM_INIT` is defined in a task class this is used
during the instantiation as a syncronous constructor.

    zmstate ZM_INIT:
        /* allocate resources */
        zmyield zmDONE;

The constructor is perfomed in a special execution mode and should be
used only for instance and define task data.

The only permitted yield inside constructor is `zmyield zmDONE`.

NOTE: `ZM_INIT` is a reserved zmstate, so cannot be used in 
a yield or raise statement.

An important feature about task constructor is that `zmdata` can 
be used as constructor argument as shown in the next example.

### Example:

    #include <stdlib.h>
    #include <stdio.h>
    #include <zm.h>

    ZMTASKDEF(foo) {
        struct FooLocal {
            int i;
        } *self = zmdata;

        ZMSTART

        zmstate ZM_INIT: {
            int *n0 = zmdata;
            printf("foo: constructor(%d)\n", *n0);

            zmdata = self = malloc(sizeof(struct FooLocal));
            self->i = *n0;
            zmyield zmDONE;
        }

        zmstate 1:
            printf("foo: self->i = %d\n", self->i++);
            zmyield 1;

        zmstate ZM_TERM:
            printf("foo: distructor\n");
            free(self);

        ZMEND
    }

    int main()
    {
        zm_VM *vm = zm_newVM("test ZM");
        zm_State *task;
        int n0 = 4;
        task = zm_newTasklet(vm , foo, &n0);
        zm_resume(vm , task, NULL);
        zm_go(vm, 5, NULL);
        zm_closeVM(vm);
        zm_go(vm, 100, NULL);
        zm_freeVM(vm);
    }


output:

    foo: constructor(4)
    foo: self->i = 4
    foo: self->i = 5
    foo: self->i = 6
    foo: self->i = 7
    foo: self->i = 8
    foo: distructor



## The Resume Argument:

The local variables system above is a way to store data and can 
also be used to exchange messages as in task constructor.

Anyway, outside task constructor, exchange messages through task data
entail some kind of variable sharing that can be source of syncronization
issue. To avoid this kind of problem and have a better control on the 
resume operation is possibile to use the **resume argument**.

*Resume argument* is a pointer that can be defined:

- in all resume function (`zm_resume`)
- in all resume-yield operator (`zmSUB`)
- before yield back to caller (`zmyield zmCALLER`, `zmyield zmTERM`)

This pointer will be accessible in the first machine step on resumed task.

In task class the resume argument pointer is stored inside **zmarg**.


### Resume argument as a parameter:

Each resume functions or operators allow to set the resume argument. 
This act like a task parameter:

- `zm_resume(zm_VM* vm, zm_State* task, void* resumearg)`
- `zmSUB(zm_State* task, void* resumearg)`
- `zmSSUB(zm_State* task, void* resumearg)`
- `zmUNRAISE(zm_State* task, void* resumearg)`
- `zm_trigger(zm_VM *vm, zm_Event* event, void* resumearg)`

### Resume argument as a return:

Resume argument can also be used as `return`. A subtask can set it
before yield to end or to the caller using `zmresult`.

`zmresult` is a macro that act like a variable (can be used only inside 
a subtask).

    zmresult = (void*)data;
    zmyield zmTERM;

### Example:

    #include <stdio.h>
    #include <zm.h>


    ZMTASKDEF( Foo )
    {
        const char* arg = (zmarg) ? (const char*)(zmarg) : "<none>";

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
        zm_go(vm, 5, NULL);

        zm_resume(vm, f, "How are you?");
        zm_go(vm, 5, NULL);

        zm_closeVM(vm);
        zm_go(vm, 100, NULL);
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



## Task class syntaxes:

There are some equivalent syntax in task class for example `ZMSTATES`
can be used in place of `ZMSTART`.
Moreover `ZMTASKDEF` and `ZMEND` implicit use curly braces `{}` making
possible to avoid them. All this features allow to define
many syntax combinations, for example the task `foo2`:

    ZMTASKDEF(foo2)
    {
        ZMSTART
        zmstate 1:
            zmyield zmTERM;
        ZMEND
    }

can also be written as:

    ZMTASKDEF(foo2)
        ZMSTART
        zmstate 1:
            zmyield zmTERM;
        ZMEND

or like:

    ZMTASKDEF(foo2) ZMSTATES
        zmstate 1:
            zmyield zmTERM;
    ZMEND






# Exception and closing operations:

Before speak about exceptions and tasks closing is better to explain 
some concept behind ZM.

## Execution-context:
A ptask and the set of its subtasks create an **execution-context**. 
In an execution-context only one between root-ptask or child-subtasks 
can be active at a specific time, in other words in the same context
operations are syncronous.


## Execution-stack:
Inside an execution-context a ptask can yield to a subtask and subtask to 
other subtasks. This can be represented as a stack: the **execution-stack**.

All elements in the execution-stack, with the only exception of the last one, 
are busy-waiting another subtask.


## Data-tree:

The *execution-stack* keep track of yields but there is another important
stored relation: the association between a task and its subtasks:

**each task keep the list of its subtask instances**

The set of this relations can be represented as a tree where ptask is
the root and subtasks are branches and leaves: this is the **data-tree**.

The main purpose of *data-tree* is to deal with the task resource deallocation.

User task-data allocation follow the *data-tree* structure from root to 
leaves direction so the deallocation must follow the same structure in the 
reversed direction.


## Close tasks:

When a task receive a *close* command it changes its operation mode
from *normal-mode* (default) to **closing-mode**.

This is the list of command that send a *close* to a task:

- `zm_abort(...)` 
- `zmyield zmTERM` 
- `zmyield zmCLOSE(...)`
- `zmraise zmABORT(...)`

This commands affect not only the target task but also its subtask and
recursively all relative *data-tree* branch. 

There are two important feature of the close system: 

*1) The order of execution of the close is from leaves to root (implosion).*

This is due to resource allocation order of subtask: from root to leaves so
close must go in reversed order.

*2) The execution of the close is done in a serial way as any other operation*
*in the same execution-context.*

This avoid race-condition during resource free.



## The task destructor:

Every task that receive a close command will be resumed in a special
zmstate used for deallocate user data resources: `ZM_TERM`.

It is not mandatory to define it (can be omitted). 

`ZM_TERM`is a reserved zmstate so can't be used within a yield:

        /* wrong yield: cause fatal */
        zmyield ZM_TERM;

        /* use instead a close operation like */
        zmyield zmTERM;


In `ZM_TERM` the only permitted yield is: `yield zmEND`.

### Example:
    ZMTASKDEF(foo2)
    {
        ZMSTART
        
        zmstate ZM_INIT: 
            zmdata = malloc(sizeof(int));;
            zmyield zmDONE;

        zmstate 1:
            zmyield 2;

        zmstate 2:
            zmyield zmTERM;

        zmstate ZM_TERM:
            free(zmdata);
            zmyield zmEND;

        ZMEND
    }

## Free tasks:

A task can be free only if it has just receive a *close*. The commands to
free a task are:

    /* free a ptask */
    zm_freeTask(vm_VM *vm, zm_State *task1);

    /* free a subtask */
    zmFreeSubTask(zm_State *subtask2);

    /* free a subtask (shortcut syntax) */
    zmFreeSub(zm_State *subtask2);


The free commands cannot be perfomed in a *sync way* because the task manager
should have to finish close operation before a task can be freed.

## Tasklet:

A tasklet is a task that have not to be manually free, it will automatically 
free after the close operation.

    /* instance a ptasklet relative to machine foo */
    zm_State* s = zm_newTasklet(vm , foo, userdata);

    /* instance a subtasklet relative to machine foo2 */
    zm_State* s = zmNewSubTasklet(foo2, userdata);

    /* instance a subtasklet relative to machine foo2 (shortcut syntax) */
    zm_State* s = zmNewSu(foo2, userdata);

The automatically free implies that it's not safe to store a tasklet 
pointer because can point to a free area of memory.

Tasklets are useful when `zm_State` reference is useless, for example in 
one-shot operations (operations without any suspend).




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



### Exception feature and rule:

- Exceptions can be raised only inside a subtask and can be catch 
  in other subtasks or in the root-ptask.
- An abort-exception without a catch, cause `zm_go` stop and return
  `ZM_RUN_EXCEPTION`. The relative exception *must* be catch
  with `zm_uCatch`.
- A continue-exception without a catch will cause a fatal.
- In the catch zmstate, exception must be catch with `zmCatch()`
  (before the next yield). 


#### Abort Exception:
Raising an abort-exception implies that all tasks between the 
raise and the task before the catch will be closed.

NOTE: This is not true if there is an abort reset (see below).

Example:

    #include <stdio.h>
    #include <zm.h>

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
                if (e->kind == ZM_EXCEPTION_ABORT)
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
        zm_go(vm, 100, NULL);
        zm_closeVM(vm );
        zm_go(vm, 100, NULL);
        zm_freeVM(vm);
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
       machine: subtask    [zmstate: 2]
       filename: exception_catch.c
       nline: 27
       --------------------
       machine: subtask2    [zmstate: 1]
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
An abort exception without a catch will be caught by `zm_go` 
This function suddenly return a `ZM_RUN_EXCEPTION`. 

This particular exception is outside task class and must be catch
and free with special functions. 

To catch an uncaught exception use:

    zm_Exception *zm_uCatch(zm_VM *vm)

Exceptions caught inside a task are automatically free by task manager, 
instead uncaught exception must manually free with:

    void zm_uFree(zm_VM *vm, zm_Exception *e);


**Note:** If an uncaught exception is ignored, the next uncaught
exception cause a fatal error. 

##### Example:

    int status;
    do {
        status = zm_go(vm, 100)

        if (status == ZM_RUN_EXCEPTION) {
            zm_Exception *e = zm_uCatch(vm);
            zm_printException(NULL, e, true);
            zm_uFree(vm, e);    
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

    zm_Event *event = zm_newEvent(void *data);

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



## ZM look into:

The idea behind ZM is to label and split code in a function with 
`switch`-`case` and use `return` to yield to another piece of code.

For example the following code: 

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

is converted by C-preprocessor in something like this:

    zm_Machine *foo = zm_newMachine(__foo__);

    int32 __foo__(uint8_t zmop) {
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

`zm_Machine *foo` is a task class used to create task instances (`zm_State`):

The core of the coroutine and task behaviour is inside `zmstate` and `zmyield`
operators.

- `zmstate` label and split task code in atomic execution blocks.
- `zmyield` control which `zmstate` will be run next.

For example:
    
    zmyield 2;

(converted in `return 2;`) tell to the scheduler that the 
next `zmstate` to be excuted is `zmstate 2`.

Each byte of the four byte returned (32 bit integer) by the `zmyield`
operator have a particular meaning: one is the directive while others 
represent the directive arguments.

for examples:

    /* 1 */
    zmyield 2;

    /* 2 */
    zmyield zmSUSPEND | 3;

    /* 3 */
    zmyield zmSUB(footask, NULL) | 4 | zmNEXT(5) | zmCATCH(7);


1. *yield to zmstate 2*: 
   - directive: inner yield (implicit)
   - argument: `2` (current task restart in zmstate 2)

2. *yield to suspend and resume in zmstate 3*:
   - directive: suspend current task
   - argument: `3` (current task restart in 3 when it
     will be resumed)

3. *yield to footask, resume in 4, iter in 5 and catch in 7*: 
   - directive: suspend current task and resume subtask `footask`
   - argument `4` (resume point when `footask` term)
   - argument `5` (resume point when `footask` yield to caller)
   - argument `7` (resume point when `footask` raise an exception)
  
### A simple implementation: 

A raw implementation of the schema described above is:

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

In this example `return` is the yield-operator, `foo` represent
a task-class, `s1` a task instance and `while` is the scheduler.


