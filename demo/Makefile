#FLAGS=-O3 -std=c99 -Wall -DZM_DEBUG_LEVEL=5 -I../../ ../../zm.c
FLAGS=-O3 -std=c99 -Wall -I../ ../zm.c
DEP=../zm.h ../zm.c


all: simple.bin simple2.bin extern.bin itersub.bin catch_error.bin unraise.bin yieldto.bin

%.bin: %.c $(DEP) 
	gcc $(FLAGS) $< -o $@

extern.bin: extern1.c extern2.c $(DEP)
	gcc $(FLAGS) extern1.c extern2.c -o extern.bin 

