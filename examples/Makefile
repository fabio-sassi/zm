#FLAGS=-O3 -std=c99 -Wall -DZM_DEBUG_LEVEL=5 -I../../ ../../zm.c
FLAGS=-O3 -std=c99 -Wall -I../ ../zm.c
DEP=../zm.h ../zm.c

CC=gcc
#CC=clang

all: taskdef basic subtask errexcept conexcept event

taskdef: simple.bin defstyles.bin extern.bin

basic: helloworld.bin localvar.bin arg.bin

subtask: itersub.bin hellosomeworlds.bin yieldto.bin argsub.bin

errexcept: errcatch.bin extcatch.bin reset.bin

conexcept: unraise.bin search.bin

event: waitinghelloworlds.bin eventcb.bin lock.bin lock2.bin


%.bin: %.c $(DEP) 
	$(CC) $(FLAGS) $< -o $@

extern.bin: extern1.c extern2.c $(DEP)
	$(CC) $(FLAGS) extern1.c extern2.c -o extern.bin 

clean:
	rm *.bin
