/*
 * Copyright (c) 2015-2017, Fabio Sassi <fabio dot s81 at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <zm.h>


size_t zmg_mcounter = 0;


#define zm_isTermState(s) ((s) == ZM_TERM)
#define zm_isntTermState(s) ((s) != ZM_TERM)


/*---------------------------------------------------------------------------
 *  state traversing
 *  -----------------------------------------------------------------------*/


static int zm_haveComeback(zm_State *s)
{
	if (!s->parent)
		return false;

	return s->parent->comeback != NULL;
}

zm_State* zm_getCaller(zm_State *s)
{
	return (zm_isSubTask(s)) ? s->parent->comeback : NULL;
}

zm_State* izmGetCaller(zm_VM *vm)
{
	return zm_getCaller(zm_getCurrentState(vm));
}

static size_t zm_deep(zm_State *sub)
{
	return sub->parent->stacksize;
}


size_t zm_getDeep(zm_State *s)
{
	return (zm_isSubTask(s)) ? s->parent->stacksize : 0;
}

zm_State* izmGetParent(zm_VM *vm, size_t n, const char *fn, int nl)
{
	zm_State *s = zm_getCurrentState(vm);

	if (zm_isTask(s)) {
		return NULL;
	}

	if (n >= zm_deep(s)) {
		zm_fatalOn("zmGetParent", fn, nl);
		zm_fatalDo(ZM_FATAL_UCODE, "GETP.M", vm, "parent max deep is "
		           "%d but requested %d", zm_deep(s) - 1, n);
	}

	return s->parent->stack[zm_deep(s) - n - 1];
}

size_t izmGetDeep(zm_VM *vm)
{
	zm_State *s = zm_getCurrentState(vm);

	if (zm_isTask(s)) {
		return 0;
	}

	return zm_deep(s);
}

zm_State* izmGetRoot(zm_VM *vm)
{
	zm_State *s = zm_getCurrentState(vm);

	if (zm_isTask(s)) {
		return NULL;
	}

	return s->parent->stack[0];
}


/*---------------------------------------------------------------------------
 *  MEMORY UTILITY
 *  -----------------------------------------------------------------------*/




static void zm_memfatal(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}



void *zm_malloc(size_t size)
{
	void *ptr = malloc(size);
	if (!ptr)
		zm_memfatal("zm_malloc: out of mem\n");

	return ptr;
}

void *zm_mrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);

	if (!ptr)
		zm_memfatal("zm_mrealloc: out of mem\n");

	return ptr;
}

void zm_mfree(size_t size, void *ptr)
{
	free(ptr);
}

/*---------------------------------------------------------------------------
 *  PRINT UTILITY
 *  -----------------------------------------------------------------------*/

#ifdef ZM_FUNNYPRINT
	#include "zm_funny.h"

	#define zm_debug_vfprintf zm_funny_vfprintf
	#define zm_debug_vsprintf zm_funny_vsprintf
#else
	#define zm_debug_vfprintf vfprintf
	#define zm_debug_vsprintf vsprintf
#endif

static void zm_log(const char *fmt, ...)
{
	FILE *out = stdout;
	va_list args;

	fprintf(out, "zmDEBUG - ");

	va_start(args, fmt);
	zm_debug_vfprintf(out, fmt, args);
	va_end(args);

	fprintf(out, "\n");

	fflush(out);
}

void zm_initPrint(zm_Print *p, FILE *stream, int indent, int buf)
{
	p->file = stream;
	p->indent = indent;

	if (buf) {
		p->buffer.data = (char*)malloc(512);
		p->buffer.used = 0;
		p->buffer.size = 512;
	} else {
		p->buffer.data = NULL;
	}
}

char* zm_popPrintBuffer(zm_Print *out)
{
	char* b = out->buffer.data;
	out->buffer.data = NULL;
	return b;
}


void zm_removePrintBuffer(zm_Print *out)
{
	free(out->buffer.data);
	out->buffer.data = false;
}

static int zm_havePrintBuffer(zm_Print *out, int len)
{
	if (!out->buffer.data)
		return false;

	if (len < 0) {
		zm_removePrintBuffer(out);
		return false;
	}

	if (out->buffer.used + len >= out->buffer.size) {
		void *ptr = (void*)out->buffer.data;

		out->buffer.size = out->buffer.used + len + 512;

		ptr = realloc(ptr, out->buffer.size);
		if (!ptr) {
			zm_removePrintBuffer(out);
			return false;
		}

		out->buffer.data = (char*)ptr;
	}

	return true;
}


/* indent print */
void zm_iprint(zm_Print *out, const char *fmt, ...)
{
	va_list args;
	int len = out->indent;
	char *b;
	int i = 0;

	for (i = 0; i < out->indent; i++)
		fprintf(out->file, " ");

	va_start(args, fmt);
	len += zm_debug_vfprintf(out->file, fmt, args);
	va_end(args);

	fflush(out->file);

	if (!zm_havePrintBuffer(out, len))
		return;

	b = out->buffer.data + out->buffer.used;


	for (i = 0; i < out->indent; i++) {
		sprintf(b, " ");
		b++;
	}

	va_start(args, fmt);
	zm_debug_vsprintf(b, fmt, args);
	va_end(args);

	out->buffer.used += len;
}


void zm_print(zm_Print *out, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = zm_debug_vfprintf(out->file, fmt, args);
	va_end(args);

	fflush(out->file);

	if (!zm_havePrintBuffer(out, len))
		return;


	va_start(args, fmt);
	zm_debug_vsprintf(out->buffer.data + out->buffer.used, fmt, args);
	va_end(args);
	out->buffer.used += len;
}


static int zm_vprint2(zm_Print *out, int len, const char *fmt, va_list args)
{
	if (len < 0) {
		len = zm_debug_vfprintf(out->file, fmt, args);

		fflush(out->file);

		return len;

	} else {
		char *data;

		if (!zm_havePrintBuffer(out, len))
			return 0;

		data = out->buffer.data + out->buffer.used;

		zm_debug_vsprintf(data, fmt, args);

		out->buffer.used += len;

		return len;
	}
}

void zm_setIndent(zm_Print *out, int indent)
{
	out->indent = indent;
}

void zm_addIndent(zm_Print *out, int indent)
{
	out->indent += indent;
}


/*---------------------------------------------------------------------------
 *  ERROR  REPORTING
 *  -----------------------------------------------------------------------*/


void zm_printVM(zm_Print *, zm_VM *);
void zm_printState(zm_Print *, zm_State *);
static void zm_printHeaderVM(zm_Print *, zm_VM *);
static void zm_printError(zm_Print*, zm_Exception*, int);
static void zm_printErrorTrace(zm_Print *out, zm_Exception *e);
static const char* zm_getMachineOpName(zm_State *s, int compact);


typedef struct {
	struct {
		const char *reference;
		const char *filename;
		int nline;
	} ucode;


	zm_Exception *exception;
	int locked;

	struct {
		zm_fatal_cb fatalcb;
		void *data;
	} at;

} zm_Fatal;


/*zm_Fatal zmg_err = {
                           {reference: NULL, filename: NULL, nline: 0},
                           exception: NULL,
                           locked: false,
                           {fatalcb: NULL, data: NULL}
                           };*/

zm_Fatal zmg_err = { {NULL, NULL, 0,}, NULL, false, {NULL, NULL}};





void zm_atFatal(zm_fatal_cb cb, void *data)
{
	zmg_err.at.fatalcb = cb;
	zmg_err.at.data = data;
}


void zm_fatalOn(const char *refname, const char *fn, int nl)
{
	zmg_err.ucode.reference = refname;
	zmg_err.ucode.filename = fn;
	zmg_err.ucode.nline = nl;
}


void zm_fatalException(zm_Exception *e)
{
	zmg_err.exception = e;
}


static void zm_fatalPrintECode(zm_Print *out, const char *ecode)
{
	zm_print(out, "\n\n");

	zm_print(out, "zm[info]: init fatal error (lib version %s)\n",
	         ZM_VERSION);

	zm_print(out, "zm[info]: error code %s\n", ecode);
}

static zm_State *zm_fatalGetState(zm_VM *vm, zm_kfatal_t kind)
{
	zm_State *s;

	if ((!vm) || (kind == ZM_FATAL_UN))
		return NULL;

	s = zm_getCurrentState(vm);

	/* if not filename is set on fatal, use filename on last yield */
	if ((s) && (!zmg_err.ucode.filename)) {
		zmg_err.ucode.filename = s->codeframe.filename;
		zmg_err.ucode.nline = s->codeframe.nline;
	}

	return s;
}


#define ZM_FATAL_HEAD(sep) if (!errorinfo) {                                  \
    errorinfo = true;                                                         \
    zm_print(out, "Error occured at: %s", sep); }


static void zm_fatalPrintErrorInfo(zm_Print *out, zm_VM *vm, zm_kfatal_t kind)
{
	zm_State *state = zm_fatalGetState(vm, kind);
	int errorinfo = false;

	zm_print(out, "\n\n");

	if (zmg_err.ucode.reference) {
		ZM_FATAL_HEAD(" ");
		zm_print(out, "%s\n", zmg_err.ucode.reference);
	}

	if (zmg_err.ucode.filename) {
		ZM_FATAL_HEAD("\n");
		zm_print(out, "\tfilename: %s\n", zmg_err.ucode.filename);
		zm_print(out, "\tline: %d\n", zmg_err.ucode.nline);
	}

	if (state) {
		ZM_FATAL_HEAD("\n");

		zm_print(out, "\ttask: %s (kind=%s)\n",
		         zm_getCurrentMachineName(vm),
		         zm_isTask(state) ? "ptask" : "subtask");

		zm_print(out, "\tvmstate: resume=%d iter=%d catch=%d\n",
		         state->on.resume, state->on.iter, state->on.c4tch);

		zm_print(out, "\tvmstate: vmop=%s\n",
		         zm_getMachineOpName(state, false));

	}

	if (errorinfo)
		zm_print(out, "\n\n");


	/*** append exception and exception-trace info ***/
	if (zmg_err.exception) {
		zm_addIndent(out, 4);
		zm_printError(out, zmg_err.exception, true);
		zm_iprint(out, "\n");
		zm_printErrorTrace(out, zmg_err.exception);
		zm_addIndent(out, -4);
	}
}


static const char* zm_fatalGetTitle(zm_kfatal_t kind)
{
	switch(kind) {
	case ZM_FATAL_UN:
		return "UNEXPECTED ERROR";

	case ZM_FATAL_UNP:
		return "UNEXPECTED ERROR IN PROCESS TASK";

	case ZM_FATAL_UCODE:
		return "TASK CODE ERROR - Wrong code inside task";

	case ZM_FATAL_ERROR:
		return "FATAL ERROR";

	case ZM_FATAL_SYNC:
		return "TASK CODE ERROR - SYNC ERROR";

	case ZM_FATAL_YIELD:
		return "TASK CODE ERROR - Wrong code in task yield";

	case ZM_FATAL_NOCATCH:
		return "UNCAUGHT EXCEPTION";

	default:
		return "??WHERE IS FATAL TITLE??";
	}
}


void zm_fatalDo(zm_kfatal_t kind, const char *ecode, zm_VM *vm,
                                          const char *fmt, ...)
{
	va_list args;
	zm_Print out;
	const char* errlabel;
	int again = false;
	char *errorstr = NULL;

	if (zmg_err.locked)
		return;

	/** lock to avoid loop between printVM and fatal*/
	/**  error when inconsistency is found*/
	zmg_err.locked = true;

	zm_initPrint(&out, stderr, 0, (zmg_err.at.fatalcb != NULL));

	errlabel = zm_fatalGetTitle(kind);

	do {
		int len = 0;
		/*** print ecode ***/
		zm_fatalPrintECode(&out, ecode);

		/*** print error kind ***/
		zm_print(&out, "\n%s:\n   ", errlabel);

		/*** print error message ***/
		va_start(args, fmt);
		len = zm_vprint2(&out, -1, fmt, args);
		va_end(args);

		/*** print error message in string buffer ***/
		va_start(args, fmt);
		zm_vprint2(&out, len, fmt, args);
		va_end(args);

		if (zmg_err.at.fatalcb)
			errorstr = zm_popPrintBuffer(&out);

		zm_fatalPrintErrorInfo(&out, vm, kind);

		if (again)
			break;

		switch(kind) {
		case ZM_FATAL_UN:
		case ZM_FATAL_UNP:
			if (vm) {
				/* core dump*/
				zm_printVM(&out, vm);
				/* print again the message after the dump*/
				again = true;
			}
		default:break;
		}
	} while(again);


	if (zmg_err.at.fatalcb) {
		zmg_err.at.fatalcb(vm, errorstr, zmg_err.at.data);

		if (errorstr)
			free(errorstr);
	}

	exit(EXIT_FAILURE);
}



/* undefined vmstate */
void zm_fatalUndefState(zm_VM *vm, const char *filename, int nline)
{
	zm_State *state = zm_getCurrentState(vm);

	if (state->on.resume == 0) {
		zm_fatalOn(NULL, filename, nline);
		zm_fatalDo(ZM_FATAL_YIELD, "UNDEFSTATE.0", vm,
		           "vmstate = 0 not permitted");
	} else if (state->on.resume == 1) {
		zm_fatalOn(NULL, filename, nline);
		zm_fatalDo(ZM_FATAL_YIELD, "UNDEFSTATE.INIT", vm,
		           "init state (vmstate ZM_INIT = 1) "
		           "not found");

	} else {
		zm_fatalOn(NULL, filename, nline);
		zm_fatalDo(ZM_FATAL_YIELD, "UNDEFSTATE.N", vm,
		           "vmstate = %d not found!",
		           state->on.resume);
	}
}





#define ZM_ASSERT_VMLOCK(errcode, refname, filename, nline)                   \
   if (!vm->plock) {                                                          \
       ZM_D("vm->plock %d", vm->plock);                                       \
       zm_fatalOn(refname, filename, nline);                                  \
       zm_fatalDo(ZM_FATAL_ERROR, errcode, vm, "operation permitted only "    \
                  "inside a task"); }




/*---------------------------------------------------------------------------
 *  STATE QUEUE
 *  -----------------------------------------------------------------------*/


int zm_queueIsntEmpty(zm_StateQueue *queue)
{
	return queue->first != NULL;
}

int zm_queueIsEmpty(zm_StateQueue *queue)
{
	return queue->first == NULL;
}


zm_StateQueue* zm_queueNew()
{
	zm_StateQueue *result = zm_alloc(zm_StateQueue);
	result->first = NULL;
	result->last = NULL;
	return result;
}

void zm_queueFree(zm_StateQueue *q)
{
	if (zm_queueIsntEmpty(q))
		zm_fatalDo(ZM_FATAL_ERROR, "QUE.FREE", NULL,
		           "zm_queueFree: queue not empty");

	zm_free(zm_StateQueue, q);
}

zm_StateList* zm_queueAdd(zm_StateQueue* queue, zm_State *s, void *data)
{
	zm_StateList *statelist = zm_alloc(zm_StateList);
	statelist->state = s;
	statelist->next = NULL;
	statelist->data = data;

	if (!queue->first) {
		queue->first = queue->last = statelist;
		return statelist;
	}

	queue->last->next = statelist;
	queue->last = statelist;

	return statelist;
}


/* pop the first element of the queue (differ from normal pop that remove */
/* last element inserted)*/
zm_StateList* zm_queuePopStateList0(zm_StateQueue* queue)
{
	zm_StateList *result;

	if (queue->first == NULL)
		return NULL;

	result = queue->first;

	if (queue->last == queue->first) {
		queue->first = NULL;
		queue->last = NULL;
	} else {
		queue->first = queue->first->next;
	}

	return result;
}

/* pop the first element of the queue (differ from normal pop that remove */
/* last element inserted)*/
zm_State* zm_queuePop0(zm_StateQueue* queue, void** data)
{
	zm_State *result;
	zm_StateList *first;

	if (queue->first == NULL) {
		return NULL;
	}

	if (data) {
		*data = queue->first->data;
	}

	first = queue->first;

	result = queue->first->state;

	if (queue->last == queue->first) {
		queue->first = NULL;
		queue->last = NULL;
	} else {
		queue->first = queue->first->next;
	}

	zm_free(zm_StateList, first);

	return result;
}

static void zm_queueTreeAdd(zm_StateQueue *q, zm_State *s)
{
	zm_State *first = s;

	do {
		zm_queueAdd(q, s, NULL);
		s = s->siblings.next;

		if (s->subtasks)
			zm_queueTreeAdd(q, s->subtasks);

	} while (s != first);
}






/*---------------------------------------------------------------------------
 *  PRINT VM STRUCTURE FUNCTIONS
 *  -----------------------------------------------------------------------*/




#define ZM_STRCASE(x) case x: return #x

static const char *zm_getExceptionKindName(zm_Exception *e)
{
	switch(e->kind) {
	ZM_STRCASE(ZM_EXCEPTION_ERROR);
	ZM_STRCASE(ZM_EXCEPTION_CONTINUE);
	ZM_STRCASE(ZM_EXCEPTION_CONTINUEREF);
	ZM_STRCASE(ZM_EXCEPTION_STARTIMPLOSION);
	}
	return "[WARNING! UNKNOW EXCEPTION KIND]";
}


static const char *zm_getYieldCommandName(int n)
{
	switch(ZM_B4(n)) {
	ZM_STRCASE(ZM_TASK_CONTINUE);
	ZM_STRCASE(ZM_TASK_SUSPEND);
	ZM_STRCASE(ZM_TASK_SUSPEND_WAITING_SUBTASK);
	ZM_STRCASE(ZM_TASK_END);
	ZM_STRCASE(ZM_TASK_TERM);
	ZM_STRCASE(ZM_TASK_SUSPEND_AND_RESUME_CALLER);
	ZM_STRCASE(ZM_TASK_BUSY_WAITING_EVENT);
	ZM_STRCASE(ZM_TASK_RAISE_CONTINUE_EXCEPTION);
	ZM_STRCASE(ZM_TASK_RAISE_ERROR_EXCEPTION);
	ZM_STRCASE(ZM_TASK_VMSTOP);
	}
	return "[WARNING! UNKNOW YIELD COMMAND]";
}

