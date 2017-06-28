## Examples: 
This folder contain some ZM examples 

### Task definition:

- A simple task with ZM: [simple.c](simple.c)
- Two example of task definition style: [defstyles.c](defstyles.c)
- Declarare and define a task in different files: [extern1.c](extern1.c), 
  [extern2.c](extern2.c)

### Some basic examples:

- Hello world as a task: [helloworld.c](helloworld.c)
- Local variables in a task: [localvar.c](localvar.c)
- Resume argument in a task: [arg.c](arg.c)


### PTask and Subtask:

- A task with a subtask: [itersub.c](itersub.c)
- Hello world with some concurrent task: [hellosomeworlds.c](hellosomeworlds.c)
- Difference between `zmTO` and `zmSUB`: [yieldto.c](yieldto.c)
- Resume argument in a task and subtask response: [argsub.c](argsub.c)

### Error Exception:

- A simple error exception catch with traceback: [errcatch.c](errcatch.c)
- An external error exception catch (cause by an error that haven't been 
  catch in task): [extcatch.c](extcatch.c)
- An example of error exception reset: [reset.c](reset.c)

### Continue Exception:

- An example about raising and unraising a continue exeception: [unraise.c](unraise.c)

### Events:

- Hello world with and an event [waitinghelloworlds.c](waitinghelloworlds.c)
- Trigger and unbind event callback [trigcb.c](trigcb.c) 
- A simple lock/unlock system [lock.c](lock.c) 

