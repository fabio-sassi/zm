#FLAGS=-O3 -std=c99 -Wall -DZM_DEBUG_LEVEL=5 -I../../ ../../zm.c
FLAGS=-g -std=c99 -Wall -pedantic -I../ ../zm.c
DEP=../zm.h ../zm.c

#CC=gcc
#CC=clang

all: taskdef basic subtask errexcept conexcept event advanced

taskdef: simple.bin defstyles.bin extern.bin

basic: helloworld.bin localvar.bin localvar2.bin arg.bin

subtask: itersub.bin helloworlds.bin yieldto.bin argsub.bin

errexcept: errcatch.bin extcatch.bin reset.bin

conexcept: unraise.bin 

event: waitinghelloworlds.bin eventcb.bin lock.bin

advanced: search.bin lock2.bin localvar3.bin

test: print.bin wrongyield.bin unexpected.bin



# taskdef

simple.bin: $(DEP) simple.c
	$(CC) $(FLAGS) simple.c -o simple.bin

defstyles.bin: $(DEP) defstyles.c
	$(CC) $(FLAGS) defstyles.c -o defstyles.bin

extern.bin: extern1.c extern2.c $(DEP)
	$(CC) $(FLAGS) extern1.c extern2.c -o extern.bin 



# basic

helloworld.bin: $(DEP) helloworld.c
	$(CC) $(FLAGS) helloworld.c -o helloworld.bin

localvar.bin: $(DEP) localvar.c
	$(CC) $(FLAGS) localvar.c -o localvar.bin

localvar2.bin: $(DEP) localvar2.c
	$(CC) $(FLAGS) localvar2.c -o localvar2.bin

helloworlds.bin: $(DEP) helloworlds.c
	$(CC) $(FLAGS) helloworlds.c -o helloworlds.bin

arg.bin: $(DEP) arg.c
	$(CC) $(FLAGS) arg.c -o arg.bin



# subtask

itersub.bin: $(DEP) itersub.c
	$(CC) $(FLAGS) itersub.c -o itersub.bin

yieldto.bin: $(DEP) yieldto.c
	$(CC) $(FLAGS) yieldto.c -o yieldto.bin

argsub.bin: $(DEP) argsub.c
	$(CC) $(FLAGS) argsub.c -o argsub.bin



# errexcept

errcatch.bin: $(DEP) errcatch.c
	$(CC) $(FLAGS) errcatch.c -o errcatch.bin

extcatch.bin: $(DEP) extcatch.c
	$(CC) $(FLAGS) extcatch.c -o extcatch.bin

reset.bin: $(DEP) reset.c
	$(CC) $(FLAGS) reset.c -o reset.bin



# conexcept

unraise.bin: $(DEP) unraise.c
	$(CC) $(FLAGS) unraise.c -o unraise.bin



# event

waitinghelloworlds.bin: $(DEP) waitinghelloworlds.c
	$(CC) $(FLAGS) waitinghelloworlds.c -o waitinghelloworlds.bin

eventcb.bin: $(DEP) eventcb.c
	$(CC) $(FLAGS) eventcb.c -o eventcb.bin

lock.bin: $(DEP) lock.c
	$(CC) $(FLAGS) lock.c -o lock.bin



# advanced

search.bin: $(DEP) search.c
	$(CC) $(FLAGS) search.c -o search.bin

lock2.bin: $(DEP) lock2.c
	$(CC) $(FLAGS) lock2.c -o lock2.bin

localvar3.bin: $(DEP) localvar3.c
	$(CC) $(FLAGS) localvar3.c -o localvar3.bin



# test

print.bin: $(DEP) print.c
	$(CC) $(FLAGS) print.c -o print.bin

wrongyield.bin: $(DEP) wrongyield.c
	$(CC) $(FLAGS) wrongyield.c -o wrongyield.bin

unexpected.bin: $(DEP) wrongyield.c
	$(CC) $(FLAGS) -DZM_DEBUG_MACHINENAME -DUNEXP wrongyield.c -o unexpected.bin


clean:
	rm *.bin