static const char *zm_getImplodeFlagName(int implodeby)
{
	switch (implodeby) {
	ZM_STRCASE(ZM_IMPLODEBY_EXCEPTION);
	ZM_STRCASE(ZM_IMPLODEBY_SUB);
	ZM_STRCASE(ZM_IMPLODEBY_ROOT);
	ZM_STRCASE(ZM_IMPLODEBY_CUR);
	}
	return "[WARNING! UNKNOW IMPLODE FLAG]";
}

#if ZM_DEBUG_LEVEL >= 1
static const char *zm_getWSCheckFlagName(int wscheck)
{
	switch (wscheck) {
	ZM_STRCASE(ZM_WSCHECK_ALL);
	ZM_STRCASE(ZM_WSCHECK_NONE);
	ZM_STRCASE(ZM_WSCHECK_SKIPFIRST);
	}

	return "[WARNING! UNKNOW WSCHECK FLAG]";
}
#endif

static const char *zm_getMachineName(zm_VM *vm, zm_State *s)
{
	void *w;

	if (s->flag & ZM_STATEFLAG_RUN) {
		if (!vm)
			return "[cannot get !vm]";

		if (s == zm_getCurrentState(vm))
			return zm_getCurrentMachineName(vm);

		return "[cannot get !current]";
	}

	if (!s->next)
		return "[cannot get !next]";


	/* No run flag means one of these combintations:
	   [ws], [ws][we], [su] and also only [we] (see  #EVB_FLAG)
	   Anyway the only difference is if there is the [we] flag
	*/

	if (s->flag & ZM_STATEFLAG_EVENTLOCKED)
		w = ((zm_EventBinder*)(s->next))->statenext;
	else
		w = s->next;

	return ((zm_Worker*)w)->machine->name;
}




#define ZM_DEFAULT_OUT stdout
#define ZM_DEFAULT_OUT_ON_NULL(out)                                           \
    zm_Print defaultout;                                                      \
    if (!(out)) {                                                             \
        out = &defaultout;                                                    \
        zm_initPrint(out, ZM_DEFAULT_OUT, 4, false);                          \
    }


static zm_Trace *zm_getRaiseTraceElement(zm_Trace *t)
{
	/* get the last element of the trace = the raising point*/
	while (t) {
		if (!t->next)
			break;

		t = t->next;
	}

	return t;
}

static void zm_printTraceElement(zm_Print *out, zm_Trace *t)
{
	zm_iprint(out, "task-id: %lx\t\n", t->taskid);
	zm_iprint(out, "machine: %s\t[vmstate: %d]\n", t->machinename, t->on);

	if (t->filename) {
		zm_iprint(out, "filename: %s\n", t->filename);
		zm_iprint(out, "nline: %d\n", t->nline);
	}
}

static void zm_printExceptionData(zm_Print *out, zm_Exception *e)
{

	if (e->msg)
		zm_iprint(out, "msg: \"%s\"\n", e->msg);
	else
		zm_iprint(out, "msg: NULL\n");


	zm_iprint(out, "ecode: %d\n", e->ecode);
	zm_iprint(out, "data: [ref: %lx]\n", e->data);
	zm_iprint(out, "beforecatch: [ref: %lx]\n", e->beforecatch);
}



static void zm_printError(zm_Print *out, zm_Exception *e, bool errormsgmode)
{
	zm_Trace *t = e->etrace;

	ZM_DEFAULT_OUT_ON_NULL(out);

	if (errormsgmode) {
		/** Error Message Mode **/
		zm_iprint(out, "Exception:\n");
		zm_iprint(out, "   (%d) \"%s\"\n", e->ecode,
		          (e->msg) ? (e->msg) : (""));

	} else {
		/** Debug/Descriptive Mode **/
		zm_printExceptionData(out, e);
	}

	switch(e->kind) {
	case ZM_EXCEPTION_ERROR:
		/* error always have trace*/
		break;
	case ZM_EXCEPTION_CONTINUE:
		/* continue can have trace */
		if (!t)
			return;

		break;

	case ZM_EXCEPTION_STARTIMPLOSION:
	case ZM_EXCEPTION_CONTINUEREF:
		/* internal ref exception don't have trace */
		return;

	default:
		zm_fatalDo(ZM_FATAL_UN, "PRNTERR.EUN", NULL,
		           "unknow exception->kind = %d",
		           e->kind);
	}

	/** print the raising point*/
	t = zm_getRaiseTraceElement(t);

	if (errormsgmode) {
		zm_iprint(out, " in: %s-%lx (file: %s at line: %d)\n\n",
		          t->machinename,
		          t->taskid,
		          t->filename,
		          t->nline);
	} else {
		zm_printTraceElement(out, t);
	}
}


static void zm_printErrorTrace(zm_Print *out, zm_Exception *e)
{
	zm_Trace *t = e->etrace;

	zm_iprint(out, "Trace: ");

	if (!t->next) {
		zm_iprint(out, "[only one trace element]\n");
		return;
	}

	zm_iprint(out, "\n");

	zm_addIndent(out, 3);

	while (t) {
		zm_printTraceElement(out, t);

		if (t->next) {
			zm_iprint(out, "--------------------\n");
		}

		t = t->next;
	}

	zm_addIndent(out, -3);
}


static void zm_printException(zm_Print *out, zm_State *estate)
{
	zm_Exception *exception = estate->exception;
	ZM_DEFAULT_OUT_ON_NULL(out);

	switch(exception->kind) {
		case ZM_EXCEPTION_ERROR: {
			/*** Exception-Error*/
			zm_iprint(out, "kind: error\n");

			zm_printError(out, exception, false);
			zm_iprint(out, "\n");
			zm_printErrorTrace(out, exception);

			break;
		}


		case ZM_EXCEPTION_CONTINUE:
		case ZM_EXCEPTION_CONTINUEREF:
			/*** Exception-Continue */
			if (exception->kind ==  ZM_EXCEPTION_CONTINUE) {
				zm_iprint(out, "kind: continue\n");
				zm_printExceptionData(out, exception);
			} else {
				zm_iprint(out, "kind: continue-innerref\n");
			}
			zm_iprint(out, "beforecatch: [ref: %lx]\n",
			          exception->beforecatch);

			zm_addIndent(out, 3);

			zm_iprint(out, "raise state: [ref: %lx] ",
			          exception->raisestate);

			if (exception->raisestate == estate) {
				zm_print(out, " (self)");
			}

			zm_print(out, "\n");

			zm_addIndent(out, -3);
			break;

		case ZM_EXCEPTION_STARTIMPLOSION:
			zm_iprint(out, "kind: implosion start\n");

			zm_iprint(out, "implosion start: [ref: %lx]\n",
			          exception->raisestate);

			zm_iprint(out, "saved exception: [ref: %lx]\n",
			          exception->data);
			break;

		default:
			zm_fatalDo(ZM_FATAL_UN, "PRNTEXCEPT.EUN", NULL,
			           "unknow exception->kind = %d",
			           exception->kind);
	}

}


#define ZM_CPRN(c, e)  zm_print(out, (compact) ? c : e)


static void zm_printFlags(zm_Print* out, zm_State *s, int compact)
{
	if (s->flag & ZM_STATEFLAG_RUN) {
		ZM_CPRN("[rn]", "(run)");
	} else {
		if (s->flag & ZM_STATEFLAG_WAITING) {
			if (s->flag & ZM_STATEFLAG_EVENTLOCKED)
				ZM_CPRN("[we]", "(waiting event) ");
			else
				ZM_CPRN("[ws]", "(waiting subtask) ");
		} else {
			if (s->flag & ZM_STATEFLAG_EVENTLOCKED)
				/* #EVB_FLAG*/
				ZM_CPRN("[evb]", "(event binding) ");
			else
				ZM_CPRN("[su]", "(suspend) ");

		}
	}

	if (s->flag & ZM_STATEFLAG_IMPLOSIONLOCK)
		ZM_CPRN("[IL]", "(implosion lock) ");


	if (s->flag & ZM_STATEFLAG_AUTOFREE)
		ZM_CPRN("[af]", "(autofree) ");


	if (s->flag & ZM_STATEFLAG_CATCH)
		ZM_CPRN("[ca]", "(catch) ");


	if (s->flag & ZM_STATEFLAG_CONTINUEMARK)
		ZM_CPRN("[cm]", "(c-mark) ");


	if (s->flag & ZM_STATEFLAG_UNUSED1)
		ZM_CPRN("[??]", "( unknow ??? ) ");

}

static const char* zm_getMachineOpName(zm_State *s, int compact)
{
	switch (s->vmop) {
	case ZM_MACHINEOP_RUN:
		return (compact) ? "ORUN" : "ZM_MACHINEOP_RUN";

	case ZM_MACHINEOP_END_TASK:
		return (compact) ? "OEND" : "ZM_MACHINEOP_END_TASK";

	case ZM_MACHINEOP_CLOSE_TASK:
		return (compact) ? "OCLO" : "ZM_MACHINEOP_CLOSE_TASK";

	case ZM_MACHINEOP_NO_MORE_TO_DO:
		return (compact) ? "ONUL" : "ZM_MACHINEOP_NO_MORE_TO_DO";

	case ZM_MACHINEOP_UNLINK_TASK_AND_IMPLODE:
		return (compact) ? "ULN" :
		       "ZM_MACHINEOP_UNLINK_TASK_AND_IMPLODE";

	default:
		return (compact) ? "O???" : "ZM_MACHINEOP_??????????";
	}
}


void zm_printStateCompact(zm_Print *out, zm_State *s)
{
	ZM_DEFAULT_OUT_ON_NULL(out);

	#ifdef ZM_DEBUG_MACHINENAME
		zm_iprint(out, "%s-%lx ", s->debugmachinename, s);
	#else
		zm_iprint(out, "%lx ", s);
	#endif


	zm_printFlags(out, s, true);

	zm_print(out, " ");

	/*zm_print(out, "on:%d|%d|%d", s->on.resume, s->on.iter,s->on.c4tch);*/


	if (zm_haveComeback(s)) {
		zm_print(out, "cb:%lx ", s->parent->comeback);
	} else {
		zm_print(out, "ncb ");
	}

	if (s->vmop != ZM_MACHINEOP_RUN)
		zm_print(out, "%s ", zm_getMachineOpName(s, true));

	if (s->exception)
		zm_print(out, "!%lx", s->exception);

	zm_print(out, "\n");
}



void zm_printState(zm_Print *out, zm_State *s)
{
	ZM_DEFAULT_OUT_ON_NULL(out);

	zm_iprint(out, "flag: %d     ", s->flag);

	zm_printFlags(out, s, false);

	zm_iprint(out, "\n");

	if (zm_isSubTask(s)) {
		size_t i;
		zm_iprint(out, "parent: (subtask) stacksize = %d\n",zm_deep(s));

		for (i = 0; i < zm_deep(s); i++) {
			zm_iprint(out, "  parent[%d] = [ref: %lx]\n", i,
			          s->parent->stack[i]);
		}

		zm_iprint(out, "parent->comeback: [ref: %lx]\n",
		          s->parent->comeback);
	} else {
		zm_iprint(out, "parent: (=>task) [NULL]\n");
	}

	zm_iprint(out, "subtasks: [ref: %lx]\n", s->subtasks);
	zm_iprint(out, "siblings: prev=[ref: %lx], next=[ref: %lx]\n",
	          s->siblings.prev, s->siblings.next);

	zm_iprint(out, "on: resume = %d, iter = %d, catch = %d\n",
	          s->on.resume, s->on.iter, s->on.c4tch);


	if (s->vmop != ZM_MACHINEOP_RUN)
		zm_iprint(out, "vmop: %s\n", zm_getMachineOpName(s, false));

	#ifdef ZM_DEBUG_MACHINENAME
		zm_iprint(out, "machine: %s\n", s->debugmachinename);
	#endif

	zm_iprint(out, "next: ");

	if (s->flag & ZM_STATEFLAG_RUN) {
		zm_print(out, "(state)");
	} else {
		const char *m = zm_getMachineName(NULL, s);
		if (s->flag & ZM_STATEFLAG_EVENTLOCKED) {
			zm_print(out, "(eventbinder->worker: %s)", m);
		} else {
			zm_print(out, "(saved worker: %s)", m);
		}
	}
	zm_print(out, " [ref: %lx]\n", s->next);


	if (!s->exception)
		return;

	zm_iprint(out, "exception: [ref: %lx]\n", s->exception);

	zm_addIndent(out, 2);

	zm_printException(out, s);

	zm_addIndent(out, -2);
}


static void zm_printWorker(zm_Print *out, zm_Worker *w)
{
	ZM_DEFAULT_OUT_ON_NULL(out);

	zm_iprint(out, "machine->name: %s\n", w->machine->name);

	zm_iprint(out, "nstate: %d\n", w->nstate);
	zm_iprint(out, "cyclestep: %d\n", w->cyclestep);

	zm_iprint(out, "states.first: [ref: %lx]\n", w->states.first);
	zm_iprint(out, "states.current: [ref: %lx]\n", w->states.current);
	zm_iprint(out, "states.previous: [ref: %lx]\n", w->states.previous);

	#ifdef ZM_CHECK_CONSISTENCY
	if ((w->states.current == w->states.previous) &&
	    (w->states.current != w->states.first)) {
		zm_fatalDo(ZM_FATAL_UN, "PRNTWRK.EQPC", NULL,
		           "current == previous != first");
	}
	#endif

	if (w->next) {
		zm_iprint(out, "next (worker): %s-%lx\n",
		          w->next->machine->name, w->next);
	} else {
			zm_iprint(out, "next (worker): NULL\n");
	}

	if (w->nstate) {
		int i = 0;
		zm_State *state = w->states.first;

		do {
			zm_iprint(out, "\n");
			zm_iprint(out, "   - state: %d", i + 1);
			zm_iprint(out, " [ref: %lx]\n", state);

			zm_addIndent(out, 5);
			zm_printState(out, state);
			zm_addIndent(out, -5);

			state = state->next;
			i++;
		} while ((state != w->states.first) &&
		         (state != NULL) &&
		         (i <= w->nstate));


		#ifdef ZM_CHECK_CONSISTENCY
		if (i != w->nstate) {
			zm_fatalDo(ZM_FATAL_UN, "PRNTWRK.1", NULL,
			           "nstate = %d but found %d states",
			           w->nstate, i);
		}
		#endif
	} else {
		zm_iprint(out, "   - no states in this worker -\n");
	}
}

static void zm_printHeaderVM(zm_Print *out, zm_VM *vm)
{
	ZM_DEFAULT_OUT_ON_NULL(out);
	zm_iprint(out, "name: %s\n", vm->vname);
	zm_iprint(out, "ptask count: %d\n", vm->nptask);
	zm_iprint(out, "worker count: %d\n", vm->nworker);

	zm_iprint(out, "plock: %d\n", vm->plock);
	zm_iprint(out, "currentsession.fixedworker: %d\n",
	          vm->currentsession.fixedworker);

	if (vm->nworker) {
		zm_iprint(out, "workercursor: %s-%lx\n",
		          vm->workercursor->machine->name,
		          vm->workercursor);
	}
}

static void zm_printStateRecursive(zm_Print *out, zm_State *s, int deep)
{
	zm_State *first;
	int i = 0;

	ZM_DEFAULT_OUT_ON_NULL(out);

	if (deep > ZM_PRINTSTATE_MAXDEEP) {
		zm_iprint(out, "zm_printStateRecursive: [WARNING] "
		          "deep = %d > MAX DEEP", deep);
		return;
	}


	zm_printState(out, s);

	if (!s->subtasks)
		return;

	s = s->subtasks;
	first = s;

	do {
		zm_iprint(out, "\n");
		zm_iprint(out, "   - substate (order=%d): %d [ref: %lx]\n",
		          deep, i++, s);
		zm_addIndent(out, 5);
		zm_printStateRecursive(out, s, deep+1);
		zm_addIndent(out, -5);

		s = s->siblings.next;
	} while (s != first);
}

static int zm_subcheckConsistency(zm_State *s)
{
	zm_State *first;
	int runcount = 0;

	if (s->flag & ZM_STATEFLAG_RUN) {
		runcount++;
	}

	if (!s->subtasks)
		return runcount;

	s = s->subtasks;
	first = s;

	do {
		runcount += zm_subcheckConsistency(s);
		s = s->siblings.next;
	} while (s != first);

	return runcount;
}



static void zm_checkConsistency(zm_VM *vm)
{
	size_t i = 0, runcount;
	zm_State *state = vm->ptasks;

	if (!vm->ptasks)
		return;

	do {
		if (!state->siblings.next) {
			zm_fatalDo(ZM_FATAL_UN, "PRNTTASK.1", vm,
			           "zm_printTasks: null ref in "
			           "siblings ring");
		}

		if (i++ > vm->nptask) {
			zm_fatalDo(ZM_FATAL_UN, "PRNTTASK.2", vm,
			           "zm_printTasks: siblings count "
			           "doesn't match");
		}

		runcount = zm_subcheckConsistency(state);


		if (runcount > 1) {
			zm_fatalDo(ZM_FATAL_UN, "PRNTTASK.RC", vm,
			           "zm_printTask: more than one task "
			           "run in the same exec-context "
			           "(context: [ref %lx] count: %d)",
			           state, runcount);
		}

		state = state->siblings.next;

	} while (state != vm->ptasks);
}


