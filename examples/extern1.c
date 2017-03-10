/*
Fabio Sassi 100% Public Domain
Extern task definition 1/2
*/

#include <zm.h>

extern zm_Machine *mycoroutine;


int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s = zm_newTask(vm, mycoroutine, NULL);
	zm_resume(vm, s);
	zm_go(vm, 100);
	return 0;
}
