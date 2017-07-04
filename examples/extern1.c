#include <zm.h>

/* Extern task definition 1/2 */

extern zm_Machine *mycoroutine;

int main() {
	zm_VM *vm = zm_newVM("test VM");
	zm_State *s = zm_newTask(vm, mycoroutine, NULL);
	zm_resume(vm, s, NULL);
	zm_go(vm, 100);
	return 0;
}