void zm_printTasks(zm_Print *out, zm_VM *vm)
{
	zm_State *state = vm->ptasks;
	size_t i = 0;

	if (!vm->ptasks)
		return;

	zm_addIndent(out, 2);
	do {
		zm_iprint(out, "\n");
		zm_iprint(out, "-TASK: %d [ref %lx]\n", i, state);

		zm_addIndent(out, 1);

		zm_printStateRecursive(out, state, 0);

		state = state->siblings.next;

		zm_addIndent(out, -1);
	} while (state != vm->ptasks);

	zm_addIndent(out, -2);
}

void zm_printTreeVM(zm_Print *out, zm_VM *vm);

void zm_printVM(zm_Print *out, zm_VM *vm)
{
	ZM_DEFAULT_OUT_ON_NULL(out);
	zm_setIndent(out, 0);
	zm_iprint(out, "------------------------------ VM ");
	zm_iprint(out, "------------------------------\n");

	zm_setIndent(out, 1);
	zm_printHeaderVM(out, vm);

	zm_iprint(out, "\n");

	zm_iprint(out, "*** tasks:\n");
	zm_printTasks(out, vm);


	zm_iprint(out, " \n");
	zm_iprint(out, "*** active workers:\n");

	if (vm->nworker == 0) {
		zm_iprint(out, "   - no worker in this vm -\n");
	} else {
		size_t i = 0;
		zm_Worker *w = vm->workercursor;

		do {
			zm_iprint(out, "\n");
			zm_iprint(out, " - worker %d: ", i++);
			zm_iprint(out, "%s-%lx\n", w->machine->name, w);

			zm_setIndent(out, 3);
			zm_printWorker(out, w);
			zm_setIndent(out, 0);

			#ifdef ZM_CHECK_CONSISTENCY
			if ((w == w->next) && (w != vm->workercursor)){
				zm_fatalDo(ZM_FATAL_UN, "PRNTVM.1",
				           NULL, "inconsistency in "
				           "zm_printVM: worker "
				           "ring is not closed");
			}
			#endif
			w = w->next;
		} while ((w != vm->workercursor) && (i < vm->nworker));

		#ifdef ZM_CHECK_CONSISTENCY
		if (i !=  vm->nworker) {
			zm_fatalDo(ZM_FATAL_UN, "PRNTVM.2", NULL,
			           "inconsistency: declared nworker = %d "
			           "but found nworker = %d",
			           vm->nworker, i);
		}
		#endif
	}

	zm_setIndent(out, 0);

	zm_print(out, "\n\n");
	zm_printTreeVM(NULL, vm);

	#ifdef ZM_CHECK_CONSISTENCY
	zm_checkConsistency(vm);
	#endif


	zm_iprint(out, "----------------------------------");
	zm_iprint(out, "------------------------------\n\n");
}

static void zm_printDataBranchVM(zm_Print *out, zm_State *state, int deep)
{
	zm_State *s, *first;

	ZM_DEFAULT_OUT_ON_NULL(out);

	if (deep > ZM_PRINTSTATE_MAXDEEP) {
		zm_iprint(out, "zm_printStateRecursive: [WARNING] "
		          "deep = %d > MAX DEEP", deep);
		return;
	}


	if (deep)
		zm_iprint(out, "|\n");

	zm_printStateCompact(out, state);

	if (!state->subtasks)
		return;

	first = s = state->subtasks;

	do {
		zm_addIndent(out, 5);
		zm_printDataBranchVM(out, s, deep+1);
		zm_addIndent(out, -5);

		s = s->siblings.next;
	} while (s != first);
}

static void zm_printDataTreeVM(zm_Print *out, zm_VM *vm)
{
	zm_State *state = vm->ptasks;

	ZM_DEFAULT_OUT_ON_NULL(out);

	if (!vm->ptasks)
		return;

	zm_addIndent(out, 2);
	do {
		zm_addIndent(out, 1);

		zm_printDataBranchVM(out, state, 0);

		zm_addIndent(out, -1);

		state = state->siblings.next;
	} while (state != vm->ptasks);

	zm_addIndent(out, -2);
}

static void zm_getStates(zm_VM *vm, zm_StateQueue* q)
{
	zm_State *state = vm->ptasks;

	do {
		zm_queueAdd(q, state, NULL);

		if (state->subtasks)
			zm_queueTreeAdd(q, state->subtasks);


		state = state->siblings.next;
	} while (state != vm->ptasks);
}



static zm_State* zm_findComebackOf(zm_VM *vm, zm_StateQueue *qiter,
                                                zm_State *searched)
{
	zm_StateList *sl = qiter->first;
	zm_State *found = NULL;
	int nmatch = 0;

	if (!sl)
		return NULL;

	do {
		if (zm_getCaller(sl->state) == searched) {
			found = sl->state;
			nmatch++;
		}

		sl = sl->next;
	} while(sl);

	#ifdef ZM_CHECK_CONSISTENCY
	if (nmatch > 1) {
		zm_fatalDo(ZM_FATAL_UN, "FINDCOMEBK", vm,
		           "findComebackOf: %lx found %d times",
		           searched, nmatch);
	}
	#endif


	return found;

}




static void zm_printComebackStates(zm_Print *out, zm_VM *vm,
                                       zm_StateQueue *qiter,
                                            zm_State *state)
{
	zm_State *comeback = zm_findComebackOf(vm, qiter, state);
	ZM_DEFAULT_OUT_ON_NULL(out);


	if (!comeback)
		return;

	zm_addIndent(out, 4);

	zm_iprint(out, " | \n");
	zm_printStateCompact(out, comeback);

	zm_printComebackStates(out, vm, qiter, comeback);

	zm_addIndent(out, -4);
}


static void zm_printComebackTreeVM(zm_Print *out, zm_VM *vm)
{
	zm_StateList *sl;
	zm_StateQueue *qiter;

	ZM_DEFAULT_OUT_ON_NULL(out);

	if (!vm->ptasks)
		return;

	qiter = zm_queueNew();

	zm_getStates(vm, qiter);

	sl = qiter->first;

	zm_addIndent(out, 2);

	while(sl) {
		if (!zm_haveComeback(sl->state)) {
			/*  root*/
			zm_addIndent(out, 1);

			zm_printStateCompact(out, sl->state);
			zm_printComebackStates(out, vm, qiter, sl->state);
			zm_print(out, "\n");

			zm_addIndent(out, -1);
		}

		sl = sl->next;
	}

	zm_addIndent(out, -2);

	while (zm_queuePop0(qiter, NULL)) {}

	zm_queueFree(qiter);
}



#if 0
static void zm_printActiveComebackState(zm_Print *out, zm_State *state)
{
	ZM_DEFAULT_OUT_ON_NULL(out);

	if (!state->parent)
		return;

	if (!state->parent->comeback)
		return;

	zm_addIndent(out, 4);

	zm_iprint(out, " | \n");
	zm_printStateCompact(out, state);

	zm_printActiveComebackState(out, state->parent->comeback);

	zm_addIndent(out, -4);
}

static void zm_printActiveComebackTreeVM(zm_Print *out, zm_VM *vm)
{
	zm_Worker *w = vm->workercursor;

	ZM_DEFAULT_OUT_ON_NULL(out);

	if (w == NULL)
		return;


	do {
		if (w->nstate) {
			zm_State *state = w->states.first;

			do {
				zm_printActiveComebackState(out, state);
			} while ((state != w->states.first) && (state != NULL));
		}

		w = w->next;
	} while (w != vm->workercursor);
}
#endif



void zm_printTreeVM(zm_Print *out, zm_VM *vm)
{
	ZM_DEFAULT_OUT_ON_NULL(out);

	zm_setIndent(out, 0);

	zm_iprint(out, "*** INIT-TREE (data flow):\n");

	zm_printDataTreeVM(out, vm);

	zm_print(out, "\n\n");

	zm_setIndent(out, 0);

	zm_iprint(out, "*** EXEC-STACK (parent-comeback flow):\n");

	zm_printComebackTreeVM(out, vm);

	zm_setIndent(out, 0);
}

void zm_debugPrintQueueSL(zm_Print *out, zm_StateList *sl)
{
	if (!sl)
		return;

	zm_addIndent(out, 4);
	zm_iprint(out, "state: [ref %lx] ", sl->state);

	zm_print(out, "\n");

	zm_iprint(out, "next: [ref %lx]\n", sl->next);
	zm_addIndent(out, -4);
}

void zm_debugPrintQueue(zm_Print *out, const char *name, zm_StateQueue* queue)
{
	zm_StateList *sl = queue->first;
	int n = 0;

	zm_iprint(out, "%s   @queue: [ref: %lx]\n", name, queue);
	zm_addIndent(out, 3);

	zm_iprint(out, "first: [ref %lx]\n", queue->first);
	zm_iprint(out, "last: [ref %lx]\n", queue->last);

	if (!queue->first)
		return;

	do {
		zm_iprint(out, "%d) statelist: [ref %lx]\n", n, sl);
		zm_debugPrintQueueSL(out, sl);
		sl = sl->next;
		n++;
	} while(sl);

	zm_addIndent(out, -3);
}

void izmPrintError(zm_VM* vm, FILE *stream, zm_Exception *e, int trace,
                                       const char *filename, int nline)
{
	zm_Print out;
	zm_initPrint(&out, stream, 0, false);

	zm_printError(&out, e, true);

	if (trace) {
		zm_printErrorTrace(&out, e);
	}
}

/*---------------------------------------------------------------------------
 *  MHW - MACHINE WORKER "HASH"
 *  -----------------------------------------------------------------------*/



static void zm_mhwInit(zm_VM *vm)
{
	size_t len = zmg_mcounter + ZM_MACHINE_HLIST_INC;

	vm->mhw.len = len;

	vm->mhw.hlist = zm_nalloc(zm_Worker*, len);

	memset(vm->mhw.hlist, 0, len * sizeof(zm_Worker*));

}

static void zm_mhwFree(zm_VM *vm)
{
	zm_mfree(sizeof(zm_Worker*) * vm->mhw.len, vm->mhw.hlist);
}


static void zm_mhwAdd(zm_VM *vm, zm_Machine *machine, zm_Worker* w)
{
	/* unitalized machine*/
	if (machine->id == -1) {
		machine->id = zmg_mcounter++;
	}

	machine->count++;

	/* chek if associative array must grow*/
	if (machine->id >= vm->mhw.len) {
		size_t nsize = machine->id + ZM_MACHINE_HLIST_INC;
		size_t esize = (nsize - vm->mhw.len) * sizeof(zm_Worker*);

		vm->mhw.hlist = zm_realloc(vm->mhw.hlist, zm_Worker*, nsize);

		memset(vm->mhw.hlist + vm->mhw.len, 0, esize);

		vm->mhw.len = nsize;
	}

	vm->mhw.hlist[machine->id] = w;
}

static zm_Worker* zm_mhwGet(zm_VM *vm, zm_Machine *machine)
{
	/* unitalized machine*/
	if (machine->id == -1)
		return NULL;

	/* chek if associative array must grow*/
	if (machine->id >= vm->mhw.len)
		return NULL;

	return vm->mhw.hlist[machine->id];
}


/*---------------------------------------------------------------------------
 *  STATE CURSOR
 *  -----------------------------------------------------------------------*/


static void zm_scursInit(zm_Worker *worker, zm_State *s)
{
	worker->states.current = worker->states.first = s;
	worker->states.previous = NULL;
	worker->nstate = 1;
}

/* move states cursor #NAV_STATES */
static void zm_scursMove(zm_Worker *worker)
{
	worker->states.previous = worker->states.current;
	worker->states.current = worker->states.current->next;
}

static void zm_scursRemove(zm_VM *vm, zm_Worker *worker)
{
	if (++vm->currentsession.suspendop > 1) {
		zm_fatalDo(ZM_FATAL_UN, "SCURS.RM", vm,
		           "more than one suspend operation in the "
		           "same session");
	}
	/* move states cursor #NAV_STATES */
	if (worker->states.previous) {
		/* unlink current preserve previous
		   (states.previous = unchanged) */
		worker->states.previous->next = worker->states.current->next;
	} else {
		/* previous == NULL means current == first so: */
		worker->states.first = worker->states.current->next;
	}

	/* max 1 per session  */
	worker->states.current = worker->states.current->next;
}



/*---------------------------------------------------------------------------
 *  RESUME - SUSPEND STATE
 *  -----------------------------------------------------------------------*/

int zmyieldtrace(zm_VM* vm, const char *name, int line)
{
	zm_getCurrentState(vm)->codeframe.filename = name;
	zm_getCurrentState(vm)->codeframe.nline = line;

	return 0;
}



static zm_State* zm_getParent(zm_State *s)
{
	return s->parent->stack[zm_deep(s) - 1];
}

static int zm_haveSameRoot(zm_State *s, zm_State *sub)
{
	if (zm_isTask(s)) {
		return sub->parent->stack[0] == s;
	} else {
		return sub->parent->stack[0] == s->parent->stack[0];
	}
}


static int zm_haveSameContext(zm_State *s1, zm_State *s2)
{
	int t1 = zm_isTask(s1);
	int t2 = zm_isTask(s2);

	if (t1 && t2) {
		return s1 == s2;
	}

	if (!t1)
		return zm_haveSameRoot(s2, s1);
	else
		return zm_haveSameRoot(s1, s2);
}

static void zm_addWorker(zm_VM *vm, zm_Worker *w)
{
	/* #NAV_WORKERS*/

	ZM_D("zm_addWorker [%lx] %s", w, w->machine->name);
	if (vm->nworker == 0) {
		vm->workercursor = w;
		w->next = w->prev = w;
	} else {
		w->prev = vm->workercursor->prev;
		w->next = vm->workercursor;

		vm->workercursor->prev->next = w;
		vm->workercursor->prev = w;
	}

	vm->nworker++;
}




static void zm_unlinkWorker(zm_VM *vm, zm_Worker *w)
{
	ZM_D("zm_unlinkWorker [%lx] %s", w, w->machine->name);

	/**** use only in not empty ring  #NAV_WORKERS*/
	if (vm->nworker == 1) {
		/* only one element*/
		vm->workercursor = NULL;
	} else {
		w->prev->next = w->next;
		w->next->prev = w->prev;

		if (vm->workercursor == w) {
			/* #BREAK_IN_SESSION_CURSOR_SYNC*/
			vm->workercursor = vm->workercursor->next;
		}
	}

	vm->nworker--;
}




static zm_Worker* zm_nextWorker(zm_VM *vm)
{
	/* #NAV_WORKERS #NAV_WORKERS2*/
	/* use only in not empty ring*/
	vm->workercursor = vm->workercursor->next;

	return vm->workercursor;
}

/**
 * NOTE: can be used only if worker->states.current is running
 * because ->next must point to a state (and not store a worker or an
 * evenbinder as in waiting)
 */
static void zm_rewindWorkerStates(zm_VM *vm, zm_Worker* worker)
{
	worker->states.previous = NULL;
	worker->states.current = worker->states.first;

	if (worker->nstate == 0)
		return;

	if (zm_hasntFlag(worker->states.current, ZM_STATEFLAG_RUN)) {
		zm_fatalDo(ZM_FATAL_UN, "RWINDW.1", vm,
		           "zm_rewindWorkerState: not found state "
		           "run flag");
	}
}



/* resume - add to worker
 * (worker  has been temporary stored in next pointer)
 */
static void zm_resumeState(zm_VM *vm, zm_State *s, int asnext)
{
	/**** worker for a suspended state is temporary stored in s->next*/
	zm_Worker* worker = (zm_Worker*)s->next;

	ZM_D("resumeState: [ref %lx] asnext = %d", s, asnext);

	/* this check is also done in RESBY.IL (should't fail)*/
	if (s->flag & ZM_STATEFLAG_IMPLOSIONLOCK) { /* #UNBIND_IMLOCK */
		if (zm_isntTermState(s->on.resume)) {
			zm_fatalDo(ZM_FATAL_UN, "RESST.IL", vm,
			           "in resume state: found locked "
			           "and implosion flag");
		}
	}

	/* this check is also done in RESBY.RRUN (should't fail)*/
	if (s->flag & ZM_STATEFLAG_RUN) {
		zm_fatalDo(ZM_FATAL_UN, "RESST.R", vm,
		           "in resume state: found run flag");
	}


	if (s->flag & ZM_STATEFLAG_EVENTLOCKED) {
		zm_fatalDo(ZM_FATAL_UN, "RESST.EV", vm,
		           "in resume state: found event-locked flag");
	}


	s->flag |= ZM_STATEFLAG_RUN;

	zm_disableFlag(s, ZM_STATEFLAG_WAITING);

	ZM_D("resumeState: worker = %s\n", worker->machine->name);

	if (!worker) {
		zm_fatalDo(ZM_FATAL_UN, "RESST.NW", vm,
		           "in resume state: "
		           "unable to resume state (null worker)\n"
		           "    State is not yet associated to a worker\n"
		           "    (possible cause: state as been free)");
	}


	/**** Add state to worker ***  */

	/* #NAV_STATES #NAV_WORKERS*/

	if (worker->nstate == 0) {
		#if 0
		worker->states.current = worker->states.first = s;
		worker->states.previous = NULL;
		worker->nstate = 1;
		#endif

		zm_scursInit(worker, s);

		s->next = NULL;

		ZM_D("resumeState - ins worker %s", worker->machine->name);

		/* add worker to vm*/
		zm_addWorker(vm, worker);

		/*ZM_D("resumeState - insert worker ...done <----");*/
		return;
	}

	if (asnext) {
		/*current->a->b->c
		  current->s
	       s->b->c*/
		s->next = worker->states.current->next;
		worker->states.current->next = s;
	} else {
		if ( worker->states.previous ) {
			ZM_D("resumeState - not first element");
			/* not first element (nstate >= 2)*/
			worker->states.previous->next = s;
			worker->states.previous = s;
		} else {
			zm_State *f = worker->states.first;
			ZM_D("resumeState - first element");
			if (worker->states.current != f) {
				if (worker->states.current == NULL) {
					worker->states.current = f;
					zm_fatalDo(ZM_FATAL_UN, "WSYLST.??",
					           vm, "TEST");
				} else {
					zm_fatalDo(ZM_FATAL_UN, "WSYLST.1",
					           vm, "UNEXPECTED WSYNCLOST");
				}
			}

			worker->states.previous = s;
			worker->states.first = s;
		}
		s->next = worker->states.current;
	}

	worker->nstate++;
}



