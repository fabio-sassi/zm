## Examples: 
This folder contain some ZM examples 

### Task definition:

- A simple task with ZM: [simple.c](simple.c)
- Two examples of task definition style: [defstyles.c](defstyles.c)
- Declarare and define a task in different files: [extern1.c](extern1.c), 
  [extern2.c](extern2.c)

### Some basic examples:

- Hello world as a task: [helloworld.c](helloworld.c)
- Local variables in a task: [localvar.c](localvar.c)
- Local variables in a task with constructor and distructor [localvar22.c](localvar2.c)
- Resume argument in a task: [arg.c](arg.c)


### PTask and Subtask:

- A task with a subtask: [itersub.c](itersub.c)
- Hello world with some concurrent task: [hellosomeworlds.c](hellosomeworlds.c)
- Differences between `zmTO` and `zmSUB`: [yieldto.c](yieldto.c)
- Resume argument in a task and subtask response: [argsub.c](argsub.c)

### Error Exception:

- A simple error exception catch with traceback: [errcatch.c](errcatch.c)
- An external error exception catch (cause by an error that haven't been 
  catch in task): [extcatch.c](extcatch.c)
- An example of error exception reset: [reset.c](reset.c)

### Continue Exception:

- Raise and unraise a continue exception: [unraise.c](unraise.c)

### Events:

- Hello world with an event [waitinghelloworlds.c](waitinghelloworlds.c)
- Trigger and unbind event callback [eventcb.c](eventcb.c) 
- A simple task lock system [lock.c](lock.c)


### Advanced:

- Sync and async local variables instance in a task: [localvar3.c](localvar3.c)

- A bit more advanced task lock system with force unlock and owner check
  [lock2.c](lock2.c)
  
- Uppercase the best matching substring pattern in a text: [search.c](search.c)

