#include <stdlib.h>
#include <zm.h>

void printTree(zm_Print *p)
{
	zm_print(p, "Head\n");

	zm_addIndent(p, 4);
	zm_print(p, "1\n");
	zm_print(p, "2\n");

	zm_addIndent(p, 4);
	zm_print(p, "2.1\n");
	zm_print(p, "2.2\n");

	zm_addIndent(p, 4);
	zm_print(p, "2.2.3\n");
	zm_print(p, "2.2.4\n");

	zm_addIndent(p, -4);
	zm_print(p, "2.3\n");

	zm_addIndent(p, -4);
	zm_print(p, "3\n");

	zm_addIndent(p, -4);
	zm_print(p, "end\n");
}


int main()
{
	zm_Print p;
	int i;

	zm_initPrint(&p, stdout, 0);

	printTree(&p);

	for (i = 0; i < 120; i+= 10) {
		p.indent = i;
		zm_print(&p, "indent %d\n", i);
	}

	return 0;
}