/* resume - add to worker
 * (worker  has been temporary stored in next pointer)
 */
static void zm_resumeStateBy(zm_VM *vm, zm_State *s, int asnext,
                                                const char *ref,
                                           const char *filename,
                                                      int nline)
{
	if (s->flag & ZM_STATEFLAG_RUN) {
		if (s == zm_getCurrentState(vm)) {
			zm_fatalOn(ref, filename, nline);
			zm_fatalDo(ZM_FATAL_UCODE, "RESBY.RRUN.S", vm,
			           "try to self-resume");
		}

		zm_fatalOn(ref, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "RESBY.RRUN.J", vm,
		           "try to active a just active state");
	}

	if (s->flag & ZM_STATEFLAG_IMPLOSIONLOCK) {
		if (zm_isntTermState(s->on.resume)) {
			zm_fatalOn(ref, filename, nline);
			zm_fatalDo(ZM_FATAL_UCODE, "RESBY.IL", vm,
			           "tring to resume a closing state");
		}
	}


	zm_resumeState(vm, s, asnext);
}

#define zm_comeback(sub, s) (sub)->parent->comeback = (s)



/* Resume Parent: this is a semantic error, resume parent don't resume
 * the parent in data-tree but resume the last element in exec-stack.
 * (after yield iter or task end)
 */
static void zm_resumeParent(zm_VM *vm, zm_State *s, int iter)
{
	zm_State *p;

	ZM_D("resumeParent: state = %lx, iter = %d", s, iter);



	if (s->parent == NULL) {
		/* State parent should not invoked for state without parent:
		   parent must exists */
		zm_fatalDo(ZM_FATAL_UN, "RESPAR.1", vm,
		           "try to resume parent of a state"
		           "that have no parent");
	}

	ZM_D("resumeParent: comeback = %lx", s->parent->comeback);


	p = s->parent->comeback;

	if (!p) {
		zm_fatalDo(ZM_FATAL_UN, "RESPAR.NOCB", vm,
		           "try to resume parent of a state"
		           "that have null comeback");
	}

	/** parent can only be in "waiting subtask" so is not neccessary  */
	/** check the kind of suspend ( ZM_STATEFLAG_WAITING )*/

	if (iter) {
		/*  [ON_RESUME_SWITCH]*/
		if (p->on.iter) {
			p->on.resume = p->on.iter;
		}
	}

	/** comeback reset #COMEBACK_RESET*/
	zm_comeback(s, NULL);

	/*** resume parent*/
	zm_resumeState(vm, p, false);
}


/**
 * must be used inside a session and can be used max 1 time
 */
static void zm_unlinkCurrentState(zm_VM* vm)
{
	zm_Worker *worker = zm_getCurrentWorker(vm);

	/* NOTE: #UNLINKSTATE_SESSION_CURSOR_SYNC
	 *  In zm_task_go: session worker can be different by workercursor
	 *  but workercursor have no mean (for zm_task_go) so session
	 *  worker is the right worker
	 *  In zm_go: session worker and workercursor are the same
	 *  because the operation of unlink current state is the only
	 *  that can broke this sync and can be done only one time in a session
	 */


	ZM_D("zm_unlinkCurrentState w = %s", worker->machine->name);

	zm_scursRemove(vm, worker);

	worker->nstate--;

	if (worker->nstate == 0)
		zm_unlinkWorker(vm, worker);
}


/**
 *
 */
static void zm_suspendCurrentState(zm_VM* vm, zm_Yield ms, int waiting)
{

	/* see. #UNLINKSTATE_SESSION_CURSOR_SYNC*/
	zm_State *state = zm_getCurrentState(vm);

	ZM_D("zm_suspendCurrentState -- state : [ref %lx]", state);

	/* CHECK_OR_NOT */

	state->on.resume = ms.resume;
	state->on.iter = ms.iter;
	state->on.c4tch = ms.c4tch;

	if (waiting)
		zm_enableFlag(state, ZM_STATEFLAG_WAITING);


	zm_unlinkCurrentState(vm);

	/* put temporary worker in next*/
	state->next = (zm_State*)zm_getCurrentWorker(vm);

	zm_disableFlag(state, ZM_STATEFLAG_RUN);

}


static void zm_removeStateFromSiblings(zm_VM *vm, zm_State *s)
{
	zm_State **first;

	if (zm_isTask(s)) {
		first = &(vm->ptasks);
		vm->nptask--;
	} else {
		first = &(zm_getParent(s)->subtasks);
	}

	if (s->siblings.next == s) {
		/* only this state (no siblings) */
		*first = NULL;
	} else {
		/* remove state from siblings list*/
		s->siblings.prev->siblings.next = s->siblings.next;
		s->siblings.next->siblings.prev = s->siblings.prev;

		/* refresh reference (last operation can remove it)*/
		*first = s->siblings.prev;
	}
}




/*---------------------------------------------------------------------------
 *  TERM / ABORT / EXCEPTION
 *  -----------------------------------------------------------------------*/


static void zm_unbindEvent(zm_VM* vm, zm_State *s, int scope);


static zm_Exception* zm_newInnerException(uint8_t kind)
{
	zm_Exception *e = zm_alloc(zm_Exception);
	e->kind = kind;
	e->elock = false;
	e->ecode = 0;
	e->msg = NULL;
	e->data = NULL;
	e->etrace = NULL;
	e->raisestate = NULL;
	e->beforecatch = NULL;

	return e;
}

static void zm_resetInnerException(zm_State* s, zm_Exception *rep)
{
	zm_free(zm_Exception, s->exception);
	s->exception = rep;
}



int zmIsError(zm_Exception *e)
{
	return e->kind == ZM_EXCEPTION_ERROR;
}

static int zm_haveException(zm_State *s, int kind)
{
	if (s->exception)
			if (s->exception->kind == kind)
				return true;

	return false;
}


zm_yield_t izmCATCH(zm_VM* vm, int n)
{
	zm_enableFlag(zm_getCurrentState(vm), ZM_STATEFLAG_CATCH);

	return ZM_B2(n);
}


static int zm_isSyncImplode(zm_LockAndImplode *li)
{
	return (li->by != ZM_IMPLODEBY_ROOT);
}

static void zm_enableWS(zm_LockAndImplode *li)
{
	li->wscheck = ZM_WSCHECK_ALL;
}

static void zm_initLockAndImplode(zm_LockAndImplode *li, int implodeby)
{
	li->deepstack = zm_queueNew();
	li->lockstack = zm_queueNew();

	li->by = implodeby;


	switch (implodeby) {\
	case ZM_IMPLODEBY_SUB:
		li->wscheck = ZM_WSCHECK_ALL;
		break;

	case ZM_IMPLODEBY_EXCEPTION:
		/* in implode by exception the wscheck will be enabled
		 * (zm_enableWS) after set the implode lock in exception
		 * path trace (that contain waiting subtask)*/
	case ZM_IMPLODEBY_ROOT:
		li->wscheck = ZM_WSCHECK_NONE;
		break;

	case ZM_IMPLODEBY_CUR:
		li->wscheck = ZM_WSCHECK_SKIPFIRST;
		break;
	}

	#if ZM_DEBUG_LEVEL >= 1
	ZM_D("init implode %s", zm_getImplodeFlagName(implodeby));
	ZM_D("init implode %s", zm_getWSCheckFlagName(li->wscheck));
	#endif


	li->econtinue = (zm_isSyncImplode(li)) ? zm_queueNew() : NULL;


	li->justlock.count = 0;
	li->justlock.exception = NULL;
	li->justlock.state = NULL;
	li->running = NULL;

	li->fromdeep = 0;
	li->todeep = 0;
	li->count = 0;

	li->chaintail = NULL;
}


/**
 * Append a state to exception traceback (take track of
 * states between the raise and the catch) state exception
 * can be an error-exception or a continue exception without
 * a catch
 */
static void zm_appendExceptionTrace(zm_VM *vm, zm_Exception* e, zm_State *state)
{
	zm_Trace* t = zm_alloc(zm_Trace);

	ZM_D("append Trace Exception state = [ref %lx]", state);

	t->taskid = (size_t)state;
	t->machinename = zm_getMachineName(vm, state);
	t->on = state->on.resume;
	t->filename = state->codeframe.filename;
	t->nline = state->codeframe.nline;

	ZM_D("append Trace Exception: fn = %s nl = %d", t->filename, t->nline);

	if (t->filename == NULL) {
		printf("HERE");
		exit(0);
	}

	/** The final trace is stored in reverse order (the raised */
	/** exception is a the end of the linked list) #EXCEPT_WORKFLOW*/
	 t->next = e->etrace;
	 e->etrace = t;
}


static void zm_deepStackPush(zm_VM *vm, zm_StateQueue *deepstack, zm_State *s)
{
	zm_State *sub = s->subtasks;

	if (!sub)
		return;


	do {
		zm_queueAdd(deepstack, sub, NULL);

		#ifdef ZM_CHECK_CONSISTENCY
		if (!sub->siblings.next) {
			zm_fatalDo(ZM_FATAL_UN, "DPLCK.NS", vm,
			           "null ref in siblings ring");
		}
		#endif

		sub = sub->siblings.next;
	} while (sub != s->subtasks);
}


static void zm_checkWS(zm_VM *vm, zm_LockAndImplode* li, zm_State *s)
{
	ZM_D("check ws: %lx", s);
	switch (li->wscheck) {
	case ZM_WSCHECK_NONE: return;
	case ZM_WSCHECK_ALL: break;

	case ZM_WSCHECK_SKIPFIRST:
		li->wscheck = ZM_WSCHECK_ALL;
		return;
	}

	ZM_D("check ws: have wating...");
	if (zm_hasntFlag(s, ZM_STATEFLAG_WAITING))
		return;

	/* mark continue exception */
	do {
		ZM_D("check ws: follow comeback: %s", s);
		if (zm_hasFlag(s, ZM_STATEFLAG_CONTINUEMARK))
			return;

		ZM_D("check ws: follow comeback enable contmark flag");
		zm_enableFlag(s, ZM_STATEFLAG_CONTINUEMARK);

		if (zm_haveException(s, ZM_EXCEPTION_CONTINUEREF)) {
			ZM_D("check ws: found contref");
			zm_queueAdd( li->econtinue, s, NULL);
			return;
		}

		s = zm_getCaller(s);
	} while(s);

	zm_fatalDo(ZM_FATAL_UN, "ILWCHK.UC", vm,
	           "continue exception not found at the end "
	           "of waiting sub state chain");
}


static void zm_checkContinue(zm_VM *vm, zm_LockAndImplode *li)
{
	zm_State *state;
	int broken = false;

	ZM_D("checkContinue - begin: %lx", li->econtinue);
	while ((state = zm_queuePop0(li->econtinue, NULL))) {
		ZM_D("checkContinue - pop %lx", state);

		if (zm_hasntFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
			broken = true;
			break;
		}
	}

	ZM_D("checkContinue - empty ... broken = %d", broken);

	if (broken) {
		zm_fatalOn(li->refname, li->filename, li->nline);
		zm_fatalDo(ZM_FATAL_ERROR, "CONTBREAK", vm,
		           "Close operation broke a continue-exception "
		           "suspended block");
	}


	zm_queueFree(li->econtinue);
	li->econtinue = NULL;

	ZM_D("checkContinue - end");
	return;
}


/**
 *
 */
static void zm_implosionUnlinkRunning(zm_VM* vm, zm_State *state,
                                           zm_LockAndImplode *li)
{
	/* #ASYNC_SERIALIZATION [step 1]*/
	if (li->by != ZM_IMPLODEBY_ROOT) {
		zm_fatalDo(ZM_FATAL_UN, "LIUNLN.RUN", vm,
		           "found a running state in a sync implosion");
	}

	if (li->running) {
		zm_fatalDo(ZM_FATAL_UN, "LIUNLN.2R", vm, "more than "
		           "one running state in async implosion");
	}

	/** set implode running reference*/
	li->running = state;

	/* state cannot be currentstate (see ABRT.SELF, DEEPLCK.SELF)  */
	state->vmop = ZM_MACHINEOP_UNLINK_TASK_AND_IMPLODE;
}



static void zm_setImplodeLock(zm_VM *vm, zm_LockAndImplode* li, zm_State *state)
{
	size_t deep;

	if (state->flag & ZM_STATEFLAG_EVENTLOCKED) {
		zm_unbindEvent(vm, state, ZM_EVENT_UNBIND_ABORT);
	}

	/** save current zmop in iter to be extract with*/
	/** zmGetCloseOp*/
	state->on.iter = state->on.resume;
	state->on.resume = ZM_TERM;
	/** state->catch must be preserved to allow catch*/
	/** in ZM_TERM #LOCK_SAVE_CATCH (this happend when a task */
	/** is aborted before process the catch) */

	/* This must be done after eventUnbind call because*/
	/* when it resume parent check implosion lock flag */
	/* #UNBIND_IMLOCK*/
	state->flag |= ZM_STATEFLAG_IMPLOSIONLOCK;

	state->vmop = ZM_MACHINEOP_CLOSE_TASK;

	if (!li)
		return;

	deep = zm_getDeep(state);

	ZM_D("deepLock.setImplodeLock - queuestack add state %lx", state);
	zm_queueAdd(li->lockstack, state, NULL);


	if (!li->count) {
		li->fromdeep = deep;
		li->todeep = deep;
	} else {
		/* from and to are traslated by 1*/
		if (deep < li->fromdeep) {
			li->fromdeep = deep;
		}

		if (deep > li->todeep) {
			li->todeep = deep;
		}
	}

	zm_checkWS(vm, li, state);


	if (zm_hasFlag(state, ZM_STATEFLAG_RUN)) {
		/* #ASYNC_SERIALIZATION:
		   async serialization is composed by 3 step:
		   1) set a special vmop to the running state in implode lock
		   2) after serialization set a fake exception in running
		      state that contain the state where implosion start
		   3) the special vmop allow unlink the running state and
		      resume the implosion start state

		   step 1), 2) are sync to this operation while 3) is async
		*/
		zm_implosionUnlinkRunning(vm, state, li);
	}

	li->count++;

	ZM_D("deepLock.setImplodeLock - end");
}


static void zm_deepLockOverlap(zm_VM *vm, zm_LockAndImplode* li, zm_State *s)
{
	switch (li->by) {
	case ZM_IMPLODEBY_ROOT:
		if (zm_haveException(s, ZM_EXCEPTION_ERROR)) {
			if (li->justlock.exception)
				zm_fatalDo(ZM_FATAL_UN, "DEEPOV.2E",
				           vm, "deep lock found more than"
				           "one exception");

			li->justlock.exception = s->exception;
		}

		li->justlock.state = s;
		li->justlock.count++;

		return;


	case ZM_IMPLODEBY_EXCEPTION:
		return;
	}

	/* in ZM_IMPLODEBY_SUB , ZM_IMPLODEBY_CUR overlap should not
	   be found */

	zm_fatalOn(li->refname, li->filename, li->nline);
	zm_fatalDo(ZM_FATAL_SYNC," DEEPOV.ILK", vm,
	           "Unexpected lock and implode flag found "
	           "in sync deep lock");

}



static void zm_deepLock(zm_VM *vm, zm_State *state, zm_LockAndImplode *li)
{
	ZM_D("deepLock - init");

	if (state)
		zm_queueAdd(li->deepstack, state, NULL);

	/* deepstack is used to recursive lock subtasks
	 * (without recursive functions)
	 */
	while ((state = zm_queuePop0(li->deepstack, NULL))) {
		if ((vm->plock) && (state == zm_getCurrentState(vm))) {
			zm_fatalOn(li->refname, li->filename, li->nline);
			zm_fatalDo(ZM_FATAL_SYNC, "DEEPLCK.SELF", vm,
			           "unexpected self-close");
		}


		if (zm_hasntFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
			/* add implode lock and add subtasks to the deepstack */
			zm_setImplodeLock(vm, li, state);

			/* add subtasks to the deepstack */
			zm_deepStackPush(vm, li->deepstack, state);
		} else {
			/* the lib allow only two abort at the same time
			 * one caused by a sync close (for example zmTERM,
			 * zmERROR ...) that lock any other kind of
			 * subtask-abort.
			 * The second close can be only an async one, over
			 * the entire ptask (zm_abortTask).
			 */
			zm_deepLockOverlap(vm, li, state);
		}
	}

	zm_queueFree(li->deepstack);


	ZM_D("deepLock - end");
}



static zm_StateQueue** zm_lockToDeepStack(zm_LockAndImplode *li)
{
	zm_StateQueue** implodestack;
	zm_State *state;
	size_t from = li->fromdeep;
	size_t fromto = 1 + li->todeep - from;
	size_t i;

	ZM_D("lock2deepstack - from=%d, to=%d", li->fromdeep, li->todeep);

	/** create the implosion deep stacks*/
	implodestack = zm_nalloc(zm_StateQueue*, fromto);

	for (i = 0; i < fromto; i++) {
		implodestack[i] = zm_queueNew();
	}

	ZM_D("lock2deepstack - add elements in implosion stack by deep");
	/* add element in implosion stack by deep*/
	while ((state = zm_queuePop0(li->lockstack, NULL))) {
		size_t n = zm_getDeep(state);

		zm_queueAdd(implodestack[n - from], state, NULL);
	}

	ZM_D("lock2deepstack - free lockstack");
	zm_queueFree(li->lockstack);

	return implodestack;
}

