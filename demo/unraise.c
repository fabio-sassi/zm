/*
Fabio Sassi 100% Public Domain
Continue Exception raise/unraise example
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zm.h>

zm_State *sub2;

ZMTASKDEF(task3) ZMSTATES
    zmstate 1:
        printf("\t\ttask3: init\n");
        printf("\t\ttask3: stop this task by raising continue (*)\n");
        zmraise zmCONTINUE(0, "test", NULL) | 2;
    zmstate 2:
        printf("\t\ttask3: (*) unraised ... OK\n");
    zmstate 3:
        printf("\t\ttask3: no more to do ... term\n");
        zmyield zmTERM;
ZMEND


ZMTASKDEF(task2) ZMSTATES
    zmstate 1:{
        printf("\ttask2: init\n");
        zm_State *s = zmNewSubTasklet(task3, NULL);
        zmyield zmSUB(s) | 2;
    }
    zmstate 2:
        printf("\ttask2: term\n");
        zmyield zmTERM;
ZMEND


ZMTASKDEF(task1) ZMSTATES
    zmstate 1: {
        printf("task1: init\n");
        sub2 = zmNewSubTasklet(task2, NULL);
        zmyield zmSUB(sub2) | 2 | zmCATCH(3);
    }
    zmstate 2:
        printf("task1: term\n");
        zmyield zmTERM;
    zmstate 3: {
        printf("task1: catch\n");
        zm_Exception* e = zmCatch();
        if (e)
            zmFreeException();
        zmyield 4;
    }
    zmstate 4:
        printf("task1: 1/2\n");
        zmyield 5;
    zmstate 5:
        printf("task1: 2/2\n");
        /* this resume the raise subtask: task3 */
        zmyield zmUNRAISE(sub2) | 2;
ZMEND

int main() {
    zm_VM *vm = zm_newVM("test");
    zm_resume(vm , zm_newTasklet(vm , task1, NULL));
    zm_go(vm , 100);
    return 0;
}

