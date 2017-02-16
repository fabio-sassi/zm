Error exception example (without reset):

	ZMTASKDEF(task3) ZMSTATES
		zmstate 1:
			raise zmERROR(0, "test exception", NULL);
	ZMEND			

	ZMTASKDEF(task2) ZMSTATES
		zmstate 1:{
			zm_State *s = zmNewSubTasklet(task3, NULL);
			yield zmSUB(s) | 2;
		}
		zmstate 2: 
			yield zmTERM;
	ZMEND			


	ZMTASKDEF(task1) ZMSTATES
		zmstate 1: {
			zm_State *s = zmNewSubTasklet(task2, NULL);
			yield zmSUB(s) | 2 | zmCATCH(3);
		}
		zmstate 2: 
			yield zmTERM;
	
		zmstate 3: {
			zm_Exception* e = zmCatch();
			if (e)
				zmFreeException();
			yield zmTERM;
		}
		zmstate ZM_TERM:
			/* alway check for uncaught exception caused by 
			   double abort */
			if (zmCatch()) 
				zmFreeException();

			yield zmEND;
	ZMEND			

	void main() {
		zm_VM *vm = zm_newVM("test");
		zm_resume(vm, zm_newTasklet(vm, task1, NULL));
		zm_go(vm, 100);
	}



Error exception example (with reset):

	ZMTASKDEF(task3) ZMSTATES
		zmstate 1:
			// an exception loop :o
			raise zmERROR(0, "test exception", NULL) | 1;
	ZMEND			

	ZMTASKDEF(task2) ZMSTATES
		zmstate 1:{
			zm_State *s = zmNewSubTasklet(task3, NULL);
			yield zmSUB(s) | 2 | zmRESET(1);
		}
		zmstate 2: 
			yield zmTERM;
	ZMEND			


	ZMTASKDEF(task1) ZMSTATES
		zmstate 1: {
			zm_State *s = zmNewSubTasklet(task2, NULL);
			yield zmSUB(s) | 2 | zmCATCH(3);
		}
		zmstate 2: 
			yield zmTERM;
	
		zmstate 3: {
			zm_Exception* e = zmCatch();
			if (e)
				zmFreeException();
			yield zmTERM;
		}
		zmstate ZM_TERM:
			/* alway check for uncaught exception caused by 
			   double abort */
			if (zmCatch()) 
				zmFreeException();

			yield zmEND;
	ZMEND			

	void main() {
		zm_VM *vm = zm_newVM("test");
		zm_resume(vm, zm_newTasklet(vm, task1, NULL));
		zm_go(vm, 100);
	}