static void zm_pushAsyncImplosionStart(zm_State *running, zm_State *start)
{
	/* #ASYNC_SERIALIZATION [step 2]*/
	zm_Exception *e;
	/*zm_getRootContext(running)->implosionstart = last;*/

	e = zm_newInnerException(ZM_EXCEPTION_STARTIMPLOSION);
	/** save implosion start in raisestate*/
	e->raisestate = start;
	/** save exception in data*/
	e->data = running->exception;

	running->exception = e;
}


static zm_State* zm_popAsyncImplosionStart(zm_State *state)
{
	/* #ASYNC_SERIALIZATION [step 3]*/
	zm_Exception *e = (zm_Exception *)state->exception->data;

	zm_State *implosionstart = state->exception->raisestate;

	zm_resetInnerException(state, e);

	return implosionstart;
}

static void zm_serialize(zm_State *state, zm_State *tail)
{
	zm_comeback(state, tail);

	if (zm_hasntFlag(state, ZM_STATEFLAG_RUN))  {
		/* all substate in an implosion is in waiting-subtask except
		 * the implosion start state #IMPLODE_WAITING_CHAIN */
		zm_enableFlag(state, ZM_STATEFLAG_WAITING);
	}
}


static zm_State *zm_serializeImplosion(zm_LockAndImplode *li,
                                zm_StateQueue **implodestack)
{
	size_t from = li->fromdeep;
	size_t to = li->todeep;
	size_t fromto = 1 + to - from;
	size_t i;

	zm_State *state, *last;

	if (!li->count)
		return NULL;

	state = zm_queuePop0(implodestack[0], NULL);

	if (li->chaintail)
		zm_serialize(state, li->chaintail);

	ZM_D("i-serialize - first = %lx", state);

	last = state;

	for (i = from; i <= to; i++) {
		ZM_D("i-serialize - deep = %d", i);

		while ((state = zm_queuePop0(implodestack[i-from],NULL))) {
			ZM_D("i-serialize - comeback(%lx) = %lx", state,last);
			zm_serialize(state, last);
			last = state;
		}

		zm_queueFree(implodestack[i - from]);
	}


	ZM_D("i-serialize - free implodestack[%d]", fromto);

	zm_nfree(zm_StateQueue*, fromto, implodestack);

	return last;
}

static void zm_startImplosion(zm_VM *vm, zm_LockAndImplode *li, zm_State *last)
{

	ZM_D("startImplode [ref %lx]?", last);
	if (li->justlock.count) {
		/* link sync with async lock and implode */
		zm_State *state = li->justlock.state;

		if (li->justlock.count > 1)
			state = li->justlock.exception->beforecatch;

		/* link async lock and implode with sync one */
		zm_comeback(state, last);
		return;
	}

	/** last pop element is the first to run*/
	ZM_D("starImplode - have a running state [ref %lx]?", li->running);

	if (li->running) {
		if (li->running == last)
			return;

		ZM_D("starImplode - async serialization");
		/* #ASYNC_SERIALIZATION [step 2]*/
		/* there is just a running state, set last in root to be */
		/* resumed after running state as been suspended */
		zm_pushAsyncImplosionStart(li->running, last);
	} else {
		ZM_D("startImplode - resume [ref %lx]", last);
		zm_resumeState(vm, last, false);
	}
}

static void zm_implode(zm_VM *vm, zm_LockAndImplode *li)
{
	zm_State *last;
	zm_StateQueue** implodestack;

	ZM_D("implode - lock to deep stack");

	implodestack = zm_lockToDeepStack(li);


	/* serialize comeback from top to bottom pop elements in each
	 * deep level from the bottom to the top of the stack linking
	 * each element with the previous
	 */
	ZM_D("implode - serialization");

	last = zm_serializeImplosion(li, implodestack);

	if (last) {
		/* resume or link to exception to run implosion */
		ZM_D("implode - set implosion head");
		zm_startImplosion(vm, li, last);
	}

	ZM_D("zm_implode - end");
}









static void zm_lockByException(zm_VM *vm, zm_LockAndImplode *li, zm_State *s)
{
	if (zm_hasFlag(s, ZM_STATEFLAG_IMPLOSIONLOCK)) {
		zm_fatalDo(ZM_FATAL_UN, "LCKBE.IL", vm,
		           "unexpected lock and implode flag in exec-stack "
		           "(along exception trace)");
	}

	if ((s->on.c4tch) && zm_hasntFlag(s, ZM_STATEFLAG_CATCH)) {
		/* zmRESET */
		ZM_D("lockbyexception: reset comeback of %lx", s);

		if (zm_isTask(s)) {
			zm_fatalDo(ZM_FATAL_UN, "LCKBE.PT", vm,
			           "ptask without catch should be just "
			           "report as uncaught exception");
		}

		/* set resume point to reset one (saved in c4tch) */
		s->on.resume = s->on.c4tch;

		/* a reset state is suspended: no waiting, no comeback */
		zm_disableFlag(s, ZM_STATEFLAG_WAITING);
		zm_comeback(s, NULL);
		return;
	}

	zm_setImplodeLock(vm, li, s);

	/* add subtasks to the deepstack */
	zm_deepStackPush(vm, li->deepstack, s);
}


static void zm_lockAndImplodeByException(zm_VM *vm, zm_State *state,
                                            zm_Exception *exception)
{
	zm_LockAndImplode li;
	zm_State *next;
	int skipfirst = 0;

	zm_initLockAndImplode(&li, ZM_IMPLODEBY_EXCEPTION);

	ZM_D("zm_lockAndImplodeByException - 1");

	/** calculate: from and to */
	do {
		if (skipfirst++)
			zm_appendExceptionTrace(vm, exception, state);

		/* save next because lock by exception can reset comeback
		 * to accomplish zmRESET */
		next = zm_getCaller(state);

		/* this check must be done before lockByException
		 * (see LCKBE.PT) */
		if (next == NULL) {
			/** fatal error: catch not found **/
			zm_fatalException(exception);
			zm_fatalDo(ZM_FATAL_NOCATCH, "NOCATCH.E", vm,
			           "exception kind = error");
		}

		zm_lockByException(vm, &li, state);

		exception->beforecatch = state;
		state = next;
	} while(zm_hasntFlag(state, ZM_STATEFLAG_CATCH));


	zm_enableWS(&li);

	ZM_D("zm_lockAndImplodeByException - catch on %lx", state);

	/* set the tail of the implosion */
	li.chaintail = state;

	/* set state->exception in catch state */
	state->exception = exception;

	zm_deepLock(vm, NULL, &li);

	zm_checkContinue(vm, &li);

	zm_implode(vm, &li);
}

static void zm_implodeCoroutine(zm_VM *vm, zm_State *state)
{
	ZM_D("implode coroutine");
	zm_setImplodeLock(vm, NULL, state);

	if (zm_hasntFlag(state, ZM_STATEFLAG_RUN))
		zm_resumeState(vm, state, false);
}

static void zm_lockAndImplodeBy(zm_VM *vm, zm_State *state, int implodeby,
                                const char* refname, const char *filename,
                                                                int nline)
{
	zm_LockAndImplode li;


	ZM_D("lockAndImplodeBy - %s state = %lx",
	     zm_getImplodeFlagName(implodeby), state);

	if (zm_isTask(state)) {
		/**** ptask ****/
		if (!state->subtasks) {
			zm_implodeCoroutine(vm, state);
			return;
		}

		zm_initLockAndImplode(&li, implodeby);

		zm_deepLock(vm, state, &li);

		if (zm_isSyncImplode(&li))
			zm_checkContinue(vm, &li);

		zm_implode(vm, &li);

		return;
	}

	/**** subtask ****/

	zm_initLockAndImplode(&li, implodeby);

	/* set the tail of the implosion:
	 * - zmTERM     don't change comeback
	 * - zmCLOSE    set the comeback as the state that invoke close */
	li.chaintail = zm_getCaller(state);

	/* calculate: from and to */
	zm_deepLock(vm, state, &li);

	zm_checkContinue(vm, &li);

	zm_implode(vm, &li);
}


static void zm_uncaughtContinue(zm_VM *vm, zm_State *s, zm_Exception *e)
{
	do {
		zm_appendExceptionTrace(vm, e, s);
		s = zm_getCaller(s);
	} while(s);

	zm_fatalException(e);
	zm_fatalDo(ZM_FATAL_NOCATCH, "NOCATCH.C", vm,
	           "exception kind = continue");
}


static void zm_removeTrace(zm_Exception *e)
{
	zm_free(zm_Trace, e->etrace);
	e->etrace = NULL;
}


static zm_State* zm_getLastBeforeContinueCatch(zm_VM *vm, zm_State *s,
                                                      zm_Exception *e)
{
	zm_State *prev, *first = s;

	ZM_D("getLastBeforeCatch: state s = %lx", s);
	do {
		prev = s;
		s = zm_getCaller(s);

		ZM_D("getLastBeforeCatch: state s = %lx", s);

		if (s == NULL) {
			zm_uncaughtContinue(vm, first, e);
			return NULL;
		}
	} while(zm_hasntFlag(s, ZM_STATEFLAG_CATCH));

	ZM_D("getLastBeforeCatch: found! end state = %lx", prev);
	/* trace in continue-exception is instanced only to show trace
	 * if continue-exception don't find a catch */
	zm_removeTrace(e);

	return prev;
}


static void zm_abortTask(zm_VM *vm, zm_State *state, const char *refname)
{

	if (zm_hasFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
		/* This check is mandatory because zm_deepLock can set
		 * one exception and can manage only one other exception just
		 * present. After zm_abortTask there can be more than one
		 * exception and a second call of zm_abortTask will cause a
		 * fatal. Moreover this check avoid an useless second lock
		 * and implode. */
		return;
	}

	ZM_D("zm_abortTask: %lx by %s", state, refname);
	zm_lockAndImplodeBy(vm, state, ZM_IMPLODEBY_ROOT, refname, NULL, 0);
}

void zm_abort(zm_VM *vm, zm_State *state)
{
	const char *refname = "zm_abort";

	if (zm_isSubTask(state)) {
		zm_fatalOn(refname, NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "ABRT.SUB", vm,
		           "expected a task but found a subtask");
	}

	if (vm->plock) {
		if (state == zm_getCurrentState(vm)) {
			zm_fatalOn(refname, NULL, 0);
			zm_fatalDo(ZM_FATAL_SYNC, "ABRT.ME", vm,
			           "cannot close current processed ptask "
			           "with zm_abort (use: zmyield zmTERM)");

		}

		if (zm_haveSameContext(state, zm_getCurrentState(vm))) {
			zm_fatalOn(refname, NULL, 0);
			zm_fatalDo(ZM_FATAL_SYNC, "ABRT.SELF", vm,
			           "cannot abort a ptask that is the parent "
			           "of the current processed task");
		}
	}

	zm_abortTask(vm, state, refname);
}

#define zm_fatalWrongCtx(ecode, s)                                            \
    do {                                                                      \
    if (!zm_haveSameRoot(current, s)) {                                       \
        zm_fatalOn(refname, filename, nline);                                 \
        zm_fatalDo(ZM_FATAL_UCODE, ecode, vm, nsamectx);                      \
    } else {                                                                  \
        zm_fatalOn(refname, filename, nline);                                 \
        zm_fatalDo(ZM_FATAL_UCODE, ecode, vm, wrongres);                      \
    }                                                                         \
    } while(0);


static void zm_canBeContextStackPush(zm_VM *vm, zm_State *sub,
                                          const char *refname,
                                         const char *filename,
                                                    int nline)
{
	const char *nsamectx = "try to resume a subtask owned by another task";
	const char *wrongres = "try to resume a subtask inaccesible from "
	                       "this position";

	zm_State *current = zm_getCurrentState(vm);

	if (zm_isTask(current)) {
		if (zm_deep(sub) != 1) {
			zm_fatalWrongCtx("WRONGCTX.ROOT", sub);
		}

		if (!zm_haveSameRoot(current, sub)) {
			zm_fatalOn(refname, filename, nline);
			zm_fatalDo(ZM_FATAL_UCODE, "WRONGCTX.PT", vm, nsamectx);
		}
		return;
	}

	/**
	      +subsub3
	      |
	      |   subsubsub
	      |   |
	      +subsub2
	      |
	  +--sub1
	  |
	  +--iterB
	  |
	  |    subiterA
	  |    |
	  +--iterA
	  |
	PTask

	yield down:
		@subsubsub: yield iterA      [OK]
		@subsubsub: yield subsub3    [OK]
		@subsubsub: yield subiterA   [WRONG]

	yield up (child-yield):          [OK]
		@sub1: yield subsub2         [OK]
		@sub1: yield subsubsub       [WRONG]

	yield sibling:
		@subsub2: yield subsub3      [OK]
		@iterA: yield iterB          [OK]

	*/

	if (zm_deep(current) >= zm_deep(sub)) {
		/* yield sibling and yield down check */

		if (current->parent->stack[zm_deep(sub) - 1] !=
				zm_getParent(sub)) {

			zm_fatalWrongCtx("WRONGCTX.LE", sub);
		}
		/* this check allow in subsubsub to yield to his
		   parent subsub2 but the yield up check make it impossible
		   without a fatal (see ACTSUB.WP)  */
	} else {
		/* current can yield up only to a child:
		   A) current.deep == sub.deep -1
		   B) sub->parent == current
		 */
		if (zm_deep(current) != zm_deep(sub) - 1) {
			/* A) deep check fail */
			zm_fatalWrongCtx("WRONGCTX.DP", sub);

		} else if (zm_getParent(sub) != current) {
			/* B) sub is not a child of current  */

			zm_fatalWrongCtx("WRONGCTX.GT", sub);
		}
	}
}


zm_yield_t izmCLOSE(zm_VM *vm, zm_State *state, const char *filename, int nline)
{
	ZM_D("zmCLOSE(%lx)", state);

	ZM_ASSERT_VMLOCK("ICLOSE.VLCK", "zmCLOSE", filename, nline);

	if (zm_isTask(state)) {
		zm_fatalOn("zmCLOSE", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ICLOSE.T", vm,
		           "expected a subtask but found a task");
	}

	if (zm_getParent(state) != zm_getCurrentState(vm)) {
		zm_fatalOn("zmCLOSE", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ICLOSE.NC", vm,
		           "zmCLOSE can close only its child");
	}

	zm_comeback(state, zm_getCurrentState(vm));

	zm_lockAndImplodeBy(vm, state, ZM_IMPLODEBY_SUB, "zmCLOSE",filename,
	                    nline);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}

static int zm_canCatch(zm_State *s)
{
	if (zm_isTermState(s->on.resume))
		return true;

	return s->on.c4tch;
}

void izmSetData(zm_VM *vm, void *data)
{
	vm->currentsession.state->data = data;
}

zm_Exception *izmCatchException(zm_VM *vm, int ekindfilter, const char* refname,
                                                const char *filename, int nline)
{
	/* #EXCEPT_WORKFLOW #CONTINUE_EXCEPT*/
	zm_Exception *e = zm_getCurrentState(vm)->exception;

	ZM_ASSERT_VMLOCK("XCATCH.VLCK", refname, filename, nline);

	/* This assertion force to catch an exception only in the
	 * catch state (the last that have exception in state->exeption)
	 * to avoid to catch and free exception before other subtasks
	 * with the same exception have been closed.

	 * This assertion work also in term vmstate because
	 * lock and implode preserve catch #LOCK_SAVE_CATCH */

	ZM_D("%s: exception = %lx - filter = %d", refname, e, ekindfilter);

	if (!zm_canCatch(zm_getCurrentState(vm))) {
		zm_fatalOn(refname, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "XCATCH.NC", vm,
		           "Cannot invoke a catch exception in "
		           "a non catch vmstate");
	}


	/* Exception can also be null because if we set the same vmstate
	 * for resume and catch ( example: yield 4 | vmCATCH(4) )
	 * zmCatch can also be used to discriminate catch (return null)
	 * from resume (return exception pointer) or to check if there is
	 * an exception that must be free (this happend when a task
	 * is aborted before process a catch) */
	if (e == NULL) {
		return NULL;
	}

	switch(e->kind) {
	case ZM_EXCEPTION_ERROR:
	case ZM_EXCEPTION_CONTINUE:
		break;

	case ZM_EXCEPTION_STARTIMPLOSION:
	case ZM_EXCEPTION_CONTINUEREF:
		return NULL;

	default:
		zm_fatalDo(ZM_FATAL_UN, "CTCEXCPT.EUN", vm,
		           "unknow exception->kind = %d", e->kind);
	}

	if (ekindfilter) {
		if (ekindfilter != e->kind)
			return NULL;
	}

	return e;
}



void izmFreeException(zm_VM *vm, const char *filename, int nline)
{
	/* This cannot be an api (only freeException macro) for the exception
	 * axiom 3 #ASSIOMI_ECCEZIONI */
	zm_Exception *e = zm_getCurrentState(vm)->exception;

	ZM_ASSERT_VMLOCK("FREEX.VLCK", "zmFreeException", filename, nline);

	if (!zm_canCatch(zm_getCurrentState(vm))) {
		zm_fatalOn("zmFreeException", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "FREEX.1", vm,
		           "Cannot be invoked in a non catch vmstate");
	}

	/* #EXCEPT_WORKFLOW #CONTINUE_EXCEPT */

	if (e == NULL) {
		zm_fatalOn("zmFreeException", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "FREEX.2", vm,
		           "no exception to free");
	}

	switch(e->kind) {
	case ZM_EXCEPTION_ERROR: {
			zm_Trace *t, *t2;

			t = e->etrace;
			while (t) {
				t2 = t->next;
				zm_free(zm_Trace, t);
				t = t2;
			}
		} break;

	case ZM_EXCEPTION_CONTINUE:
		/* nothing to do */
		break;

	case ZM_EXCEPTION_STARTIMPLOSION:
	case ZM_EXCEPTION_CONTINUEREF:
		zm_fatalOn("zmFreeException", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "FREEX.3", vm,
		           "no exception to free");

	default:
		zm_fatalDo(ZM_FATAL_UN, "FREEX.EUN", vm,
		           "exception->kind = unknow");
	}

	e->msg = NULL;
	e->data = NULL;
	e->elock = false;

	/* remove exception reference from state */
	zm_getCurrentState(vm)->exception = NULL;
}


static zm_Exception* zm_newException(zm_VM *vm, bool error, int ecode,
                                          const char *msg, void *data,
                                      const char *filename, int nline)
{

	/* This cannot be an api (only freeException macro) for the exception
	 * axiom 1 #ASSIOMI_ECCEZIONI */

	zm_Exception* e = zm_alloc(zm_Exception);

	e->kind = (error) ? (ZM_EXCEPTION_ERROR) :
			(ZM_EXCEPTION_CONTINUE);

	e->msg = msg;
	e->ecode = ecode;
	e->data = data;

	e->elock = true;
	/* beforecatch is set by zm_implode for error-exception or by
	 * getLastBeforeCatch for continue-exception */
	e->beforecatch = NULL;

	e->etrace = zm_alloc(zm_Trace);
	e->etrace->taskid = (size_t)zm_getCurrentState(vm);
	e->etrace->filename = filename;
	e->etrace->nline = nline;
	e->etrace->machinename = zm_getCurrentMachineName(vm);
	e->etrace->on = zm_getCurrentState(vm)->on.resume;
	e->etrace->next = NULL;

	/* #CONTINUE_EXCEPT */
	e->raisestate = zm_getCurrentState(vm);

	return e;
}



static zm_yield_t zm_raiseException(zm_VM *vm, zm_Exception* e, bool error)
{
	zm_State *state = zm_getCurrentState(vm);

	/* #EXCEPT_WORKFLOW #CONTINUE_EXCEPT */

	ZM_D("raise Exception");

	/* set exception */
	state->exception = e;

	if (error) {
		return ZM_TASK_RAISE_ERROR_EXCEPTION;
	} else {
		return ZM_TASK_RAISE_CONTINUE_EXCEPTION;
	}
}


zm_yield_t izmDROP(zm_VM *vm, zm_Exception* e, const char *filename, int nline)
{

	ZM_ASSERT_VMLOCK("DROPERR.VLCK", "zmDROP", filename, nline);

	if (e->kind != ZM_EXCEPTION_ERROR) {
		zm_fatalOn("zmDROP", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "DROPERR.1", vm,
		           "zmDROP need an error-exception");
	}

	if (zm_isTask(zm_getCurrentState(vm))) {
		/* #NO_RAISE_IN_PTASK  */
		zm_fatalOn("zmDROP", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "DROPERR.2", vm,
		           "exception can be drop only in subtask");
	}

	return zm_raiseException(vm, e, true);
}

zm_yield_t izmEXCEPTION(zm_VM *vm, bool error, int ecode, const char *msg,
                              void *data, const char *filename, int nline)
{
	const char *refname = ((error) ? "zmERROR" : "zmCONTINUE");
	zm_Exception* e;

	ZM_D("%s(error = %d, ecode = %d, %s)", refname,
	         error, ecode, (msg != NULL) ? msg : "[NULL]");

	ZM_ASSERT_VMLOCK("RAISENEW.VLCK", refname, filename, nline);

	e = zm_newException(vm, error, ecode, msg, data, filename, nline);

	if (zm_isTask(zm_getCurrentState(vm))) {
		/* #NO_RAISE_IN_PTASK */
		zm_fatalOn(refname, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "RAISENEW.1", vm,
		           "exception can be raise only in subtask");
	}

	return zm_raiseException(vm, e, error);
}


static void zm_unraise(zm_VM *vm, zm_State* state, const char *ref,
                                              const char *filename,
                                                         int nline)
{
	zm_Exception *e = state->exception;

	zm_resumeStateBy(vm, e->raisestate, false, ref, filename, nline);

	zm_comeback(e->beforecatch, zm_getCurrentState(vm));

	zm_free(zm_Exception, e);

	state->exception = NULL;
}


/**
 * e.g. yield zmUNRAISE
 */
zm_yield_t izmUNRAISE(zm_VM *vm, zm_State* state, const char *fn, int nl)
{
	ZM_D("unraise Exception");

	/* #CONTINUE_EXCEPT*/
	if (zm_isTask(state)) {
		zm_fatalOn("zmUNRAISE", fn, nl);
		zm_fatalDo(ZM_FATAL_UCODE, "UNRAISE.S", vm,
		           "unraise can be applied only to subtask");

	}

	zm_canBeContextStackPush(vm, state, "zmUNRAISE", fn, nl);

	if (!state->exception) {
		zm_fatalOn("zmUNRAISE", fn, nl);
		zm_fatalDo(ZM_FATAL_UCODE, "UNRAISE.NE", vm,
		           "unraise can be applied only to subtask with "
		           "continue-exception (no exception found)");
	}

	if (state->exception->kind != ZM_EXCEPTION_CONTINUEREF) {
		zm_fatalOn("zmUNRAISE", fn, nl);
		zm_fatalDo(ZM_FATAL_UCODE, "UNRAISE.WK", vm,
		           "unraise can be applied only to subtask with "
		           "continue-exception (exception: %s)",
		           zm_getExceptionKindName(state->exception));
	}


	zm_unraise(vm, state, "zmUNRAISE", fn, nl);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}




uint8_t izmGetCloseOp(zm_VM *vm, const char *filename, int nline)
{
	zm_State *state = zm_getCurrentState(vm);

	ZM_ASSERT_VMLOCK("GETCLSOP.VLCK", "zmGetCloseOp", filename, nline);

	/* #LOCK_SAVE_CATCH */
	if (zm_hasntFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
		zm_fatalOn("zmGetCloseOp", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "GETCLSOP.1", vm,
		           "this function can be call only in "
		           "ZM_TERM");
	}

	return state->on.iter;
}

#if 0
zm_Trace* zm_getTraceback(zm_Exception *e)
{
	return e->etrace;
}
#endif



/*---------------------------------------------------------------------------
 *  EVENT BIND/UNBIND/TRIGGER
 *  -----------------------------------------------------------------------*/



static void zm_bindEvent(zm_Event *event, zm_State *s)
{

	zm_EventBinder *evb = zm_alloc(zm_EventBinder);

	zm_enableFlag(s, ZM_STATEFLAG_EVENTLOCKED);

	/* save state->next in event binder (now contain next state) */
	evb->statenext = (void*)s->next;

	/* save evb inside state next (to allow switch in busy_waiting_event)*/
	s->next = (zm_State*)evb;

	/* In ZM_TASK_BUSY_WAITING_EVENT state->next will be temporary
	 * recovered using statenext to call zm_suspendCurrentState
	 * that replace state->next with the worker. After worker
	 * in state->next will be saved (again) in evb->statenext and
	 * state->next will be replaced with evb    #EVENT_BIND
	 *
	 * during this transaction state have event flag [we] but not
	 * waiting flag #EVB_FLAG
	 */

	evb->event = event;
	evb->owner = s;
	event->count++;


	if (!event->bindlist) {
		evb->next = evb; /* ring */
		evb->prev = evb;

		event->bindlist = evb;
	} else {
		evb->prev = event->bindlist->prev;
		evb->next = event->bindlist;

		event->bindlist->prev->next = evb;
		event->bindlist->prev = evb;
	}
}

static const char* zm_getUnbindEventScope(uint8_t flag)
{
	if (flag & ZM_EVENT_UNBIND_REQUEST)
		return "REQUEST";

	if (flag & ZM_EVENT_UNBIND_TRIGGER)
		return "TRIGGER";

	if (flag & ZM_EVENT_UNBIND_ABORT)
		return "ABORT";


	return "??UNBIND_SCOPE_UNKNOW??";
}


/* note: event binder free evb reference*/
static void zm_unbindEvent(zm_VM* vm, zm_State *s, int scope)
{
	zm_EventBinder *evb = ((zm_EventBinder*)s->next);

	ZM_D("zm_unbindEvent: check flag");

	if (s->flag & ZM_STATEFLAG_EVENTLOCKED) {
		zm_disableFlag(s, ZM_STATEFLAG_EVENTLOCKED);
	} else {
		zm_fatalDo(ZM_FATAL_UN, "UNBNDEV", NULL,
		           "event unbind (scope = %s): state doesn't "
		           " have a binded event",
		           zm_getUnbindEventScope(evb->event->flag));
	}

	if (evb->event->unbind)
		evb->event->unbind(vm, evb->event->data, s->data, scope);


	/* check if evb is the first element of the bindlist*/
	if (evb->event->bindlist == evb) {
		/* check if there is only one element*/
		if (evb->event->bindlist->next == evb) {
			evb->event->bindlist = NULL;
		} else {
			/* set header pointer of the list to second element */
			evb->event->bindlist = evb->event->bindlist->next;
		}
	}


	if (evb->event->bindlist) {
		/* remove evb from list */
		evb->prev->next = evb->next;
		evb->next->prev = evb->prev;
	}

	evb->event->count--;
	s->next = (zm_State*)evb->statenext;

	ZM_D("zm_unbindEvent: resume");
	/* #UNBIND_IMLOCK*/
	zm_resumeState(vm, s, (scope & ZM_EVENT_NOW_TASK));

	ZM_D("zm_unbindEvent: free event binder");
	zm_free(zm_EventBinder, evb);

	ZM_D("zm_unbindEvent: end");
}



static void zm_triggerWrongReturn(zm_VM *vm, int r)
{
	if (r & ZM_EVENT_STOP) {
		zm_fatalOn("zm_trigger", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "TRIG.WRNGFLG.1", vm,
		           "cannot use ZM_EVENT_STOP in trigger "
		           "pre-fetch");
	}

	#if 0
	if ((r & ZM_EVENT_NOW) ==  ZM_EVENT_NOW) {
		zm_fatalOn("zm_trigger", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "TRIG.WRNGFLG.2", vm,
		           "cannot use ZM_EVENT_NOW in trigger "
		           "pre-fetch");
	}
	#endif

	if (r & ZM_EVENT_NOW_TASK) {
		zm_fatalOn("zm_trigger", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "TRIG.WRNGFLG.3", vm,
		           "cannot use ZM_EVENT_NOW_TASK in "
		           "trigger pre-fetch");
	}

	#if 0
	if (r & ZM_EVENT_NOW_TASKS) {
		zm_fatalOn("zm_trigger", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "TRIG.WRNGFLG.4", vm,
		           "cannot use ZM_EVENT_NOW_TASKS in "
		           "trigger pre-fetch");
	}
	#endif

	zm_fatalOn("zm_trigger", NULL, 0);
	zm_fatalDo(ZM_FATAL_ERROR, "TRIG.WRNGFLG.5", vm,
	           "invalid return in trigger callback use: "
	           "ZM_EVENT_ACCEPTED or ZM_EVENT_ACCEPTED");
}


static int zm_triggerEVB(zm_VM *vm, zm_Event *event, void *arguments, int i,
                                                        zm_EventBinder *evb)
{
	zm_Worker *unbinded = NULL;
	int r;

	if (!event->trigger) {
		/* no trigger callback: resume state */
		zm_unbindEvent(vm, evb->owner,
		               ZM_EVENT_UNBIND_TRIGGER |
		               ZM_EVENT_ACCEPTED);
		return false;
	}

	/* ACCEPT accept the event for a single task -> resume task*/
	/* REFUSE leave the event binded and task waiting*/
	/* EXCEPTION resume task raising an exception*/
	/* STOP stop fetch other event-task*/

	ZM_D("zm_trigger: cb(state = [ref %lx])", evb->owner);
	r = event->trigger(vm, event->data, arguments, evb->owner->data, i);

	ZM_D("zm_trigger: result = %d\n", r);

	/* main selection accepted, exception, refused */

	if (r & ZM_EVENT_ACCEPTED) {
		unbinded = ((zm_Worker*)evb->statenext);

		ZM_D("zm_trigger: ACCEPTED - w = %s", unbinded->machine->name);

		#if 0
		zm_unbindEvent(vm, evb->owner,
				ZM_EVENT_UNBIND_TRIGGER |
				ZM_EVENT_ACCEPTED |
				(r & ZM_EVENT_NOW));
		#endif
		zm_unbindEvent(vm, evb->owner,
		               ZM_EVENT_UNBIND_TRIGGER |
		               ZM_EVENT_ACCEPTED |
		               (r & ZM_EVENT_NOW_TASK));

	}

	/*** secondary flags to be processed in trigger ***/

	#if 0
	if (r & ZM_EVENT_NOW_TASKS) {

		/* here process worker change in unbind process */
		/* other flags ZM_EVENT_NOW_TASK*/

		#ifdef ZM_CHECK_CONSISTENCY
		if (!unbinded)
			zm_fatalDo(ZM_FATAL_UN, "TRIG.NU", vm,
			           "unexpected NULL worker "
			           "in unbind event");
		#endif

		ZM_D("zm_trigger: set worker %s\n", unbinded->machine->name);


		/* #BREAK_IN_SESSION_CURSOR_SYNC */
		vm->workercursor = unbinded;

		if (r & ZM_EVENT_NOW_TASK) {
			/* dont rewind because unbind have set the next state*/
		} else {
			/* rewind because event want to give priority to
			 * all task of a choosen machine
			 */

			/* NOTE:
			states rewind is possible also during a vm cycle
			because: no one state in current session
			exec-context can hold this event (because event
			binding put the running state of the exec-context
			in waiting event). Moreover also if session worker
			is the same as unbinded worker race condition
			problem are relative to current state
			(as #UNLINKSTATE_NO_NEXT) but current state are
			triggering not recive a trigger.
			*/

			zm_rewindWorkerStates(vm, unbinded);
		}
	}
	#endif

	if (r & ZM_EVENT_STOP) {
		return true;
	}

	return false;
}


int zm_trigger(zm_VM *vm, zm_Event *event, void *arguments)
{
	zm_EventBinder *evb, *nextevb;
	int r, n, i = 0;

	ZM_D("zm_trigger: PRE-FETCH");
	if (event->trigger) {
		/*** trigger pre-fetch ***/

		/* pass event count*/
		r = event->trigger(vm, event->data, arguments, NULL,
		                   event->count);

		/* In pre-fetch ACCEPTED/REFUSE value accept/refuse (filter) */
		/* the trigger action */
		switch (r) {
		case ZM_EVENT_ACCEPTED:
			break;

		case ZM_EVENT_REFUSED:
			return ZM_EVENT_TRIGGER_REFUSED;

		default:
			zm_triggerWrongReturn(vm, r);
		}
	}


	ZM_D("zm_trigger: FETCH");

	evb = event->bindlist;

	if (!evb) {
		return ZM_EVENT_TRIGGER_PREFETCH;
	}

	n = event->count;

	do {
		ZM_D("zm_trigger: fetch trigger %d", i);
		/*** trigger fetch ***/

		/* save evb->next because unbind free evb (note: evb is
		 * a ring linked list, when evb contain only one element
		 * nextevb == evb this can't be a problem because in this
		 * situation do while end */
		nextevb = evb->next;

		if (zm_triggerEVB(vm, event, arguments, i, evb)) {
			break;
		}

		evb = nextevb;

		ZM_D("zm_trigger: cycle end %d", i);
		i++;
	} while (i < n);


	return ZM_EVENT_TRIGGER_DONE;
}


/*---------------------------------------------------------------------------
 *  WORKER COSTRUCTOR
 *  -----------------------------------------------------------------------*/

static zm_Worker* zm_newWorker(zm_VM *vm, zm_Machine *machine)
{
	zm_Worker* w;

	w = zm_alloc(zm_Worker);

	w->cyclestep = 1;
	w->machine = machine;

	w->states.first = NULL;
	w->states.current = NULL;
	w->states.previous = NULL;
	w->nstate = 0;

	w->next = NULL;

	return w;
}


static void zm_freeWorker(zm_VM* vm, zm_Worker *w)
{
	if (w->nstate) {
		zm_fatalDo(ZM_FATAL_UN, "FREEW.NS", vm,
		           "vm have no ptask but a worker is not empty");
	}

	zm_free(zm_Worker, w);
}





/*---------------------------------------------------------------------------
 *  NEW TASK/SUBTASK/EVENT
 *  -----------------------------------------------------------------------*/



static void zm_addStateToSiblingsRing(zm_State **first, zm_State *state)
{
	if ((*first) == NULL) {
		(*first) = state;
		state->siblings.next = state->siblings.prev = state;
	} else {
		/* put state as the second element
		   (between child and child.next)*/
		state->siblings.next = (*first)->siblings.next;
		state->siblings.prev = (*first);

		(*first)->siblings.next->siblings.prev = state;
		(*first)->siblings.next = state;
	}

}

static void zm_addParent(zm_VM *vm, zm_State* s, const char *ref,
                                 const char *filename, int nline)
{
	zm_Parent *parent = zm_alloc(zm_Parent);
	zm_State *current = zm_getCurrentState(vm);
	size_t deep = zm_getDeep(current) + 1;

	if (zm_hasFlag(current, ZM_STATEFLAG_IMPLOSIONLOCK)) {
		zm_fatalOn(ref, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ADDTASK.P", vm,
		           "cannot create task in a close vmstate");
	}

	parent->stacksize = deep;
	parent->stack = zm_nalloc(zm_State*, deep);

	if (deep > 1) {
		memcpy(parent->stack, current->parent->stack,
		       (deep-1) * sizeof(zm_State*));
	}

	parent->stack[ deep - 1 ] = current;

	parent->comeback = NULL;

	s->parent = parent;
}


zm_State* izm_addTask(zm_VM *vm, zm_Machine *machine, void *data, bool subtask,
                                 uint8_t flag, const char *filename, int nline)
{
	zm_State* state;
	zm_Worker *worker;

	ZM_D("zm_addTask %s: %s", subtask ? "subtask" : "ptask", machine->name);

	/**** Allocate State ****/
	state = zm_alloc(zm_State);

	state->vmop = ZM_MACHINEOP_RUN; /* after resume will be RUN */
	state->flag = flag;

	#ifdef ZM_DEBUG_MACHINENAME
		state->debugmachinename = machine->name;
	#endif

	if (subtask) {
		const char *fname = (flag & ZM_STATEFLAG_AUTOFREE) ?
		                    "zmNewSubTasklet" : "zmNewSubTask";
		zm_State *current;

		ZM_ASSERT_VMLOCK("ADDTASK.VLCK", fname, filename, nline);

		current =  zm_getCurrentState(vm);

		zm_addParent(vm, state, fname, filename, nline);

		zm_addStateToSiblingsRing(&(current->subtasks), state);
	} else {
		state->parent = NULL;

		zm_addStateToSiblingsRing(&(vm->ptasks), state);

		vm->nptask++;
	}


	/* State*/
	state->on.resume = ZM_INIT;
	state->on.iter = 0;
	state->on.c4tch = 0;

	state->data = data;
	state->subtasks = NULL;
	state->exception = NULL;

	state->codeframe.filename = "<not set>";
	state->codeframe.nline = 0;

	worker = zm_mhwGet(vm, machine);

	if (!worker) {
		ZM_D("zm_newWorker: %s", machine->name);

		worker = zm_newWorker(vm, machine);

		zm_mhwAdd(vm, machine, worker);
	}

	/* task and subtask are created suspended */
	state->next = (zm_State*)worker;

	ZM_D("zm_addTask = %lx", state);

	return state;
}


/**
 * The problem with free state is that is a sync request
 * but operations before free a state are managed by vm in
 * an async way. So if the task is ready to be free the
 * request can be performed in a sync way and the function
 * return true otherwise the function return false
 */
static int zm_requestFreeState(zm_VM *vm, zm_State *state, const char* refname)
{
	ZM_D("request free state by: %s state=%lx", refname, state);

	if (zm_hasFlag(state, ZM_STATEFLAG_AUTOFREE)) {
		zm_fatalOn(refname, NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "RFREEST.AF", vm,
		           "Cannot request to free a state "
		           "that is just marked to be free");
	}

	if (state->vmop == ZM_MACHINEOP_NO_MORE_TO_DO) {
		/*** this vmop is set by ZM_MACHINEOP_END_TASK*/
		/*** then is possible to free state in a sync way*/
		zm_free(zm_State, state);
		return true;
	}

	if (zm_hasntFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
		zm_fatalOn(refname, NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "RFREEST.AT", vm,
		           "cannot free an active task");
	}

	/* if ZM_STATEFLAG_IMPLOSIONLOCK the state will be closed but
	 * is not just closed because vmop != ZM_MACHINEOP_NO_MORE_TO_DO
	 * ==> set autofree flag to perform an async free */
	zm_enableFlag(state, ZM_STATEFLAG_AUTOFREE);

	return false;
}


int zm_freeTask(zm_VM *vm, zm_State *state)
{
	/* #FREE_TASK_KIND */
	if (zm_isSubTask(state)) {
		zm_fatalOn("zm_freeTask", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "FREET.S", vm,
		           "expected a ptask but found a subtask");
	}

	return zm_requestFreeState(vm, state, "zm_freeTask");
}

int zm_freeSubTask(zm_VM *vm, zm_State *state)
{
	/* #FREE_TASK_KIND */
	if (zm_isTask(state)) {
		zm_fatalOn("zm_freeSubTask", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "FREES.T", vm,
		           "expected a subtask but found a ptask");
	}

	return zm_requestFreeState(vm, state, "zm_freeSubTask");
}

int zm_freeState(zm_VM *vm, zm_State *state)
{
	return zm_requestFreeState(vm, state, "zm_freeState");
}

zm_Event* zm_newEvent(zm_trigger_cb trigger, zm_unbind_cb unbind, void *data)
{
	zm_Event *event = zm_alloc(zm_Event);

	event->flag = 0;
	event->bindlist = NULL;
	event->count = 0;
	event->unbind = unbind;
	event->trigger = trigger;
	event->data = data;

	return event;
}


void zm_freeEvent(zm_VM *vm, zm_Event *event)
{
	size_t i = 0;

	while(event->bindlist) {
		zm_unbindEvent(vm, event->bindlist->owner,
		               ZM_EVENT_UNBIND_REQUEST);

		#ifdef ZM_CHECK_CONSISTENCY
		if (i >= event->count) {
			zm_fatalDo(ZM_FATAL_UN, "FREEEV.CGT", vm,
			           "event counter bind list check fail");
		}
		#endif
		i++;
	}

	#ifdef ZM_CHECK_CONSISTENCY
	if ((i-1) != event->count) {
		zm_fatalDo(ZM_FATAL_UN, "FREEEV.CEQ", vm,
		           "event counter bind list check fail");
	}
	#endif

	/* event has no more binded task: unregister */
	if (event->unbind)
		event->unbind(vm, event->data, NULL, ZM_EVENT_UNBIND_REQUEST);

	zm_free(zm_Event, event);
}





/*---------------------------------------------------------------------------
 *  ACTIVE - DEACTIVE TASK/SUBTASK/EVENT
 *  -----------------------------------------------------------------------*/


static zm_yield_t zm_unraiseSSUB(zm_VM* vm, zm_State *s, bool allowunraise,
                                     const char* ref, const char *filename,
                                                                 int nline)
{

	if (!s->exception) {
		zm_State *current = zm_getCurrentState(vm);

		if (zm_getParent(current) == s) {
			zm_fatalOn(ref, filename, nline);
			zm_fatalDo(ZM_FATAL_UCODE, "ACTSUB.WP", vm,
			                 "try to resume a waiting subtask that "
			                 "is the previous element in "
			                 "execution-stack; use instead "
			                 "yield zmSUSPEND");
		} else {
			zm_fatalOn(ref, filename, nline);
			zm_fatalDo(ZM_FATAL_UCODE, "ACTSUB.WS", vm,
			           "try to resume a waiting subtask");
		}
	}

	/* #CONTINUE_EXCEPT*/
	if (s->exception->kind != ZM_EXCEPTION_CONTINUEREF) {
		zm_fatalOn(ref, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTSUB.E", vm,
		           "try to resume a waiting subtask "
		           "(with an unexpected exception");
	}

	if (!allowunraise) {
		zm_fatalOn(ref, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTSUB.C", vm,
		           "try to active a subtask with a "
		           "continue-exception (use instead "
		           "zmUNRAISE or zmSSUB)");
	}

	zm_unraise(vm, s, "zmSSUB", filename, nline);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}

/*
 * e.g. yield zmSUB
 *
 * when ... use zmUNRAISE
 */
zm_yield_t izmSUB(zm_VM* vm, zm_State *s, bool allowunraise,
                            const char *filename, int nline)
{
	const char *rn = (allowunraise) ? "zmSSUB" : "zmSUB";

	ZM_ASSERT_VMLOCK("ACTSUB.VLCK", rn, filename, nline);

	ZM_D("yield SUB(state: [ref %lx]) - parent: [ref %lx]\n", s, s->parent);

	if (zm_isTask(s)) {
		zm_fatalOn(rn, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTSUB.NS", vm,
		           "expected a subtask but found a task "
		           "(use instead: zmTO/zmLAST)");

	}

	if (zm_hasFlag(s, ZM_STATEFLAG_IMPLOSIONLOCK)) {
		zm_fatalOn(rn, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTSUB.LI", vm,
		           "try to active a closed subtask");
	}

	zm_canBeContextStackPush(vm,  s, rn, filename, nline);

	if (zm_hasFlag(s, ZM_STATEFLAG_WAITING)) {
		return zm_unraiseSSUB(vm, s, allowunraise, rn,
		                      filename, nline);
	}

	/*ZM_STATEFLAG_EVENTLOCKED*/

	zm_comeback(s, zm_getCurrentState(vm));

	zm_resumeStateBy(vm, s, false, rn, filename, nline);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}

/*
 *
 */
zm_yield_t izm_resume(const char *fname, zm_VM* vm, zm_State *s, int iter,
                                  int up, const char *filename, int nline)
{
	if (zm_hasFlag(s, ZM_STATEFLAG_RUN)) {
		zm_fatalOn(fname, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTTASK.R", vm,
		           "try to active a just active task");
	}


	if (zm_isSubTask(s)) {
		zm_fatalOn(fname, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTTASK.NP", vm,
		           "expected a task but found a subtask "
		           "(use instead: zmSUB)");
	}

	if (zm_hasFlag(s, ZM_STATEFLAG_WAITING)) {
		zm_fatalOn(fname, filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "ACTTASK.WS", vm,
		           "try to resume a waiting task");
	}


	if (iter) {
		/*  [ON_RESUME_SWITCH]*/
		if (s->on.iter) {
			s->on.resume = s->on.iter;
		}
	}

	ZM_D("activeTask (%s): iter = %d", fname, iter);

	zm_resumeStateBy(vm, s, up, fname, filename, nline);

	if (iter)
		return ZM_TASK_SUSPEND;

	return ZM_TASK_TERM;
}


/*
 * yield to event (listen event)
 */
zm_yield_t izmEVENT(zm_VM* vm, zm_Event *e, const char *filename, int nline)
{
	zm_State *s = zm_getCurrentState(vm);

	ZM_ASSERT_VMLOCK("LISTEV.VLCK", "zmEVENT", filename, nline);

	ZM_D("zm_listenEvent - event bind for state: [ref %lx]", s);

	if (s->flag & ZM_STATEFLAG_EVENTLOCKED) {
		zm_fatalOn("zmEVENT", filename, nline);
		zm_fatalDo(ZM_FATAL_UCODE, "LISTEV.1", vm,
		           "this state is just associated to an event");
	}

	zm_bindEvent(e, s);

	return ZM_TASK_BUSY_WAITING_EVENT;
}





/*---------------------------------------------------------------------------
 *  VM COSTRUCTOR
 *  -----------------------------------------------------------------------*/

zm_VM* zm_newVM(const char *name)
{
	zm_VM* vm;

	vm = zm_alloc(zm_VM);

	vm->data = NULL;

	vm->plock = false;

	vm->prepost = NULL;
	/** ptasks contain a pointer to a ptask of the vm or NULL when empty */
	/** all ptask are connected througth siblings so this pointer allow*/
	/** to access all the task (and relative subtask) of the vm*/
	vm->ptasks = NULL;

	vm->nptask = 0;
	vm->nworker = 0;

	zm_mhwInit(vm);

	vm->workercursor = NULL;
	vm->currentsession.state = NULL;
	vm->currentsession.worker = NULL;
	vm->currentsession.fixedworker = false;
	vm->currentsession.suspendop = 0;


	vm->vname = name;

	return vm;
}



int zm_closeVM(zm_VM* vm)
{
	zm_State *state = vm->ptasks;
	#ifdef ZM_CHECK_CONSISTENCY
	size_t n = vm->nptask;
	#endif

	if (!state)
		return true;

	if (vm->plock) {
		/* can be replaced with a ZM_ASSERT_VMUNLOCK TODO */
		zm_fatalOn("zm_closeVM", NULL, 0);
		zm_fatalDo(ZM_FATAL_SYNC, "CLSVM.LCK", vm,
		           "cannot invoke a vm close during task "
		           "execution (operation permitted only "
		           "outside task definition)");
	}

	do {
		#ifdef ZM_CHECK_CONSISTENCY
		if (!state->siblings.next) {
			zm_fatalDo(ZM_FATAL_UN, "CLSVM.SN", vm,
			           "zm_closeVM: unexpected error: "
			           "null ref in siblings ring");
		}
		n--;
		#endif

		zm_abortTask(vm, state, "zm_closeVM");
		state = state->siblings.next;
	} while (state != vm->ptasks);

	#ifdef ZM_CHECK_CONSISTENCY
	if (n != 0) {
		zm_fatalDo(ZM_FATAL_UN, "CLSVM.SC", vm,
		           "zm_closeVM: unexpected error: "
		           "siblings count doesn't match");
	}
	#endif


	return false;
}



void zm_freeVM(zm_VM* vm)
{
	int i;
	if (vm->ptasks) {
		zm_fatalOn("zm_freeVM", NULL, 0);
		zm_fatalDo(ZM_FATAL_ERROR, "FREEVM.TP", vm,
		           "cannot free vm if all task hasn't been "
		           "free (use zm_closeVM and wait until "
		           "zm_go return ZM_RUN_IDLE)");
	}


	for (i = 0; i < vm->mhw.len; i++) {
		zm_Worker *w = vm->mhw.hlist[i];
		if (w) {
			zm_freeWorker(vm, w);
		}
	}

	zm_mhwFree(vm);

	zm_free(zm_VM, vm);
}

static void zm_checkCloseYield(zm_VM *vm, zm_State *state, zm_Yield result,
                                            const char *et, const char *ed)
{

	 /*
	  * - task on TERM:      yield to normal-vmstate [FAIL]
	  * - task not in TERM:  yield to ZM_TERM [FAIL]
	  */
	if (state->on.resume == ZM_TERM) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, et, vm,
		           "in ZM_TERM only yield zmEND is "
		           "permitted");
	} else if (zm_isTermState(result.resume)) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, ed, vm,
		           "an active task cannot yield "
		           "directly to ZM_TERM use instead: "
		           "zmTERM");
	}

}

static void zm_checkInnerYield(zm_VM *vm, zm_State *state, zm_Yield result)
{
	if (result.resume == 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YINN.0", vm,
		           "yield to vmstate 0");
	}

	if (result.c4tch != 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YINN.C", vm,
		           "catch in yield can be set only with "
		           "zmSUB(state)");
	}

	if (result.iter != 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YINN.I", vm,
		           "iter in yield can be set only with "
		           "zmSUB(state)");
	}

	zm_checkCloseYield(vm, state, result, "YINN.T", "YINN.D");
}



static void zm_checkParentYield(zm_VM *vm, zm_State *state, zm_Yield result,
                                                                int suspend)
{
	if ((!suspend) && zm_isTask(state)) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YSUSPEND.CALL", vm,
		           "yield zmCALLER can be invoked only "
		           "in subtask (ptask have no parent)");
	}

	/* only resume can be set in yield parent */

	if (result.resume == 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YSUSPEND.RESUME", vm,
		           "resume point must be set in "
		           "yield zmSUSPEND");
	}

	/* CHECK_OR_NOT check or reset ?? */
	if (result.c4tch != 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YSUSPEND.CATCH", vm,
		           "invalid %s in yield zmSUSPEND",
		           zm_hasFlag(state, ZM_STATEFLAG_CATCH) ?
		           "zmCATCH(...)" :"zmRESET(...)");
	}

	if (result.iter != 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YSUSPEND.ITER", vm,
		           "invalid zmNEXT(iter) in "
		           "yield zmSUSPEND");
	}
}

/*---------------------------------------------------------------------------
 *  PROCESS STATE - RUN TASK
 *  -----------------------------------------------------------------------*/

static void zm_processUnexpected(zm_VM *vm, zm_State *state, zm_Yield result)
{
	const char *sop = zm_getMachineOpName(state, false);

	switch(state->vmop) {

	case ZM_MACHINEOP_RUN:
	case ZM_MACHINEOP_END_TASK:
		zm_fatalDo(ZM_FATAL_UNP, "WCMOP.U", vm,
		           "unknow combination of machine result "
		           "cmd = %s and vmop = %s",
		           zm_getYieldCommandName(result.cmd),
		           sop);
		break;

	case ZM_MACHINEOP_CLOSE_TASK:
		if (zm_hasntFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
			zm_fatalDo(ZM_FATAL_UNP, "WCMOP.NFLI", vm,
			           "not found lock and implode flag in "
			           "state with vmop = %s", sop);
		}

		if (zm_isntTermState(state->on.resume)) {
			zm_fatalDo(ZM_FATAL_UNP, "WCMOP.TR", vm,
			           "not found term state in state with "
			           "vmop = %s", sop);
		}

		zm_checkCloseYield(vm, state, result, "WCMOP.T", "WCMOP.D");

		break;


	case ZM_MACHINEOP_NO_MORE_TO_DO:
		zm_fatalDo(ZM_FATAL_UNP, "WCMOP.NMTD", vm,
		           "vmop = %s after dt", sop);
		break;


	default:
		zm_fatalDo(ZM_FATAL_UNP, "WCMOP.UOP", vm,
		           "unknow vmop = %d", state->vmop);
	}
}



static void zm_freeUnlockException(zm_VM *vm, zm_Exception *e)
{
	if (!e->elock) {
		/* no lock => free exception */
		zm_free(zm_Exception, e);
		return;
	}

	/*
	 * A simpler way is check state->exception == NULL and
	 * free the exception inside izmFreeException. But if
	 * an exception will be raised inside a catch this control
	 * isn't enougth. elock allow to manage this kind of
	 * situations.
	 */

	ZM_D("process_state[30] - check exception...");
	switch(e->kind) {

	case ZM_EXCEPTION_CONTINUE:
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YNOFREEC.1", vm,
		           "continue-exception not free after processing "
		           "catch state (see zmFreeException)");

	case ZM_EXCEPTION_ERROR:
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YNOFREEE.1", vm,
		           "exception-error not free after processing "
		           "catch state (see zmFreeException)");

	case ZM_EXCEPTION_STARTIMPLOSION:
	case ZM_EXCEPTION_CONTINUEREF:
	default:
		zm_fatalDo(ZM_FATAL_UNP, "YNOFREEC.EUN", vm,
		           "exception->kind = %s",
		           zm_getExceptionKindName(e));
	}

	ZM_D("process_state[30] - check exception...ok");
}


#ifdef ZM_BYTE_ORDER_RUNTIME
static zm_Yield zm_r2Y(zm_yield_t n)
{
	zm_Yield y;

	y.resume = n & 0xFF;
	y.c4tch = (n >> 8) & 0xFF;
	y.iter = (n >> 16) & 0xFF;
	y.cmd = (n >> 24) & 0xFF;

	return y;
}
#else
static zm_Yield zm_r2Y(zm_yield_t n)
{
	union {
		zm_yield_t r;
		zm_Yield y;
	} tmp;
	tmp.r = n;
	return tmp.y;
}
#endif


static zm_Yield zm_runMachine(zm_VM *vm, zm_Worker *worker, zm_State *state)
{
	zm_yield_t n;

	vm->plock = true;

	n = (worker->machine->fun)(vm, state->on.resume, state->data);

	vm->plock = false;

	return zm_r2Y(n);

}

static int zm_processYield(zm_VM *vm, zm_Worker *worker, zm_State *state,
                                                         zm_Yield result)
{
	int cmd =  ZM_B4(result.cmd);
	int resop = state->vmop | cmd;

	ZM_D("process_state[30] - eval machine dt");

	switch( resop ) {

	/**
	 *  --------------------------------------------------------
	 *  context: ZM_MACHINEOP_RUN:
	 *  --------------------------------------------------------
	 */
	case ZM_MACHINEOP_RUN | ZM_TASK_CONTINUE:
	case ZM_MACHINEOP_RUN | ZM_TASK_VMSTOP:
	case ZM_MACHINEOP_CLOSE_TASK | ZM_TASK_CONTINUE:
	case ZM_MACHINEOP_CLOSE_TASK | ZM_TASK_VMSTOP:


		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_CONTINUE");

		zm_checkInnerYield(vm, state, result);

		state->on.resume = result.resume;

		if (cmd == ZM_TASK_VMSTOP) {
			/* after restart continue on selected vmstate */
			return ZM_PROCESS_VMBREAK;
		}
		break;

	case ZM_MACHINEOP_RUN | ZM_TASK_RAISE_CONTINUE_EXCEPTION: {
		zm_Exception *e;
		zm_State *lastbeforecatch;

		/* #CONTINUE_EXCEPT*/

		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_RAISE_CONTINUE");

		/** get state before catch */
		lastbeforecatch = zm_getLastBeforeContinueCatch(vm, state,
		                                         state->exception);

		/* move exception in state that catch (see zmCatch)
		 * The continue-exception is stored only in the catch state
		 * and a new innercontinue (with kind=ref) in lastbeforecatch*/

		lastbeforecatch->parent->comeback->exception = state->exception;
		state->exception = NULL;

		/** create a continue-exception-ref to allow unraise  */
		e = zm_newInnerException(ZM_EXCEPTION_CONTINUEREF);

		e->raisestate = state;
		/** continue-exception beforecatch*/
		e->beforecatch = lastbeforecatch;

		/* this work also when lastbeforecatch == state because
		 * state->exception reference are just been set = NULL */
		lastbeforecatch->exception = e;


		/** suspend with waiting = true (ZM_STATEFLAG_WAITING) */
		zm_suspendCurrentState(vm, result, true);

		/** resume parent*/
		zm_resumeParent(vm, lastbeforecatch, false);

		return ZM_PROCESS_STATEUNLINKED;
	}


	case ZM_MACHINEOP_RUN | ZM_TASK_RAISE_ERROR_EXCEPTION: {
		zm_Exception *e = state->exception;

		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_RAISE_ERROR");

		/* remove reference from raise state*/
		state->exception = NULL;

		/* zmRESET */
		if (result.resume) {
			if (!result.c4tch)
				result.c4tch = result.resume;
		}

		zm_suspendCurrentState(vm, result, true);

		zm_lockAndImplodeByException(vm, state, e);

		return ZM_PROCESS_STATEUNLINKED;
	}


	/** distructor of the task  */
	case ZM_MACHINEOP_RUN | ZM_TASK_TERM:
		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_TERM");
		/* CHECK_OR_NOT */
		/*if (result.c4tch != 0) {
			zm_fatal ...
		}

		if (result.iter != 0) {
			zm_fatal ...
		}*/

		zm_suspendCurrentState(vm, result, true);

		zm_lockAndImplodeBy(vm, state, ZM_IMPLODEBY_CUR, "yield zmTERM",
		                    state->codeframe.filename,
		                    state->codeframe.nline);

		return ZM_PROCESS_STATEUNLINKED;


	case ZM_MACHINEOP_CLOSE_TASK | ZM_TASK_END:
		/*  CHECK_OR_NOT	*/

		if (zm_hasntFlag(state, ZM_STATEFLAG_IMPLOSIONLOCK)) {
			zm_fatalOn(NULL, NULL, 0);
			zm_fatalDo(ZM_FATAL_YIELD, "YEND.NVT", vm,
			           "yield to zmEND is permitted "
			           "only in ZM_TERM vmstate");
		}

		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_END");

		worker->machine->count--;

		state->vmop = ZM_MACHINEOP_END_TASK;
		break;


	/** Task Suspend - e.g. yield TO(foo) */
	case ZM_MACHINEOP_RUN | ZM_TASK_SUSPEND:
		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_SUSPEND");

		if (zm_isTask(state)) {
			zm_suspendCurrentState(vm, result, false);
			return ZM_PROCESS_STATEUNLINKED;
		} /* else -> suspend and resume parent */

	/** Task suspend (iter) - e.g. yield zmCALLER */
	case ZM_MACHINEOP_RUN | ZM_TASK_SUSPEND_AND_RESUME_CALLER:
		ZM_D("ZM_MACHINEOP_RUN | TASK_SUSPEND_AND_RES_CALLER");

		/* CHECK_OR_NOT */
		zm_checkParentYield(vm, state, result,(cmd == ZM_TASK_SUSPEND));

		/* suspend current state (child)*/
		zm_suspendCurrentState(vm, result, false);

		/* resume parent (iter mode)*/
		zm_resumeParent(vm, state, true);

		return ZM_PROCESS_STATEUNLINKED;

	/** Task Suspend - e.g. yield SUB(foo) */
	case ZM_MACHINEOP_RUN | ZM_TASK_SUSPEND_WAITING_SUBTASK:
		ZM_D("ZM_MACHINEOP_RUN | ZM_TASK_SUSPEND_WAIT_SUB");
		/* suspend with waiting = true (ZM_STATEFLAG_WAITING) */
		zm_suspendCurrentState(vm, result, true);

		return ZM_PROCESS_STATEUNLINKED;


	/** Task suspend waiting event - e.g. yield EVENT(...)*/
	case ZM_MACHINEOP_RUN | ZM_TASK_BUSY_WAITING_EVENT: {
		/* zmEVENT has just:
		 * - set flag ZM_STATEFLAG_EVENTLOCKED #EVB_FLAG
		 * - set state->next to an eventbinder instance pointer
		 *   #EVENT_BIND
		 */
		zm_EventBinder* evb = (zm_EventBinder*)state->next;

		/* temporary recover state->next to perform
		 * suspendCurrentState */
		state->next = (zm_State*)evb->statenext;

		ZM_D("ZM_MACHINEOP_RUN | TASK_BUSY_WAITING_EVENT");

		/* Temporary disable event flag to allow debug print in
		   suspend current */
		zm_disableFlag(state, ZM_STATEFLAG_EVENTLOCKED);

		/* suspend with waiting = true (ZM_STATEFLAG_WAITING) */
		zm_suspendCurrentState(vm, result, true);

		zm_enableFlag(state, ZM_STATEFLAG_EVENTLOCKED);

		/* save worker pointer in (suspend put worker in state->next */
		/* to allow resume operation) to be recover after unbind */
		evb->statenext = state->next;

		/* replace again next state with evb */
		state->next = (zm_State*)evb;

		return ZM_PROCESS_STATEUNLINKED;
	}





	/**
	 *  --------------------------------------------------------
	 * context: [unknow]
	 *
	 * Unexpected combination: report error
	 *  --------------------------------------------------------
	 */
	default:
		zm_processUnexpected(vm, state, result);
	}

	return 0;
}


static zm_Yield zm_runTask(zm_VM *vm, zm_Worker *worker, zm_State *state)
{
	zm_Exception *checkexception = NULL;
	zm_Yield y;


	ZM_D("runState - begin");

	/* check for exception and catch */
	if (zm_hasFlag(state, ZM_STATEFLAG_CATCH)) {
		zm_disableFlag(state, ZM_STATEFLAG_CATCH);

		if (state->exception) {
			checkexception = state->exception;

			/* resume on catch #EXCEPT_WORKFLOW */
			if (zm_isntTermState(state->on.resume))
				state->on.resume = state->on.c4tch;
		}
	}


	/* in run mode a resume point must always have to been set */
	if (state->on.resume == 0) {
		zm_fatalOn(NULL, NULL, 0);
		zm_fatalDo(ZM_FATAL_YIELD, "YRES.0", vm,
		           "resume vmstate is set 0 (possible cause: "
		           "last zmyield hasn't define the "
		           "proper resume point");
	}

	ZM_D("runState: (resume = %d) machine: %s", state->on.resume,
	     zm_getCurrentMachineName(vm));

	/* execute a machine step */
	y = zm_runMachine(vm, worker, state);

	ZM_D("runState: resume: %d iter: %d catch: %d cmd: %d",
	     y.resume, y.iter, y.c4tch, y.cmd);

	/* free exception if is unlock otherwise go fatal */
	if (checkexception)
		zm_freeUnlockException(vm, checkexception);

	return y;
}






static int zm_processState(zm_VM *vm, zm_Worker *worker, zm_State *state)
{
	ZM_D("process_state[0] - state = [ref %lx]  vmop = %d", state,
	     state->vmop);

	switch(state->vmop) {
	case ZM_MACHINEOP_RUN:
	case ZM_MACHINEOP_CLOSE_TASK: {
		/* RUN: Excute a step of machine */
		zm_Yield y = zm_runTask(vm, worker, state);

		return zm_processYield(vm, worker, state, y);
	}

	case ZM_MACHINEOP_END_TASK:
		/* Remove the state from list (must be invoked in
		 * ZM_TERM only when all user-resource as been free)
		 */
		ZM_D("ZM_MACHINEOP_END_TASK:");

		if (zm_isSubTask(state)) {
			/* resume parent (end mode) done before
			 * unlinkCurrentState to avoid unuseful
			 * unlink/add worker when state and parent
			 * have same worker */
			if (zm_haveComeback(state)) {
				zm_resumeParent(vm, state, false);
			}
		}

		zm_unlinkCurrentState(vm);

		state->vmop = ZM_MACHINEOP_NO_MORE_TO_DO;

		ZM_D("CLOSE TASK: remove state from siblings ...");
		zm_removeStateFromSiblings(vm, state);

		if (zm_isSubTask(state)) {
			zm_nfree(zm_State*, zm_deep(state),
			         state->parent->stack);

			state->parent->comeback = NULL;
			state->parent->stack = NULL;

			zm_free(zm_Parent, state->parent);

			/* NOTE: state->parent don't have to be set = NULL
			 * because this will change the nature of the task
			 * (subtask become ptask) and this is a problem in
			 * explicit free task operation (see #FREE_TASK_KIND)
			 */
		}

		if (zm_haveException(state, ZM_EXCEPTION_CONTINUEREF)) {
			zm_resetInnerException(state, NULL);
		} else if (state->exception) {
			/* If there is alredy an exception with
			 * kind != continueref ... something has going wrong */
			zm_fatalDo(ZM_FATAL_UNP, "PPS.EEN", vm,
			           "exception still present in "
			           "ZM_MACHINEOP_END_TASK");
		}

		if (zm_hasFlag(state, ZM_STATEFLAG_AUTOFREE)) {
			ZM_D("CLOSE TASK: free state");
			zm_free(zm_State, state);
		}

		/** remove state from vm (no more executed)*/
		return ZM_PROCESS_STATEUNLINKED;


	case ZM_MACHINEOP_UNLINK_TASK_AND_IMPLODE: {
		/* this is a special for lock and implode
		   #ASYNC_SERIALIZATION [step 3]*/
		zm_State *imstart;
		zm_Yield y;

		y.resume = state->on.resume;
		y.iter = state->on.iter;
		y.c4tch = state->on.c4tch;

		ZM_D("ZM_MACHINEOP_UNLINK_TASK_AND_IMPLODE");

		imstart = zm_popAsyncImplosionStart(state);

		state->vmop = ZM_MACHINEOP_CLOSE_TASK;

		/* All implosion task, except the imstart, must be in
		 * waiting subtask (see #IMPLODE_WAITING_CHAIN)
		 * suspend with waiting = true (ZM_STATEFLAG_WAITING) */
		zm_suspendCurrentState(vm, y, true);

		ZM_D("star implosion from: %lx", imstart);

		zm_resumeState(vm, imstart, false);

		return ZM_PROCESS_STATEUNLINKED;
	}

	case ZM_MACHINEOP_NO_MORE_TO_DO:
		zm_fatalDo(ZM_FATAL_UNP, "PPS.NMTD", vm,
		           "unexpected vmop in processState"
		           "(vmop = ZM_MACHINEOP_NO_MORE_TO_DO)");
		break;

	default:
		zm_fatalDo(ZM_FATAL_UNP, "PPS.UVMOP", vm,
		           "unexpected vmop in processState (vmop = %d)",
		           state->vmop);
	}

	ZM_D("process_state[10] - dt = false - process state end");
	return 0;
}




void zm_setProcessStateCallback(zm_VM *vm, zm_process_cb p)
{
	vm->prepost = p;
}









/*---------------------------------------------------------------------------
 *  VM GO
 *  -----------------------------------------------------------------------*/



static int zm_state_go(zm_VM* vm, zm_Worker *worker, zm_State *state)
{
	int processresult;

	ZM_D("zm_state_go - state: [ref %lx]", state);

	vm->currentsession.state = state;
	vm->currentsession.worker = worker;
	vm->currentsession.suspendop = 0;

	if (vm->prepost) {
		vm->prepost(vm, worker->machine, state, 0);
	}

	ZM_D("zm_state_go ~~~~ resume:%d", state->on.resume);
	processresult = zm_processState(vm, worker, state);

	if (vm->prepost) {
		vm->prepost(vm, worker->machine, state, 1);
	}

	ZM_D("zm_state_go - process state return %d", processresult);


	/* update states and worker cursors:
	 * when an unlinkcurrentstate is called during a session
	 * worker->states cursor is just updated otherwise cursor
	 * must be updated
	 */
	if (processresult == ZM_PROCESS_STATEUNLINKED) {
		if ((worker->nstate == 0) && (vm->currentsession.fixedworker))
			return ZM_RUN_IDLE;
	} else {
		zm_scursMove(worker);
	}

	if (processresult == ZM_PROCESS_VMBREAK)
		return ZM_RUN_VMBREAK | ZM_RUN_AGAIN;


	return ZM_RUN_INNERREF_CONTINUE;
}




int zm_mgo(zm_VM* vm, zm_Machine* machine, unsigned int ncycle)
{
	zm_Worker* worker = vm->workercursor;
	zm_State* state;
	int onemachine = (machine != NULL);
	int r;

	ZM_D("GO - init: vm = %d - machine = %lx", vm, machine);

	if (machine) {
		worker = zm_mhwGet(vm, machine);
	} else {
		if (worker == NULL)
			return ZM_RUN_IDLE;
	}

	vm->currentsession.worker = worker;
	vm->currentsession.fixedworker = onemachine;

	while(ncycle > 0) {
		ZM_D("GO: ~********** STEP #%d **********~", ncycle);

		if (!onemachine) {
			/* get current cursor worker #NAV_WORKERS */
			worker = vm->workercursor;

			if (!worker) {
				/* no worker in ring -> IDLE */
				ZM_D("GO: no more worker -> IDLE");
				return ZM_RUN_IDLE;
			}
		}

		ZM_D("GO: checkpoint 2");

		/* check if there a least one state */
		if (worker->nstate == 0) {
			/** no more state in this worker ****/
			ZM_D("zm_go: no more state in worker -> IDLE");
			return ZM_RUN_IDLE;
		} else if (worker->nstate < 0) {
			zm_fatalDo(ZM_FATAL_UN, "WGO.WNS", vm,
			           "zm_go: inconsistency error: "
			           "worker->nstate = %d", worker->nstate);
		}

		ZM_D("GO: checkpoint 3");


		/* get next state (and in some condition next worker) */
		if (worker->states.current == NULL) {
			ZM_D("GO: 3.a %lx", worker);
			/* rewind worker states */
			zm_rewindWorkerStates(vm, worker);

			ZM_D("GO: 3.b");
			if ((!onemachine) && (vm->nworker > 1)) {
				/* get next worker in ring #NAV_WORKERS */
				worker = zm_nextWorker(vm);
				ZM_D("GO: ~Next worker is: %s\n",
				     worker->machine->name);
				continue;
			}
		}

		ZM_D("GO: checkpoint 4");
		/**** process (exec) state ****/

		state = worker->states.current;

		ZM_D("GO: process state = %lx [%s%s]", state,
		     (onemachine) ? " const " : "",
		     worker->machine->name);

		r = zm_state_go(vm, worker, state);

		if (r != ZM_RUN_INNERREF_CONTINUE) {
			return r;
		}

		ncycle -= worker->cyclestep;
		ZM_D("GO: step end\n");

		#if ZM_DEBUG_LEVEL >= 4
		zm_printVM(NULL, vm);
		#endif
	}

	ZM_D("GO[end]: *** ncylce = %d\n", ncycle);
	return ZM_RUN_AGAIN;
}




int zm_go(zm_VM* vm, unsigned int ncycle)
{
	return zm_mgo(vm, NULL, ncycle);
}






