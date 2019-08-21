/*
 * Copyright (c) 2015-2019, Fabio Sassi <fabio dot s81 at gmail dot com>
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <zm.h>


/*
 * SECTION BASIC_TOOL
 * SECTION MT
 * SECTION REPORT
 * SECTION CORE
 */


size_t zmg_mcounter = 0;


typedef struct {
	short by;
	short busycheck;

	int fromdeep;
	int todeep;
	int count;

	zm_StateQueue *deepstack;
	zm_StateQueue *lockstack;
	zm_StateQueue *econtinue;

	struct {
		int count;
		zm_Exception *exception;
		zm_State *state;
	} justlock;

	zm_State *chaintail;
	zm_State *running;

	const char *refname;
	const char *filename;
	int nline;
} zm_LockAndImplode;


typedef enum {
	/* unexpected error */
	ZM_FATAL_U1,
	/* unexpected second error (in print dump) */
	ZM_FATAL_U2,
	/* unexpected error in process task */
	ZM_FATAL_UP,
	/* user code error (generic) */
	ZM_FATAL_GCODE,
	/* user code error (inside task) */
	ZM_FATAL_TCODE,
	/* user code error (using yield) */
	ZM_FATAL_YCODE
} zm_fatal_t;


static struct {
	zm_VM *vm;
	zm_Exception *exception;

	struct {
		const char *reference;
		const char *filename;
		int nline;
	} ucode;

	int first;

	struct {
		zm_fatal_cb fatalcb;
		void *data;
	} at;

} zmg_err = { NULL, NULL, {NULL, NULL, 0,}, true, {NULL, NULL}};


static struct {
	zm_tlock_cb lockcb;
	void *data;
} zmg_mutex = {NULL, NULL};


/* indentation-less special prefix char */
#define ZM_ILS "<"


#define ZM_ELOCK_ON 1
#define ZM_ELOCK_OFF 2
#define ZM_ELOCK_REUSE 3

#define zm_enableFlag(s, FLAG)   (s)->flag |= FLAG
#define zm_disableFlag(s, FLAG)   (s)->flag &= (0xFFFF ^ FLAG)

#define zm_getCurrentState(vm) ((vm)->session.state)
#define zm_getCurrentWorker(vm) ((vm)->session.worker)
#define zm_getCurrentMachine(vm) ((vm)->session.worker->machine)
#define zm_getCurrentMachineName(vm) ((vm)->session.worker->machine->name)
#define zm_workerName(w) ((w)->machine->name)


#if ZM_DEBUG_LEVEL >= 1
	#define ZM_D zm_log
#else
	#define ZM_D if (0) zm_log
#endif

#define ZM_ASSERT_VMLOCK(errcode, refname, filename, nline)                   \
   if (!vm->plock) {                                                          \
       ZM_D("vm->plock %d", vm->plock);                                       \
       zm_fatalInitAt(vm, refname, filename, nline);                          \
       zm_fatalDo(ZM_FATAL_GCODE, errcode, "operation permitted only "        \
                  "inside a task"); }



/* ----------------------------------------------------------------------------
 *  DEBUG PRINT UTILITY                                    (SECTION BASIC_TOOL)
 * --------------------------------------------------------------------------*/

static void zm_log(const char *fmt, ...)
{
	FILE *out = stdout;
	va_list args;

	fprintf(out, "zmDEBUG - ");

	va_start(args, fmt);
	vfprintf(out, fmt, args);
	va_end(args);

	fprintf(out, "\n");

	fflush(out);
}




/* ----------------------------------------------------------------------------
 *  MEMORY UTILITY                                         (SECTION BASIC_TOOL)
 * --------------------------------------------------------------------------*/


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



/* ----------------------------------------------------------------------------
 *  STATE QUEUE                                            (SECTION BASIC_TOOL)
 * --------------------------------------------------------------------------*/


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
	assert(q->first == NULL);
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

	first = zm_queuePopStateList0(queue);

	if (!first)
		return NULL;

	if (data)
		*data = first->data;

	result = first->state;

	zm_free(zm_StateList, first);

	return result;
}



static zm_State* zm_queueFindPop(zm_StateQueue *q, zm_State *s,
                      int (*match)(zm_State* s, zm_State* cur))
{
	zm_StateList *sl = q->first;
	zm_StateList *prev = NULL;

	if (!sl)
		return NULL;

	do {
		zm_State *c = sl->state;


		if ((match) ? match(s, c) : (c == s)) {
			zm_State *found = sl->state;

			if (prev)
				prev->next = sl->next;
			else
				q->first = sl->next;

			zm_free(zm_StateList, sl);

			return found;
		}

		prev = sl;
		sl = sl->next;
	} while (sl);


	return NULL;
}


/* ----------------------------------------------------------------------------
 *  PRINT FUNCTION                                         (SECTION BASIC_TOOL)
 * --------------------------------------------------------------------------*/


void zm_initPrint(zm_Print *p, FILE *stream, int indent)
{
	p->file = stream;
	p->indent = indent;
}


void zm_setIndent(zm_Print *out, int indent)
{
	out->indent = indent;
}


void zm_addIndent(zm_Print *out, int indent)
{
	out->indent += indent;
}


/*
 * Indent and print. If fmt starts with ZM_ILS the
 * indentation is ignored.
 */
void zm_vprint(zm_Print *out, const char *fmt, va_list args)
{
	if (fmt[0] != ZM_ILS[0]) {
		#if 0
		int i;

		for (i = 0; i < out->indent; i++)
			fprintf(out->file, " ");
		#else
		if (out->indent > 0)
			fprintf(out->file, "%.*s",
				((out->indent > 74) ? 74 : out->indent),
				"                                     "
			        "                                     ");
		#endif
	} else {
		fmt++;
	}

	vfprintf(out->file, fmt, args);

	fflush(out->file);
}

/*
 * Indent and print (see zm_vprint)
 */
void zm_print(zm_Print *out, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	zm_vprint(out, fmt, args);
	va_end(args);
}



/* ----------------------------------------------------------------------------
 *  MULTI-THREAD SUPPORT                                           (SECTION MT)
 * --------------------------------------------------------------------------*/


static void zm_lockOn(FILE *f)
{
	if (!zmg_mutex.lockcb)
		return;

	zmg_mutex.lockcb(f, zmg_mutex.data, true);
}


static void zm_lockOff(FILE *f)
{
	if (!zmg_mutex.lockcb)
		return;

	zmg_mutex.lockcb(f, zmg_mutex.data, false);
}


void zm_enableMT(zm_tlock_cb cb, void* data)
{
	zmg_mutex.lockcb = cb;
	zmg_mutex.data = data;
}



/* ----------------------------------------------------------------------------
 *  ERROR  REPORTING                                           (SECTION REPORT)
 * --------------------------------------------------------------------------*/


static const char* zm_getModeName(zm_State *s, int compact);


void zm_atFatal(zm_fatal_cb cb, void *data)
{
	zmg_err.at.fatalcb = cb;
	zmg_err.at.data = data;
}


static void zm_fatalLock()
{
	zm_lockOn(stderr);
}

static void zm_fatalInit(zm_VM *vm, const char *ref)
{
	zm_fatalLock();
	zmg_err.vm = vm;
	zmg_err.ucode.reference = ref;
	zmg_err.ucode.filename = NULL;
	zmg_err.ucode.nline = -1;
}


static void zm_fatalInitAt(zm_VM *vm, const char *ref, const char *fn, int nl)
{
	zm_fatalLock();
	zmg_err.vm = vm;
	zmg_err.ucode.reference = ref;
	zmg_err.ucode.filename = fn;
	zmg_err.ucode.nline = nl;
}

static void zm_fatalInitByLI(zm_VM *vm, zm_LockAndImplode *li)
{
	zm_fatalInitAt(vm, li->refname, li->filename, li->nline);
}


static void zm_fatalException(zm_Exception *e)
{
	zmg_err.exception = e;
}


static const char* zm_fatalKind(zm_fatal_t kind)
{
	switch(kind) {
	case ZM_FATAL_U2:
		if (!zmg_err.first)
			return "Unexpected (while reporting another error)";

	case ZM_FATAL_U1:
		return "Unexpected";

	case ZM_FATAL_UP:
		return "Unexpected during process task";

	case ZM_FATAL_GCODE:
		return "Error";

	case ZM_FATAL_YCODE:
		return "Error in yield/raise";

	case ZM_FATAL_TCODE:
		return "Error in task";

	default:
		return "??UNKNOW FATAL-KIND??";
	}
}


static void zm_fatalPrintWhere(zm_Print *out, zm_fatal_t kind)
{
	zm_VM *vm = zmg_err.vm;
	zm_State *state = (vm) ? zm_getCurrentState(vm) : NULL;

	if ((!zmg_err.ucode.reference) && (!zmg_err.ucode.filename) && (!state))
		return;

	zm_print(out, "Error occured at:");

	if (zmg_err.ucode.reference)
		zm_print(out, ZM_ILS" %s\n", zmg_err.ucode.reference);
	else
		zm_print(out, ZM_ILS"\n");

	zm_addIndent(out, 4);

	if (zmg_err.ucode.filename) {
		zm_print(out, "file: %s\n", zmg_err.ucode.filename);
		zm_print(out, "line: %d\n", zmg_err.ucode.nline);
	}

	if ((state) && (kind != ZM_FATAL_U1) && (kind != ZM_FATAL_U2)) {
		zm_print(out, "task: %s (kind=%s)\n",
		         zm_getCurrentMachineName(vm),
		         zm_isTask(state) ? "ptask" : "subtask");

		zm_addIndent(out, 4);

		if (state->codeframe.filename) {
			zm_print(out, "last yield at: %s line %d\n",
			         state->codeframe.filename,
			         state->codeframe.nline);
		}

		zm_print(out, "resume=%d iter=%d catch=%d\n",
		         state->on.resume, state->on.iter, state->on.c4tch);

		zm_print(out, "pmode=%s\n", zm_getModeName(state, false));

		zm_addIndent(out, -4);
	}

	zm_addIndent(out, -4);
	zm_print(out, "\n\n");
}


static void zm_fatalPrintError(zm_fatal_t kind, const char *ecode,
                                    const char *fmt, va_list args)
{
	zm_Print out;

	zm_initPrint(&out, stderr, 0);

	zm_print(&out, "\n[ZM ver %s] ZM FATAL ERROR: (%s)\n  %s: ",
	         ZM_VERSION, ecode,
	         zm_fatalKind(kind)
	         );

	zm_vprint(&out, fmt, args);

	zm_print(&out, "\n\n");

	zm_fatalPrintWhere(&out, kind);

	if (zmg_err.exception) {
		zm_addIndent(&out, 4);
		zm_printException(&out, zmg_err.exception, true);
		zm_addIndent(&out, -4);
	}
}


static int zm_fatalPrintDump(zm_fatal_t kind)
{
	switch(kind) {
	case ZM_FATAL_U1: break;
	case ZM_FATAL_UP: break;

	case ZM_FATAL_U2:
	case ZM_FATAL_GCODE:
	case ZM_FATAL_YCODE:
	case ZM_FATAL_TCODE:
	default:
		return false;
	}

	if (zmg_err.vm) {
		zm_Print out;
		zm_initPrint(&out, stderr, 0);
		zm_printVM(&out, zmg_err.vm);
		return true;
	}

	return false;
}


static void zm_fatalTrigger()
{
	if (zmg_err.at.fatalcb)
		zmg_err.at.fatalcb((zmg_err.vm) ? zmg_err.vm->name : NULL,
		                   (zmg_err.vm) ? zmg_err.vm->data : NULL,
		                   zmg_err.at.data);
}


static void zm_fatalDo(zm_fatal_t kind, const char *ecode,
                                     const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	zm_fatalPrintError(kind, ecode, fmt, args);
	va_end(args);

	if (zmg_err.first) {
		/* The vm dump can produce a second fatal of kind unexpected
		   error if an inconsistency is found. The second fatal must
		   print error and exit suddenly */
		zmg_err.first = false;

		zm_fatalTrigger();

		if (zm_fatalPrintDump(kind)) {
			/* print again the message after the dump */
			va_start(args, fmt);
			zm_fatalPrintError(kind, ecode, fmt, args);
			va_end(args);
		}
	}

	exit(EXIT_FAILURE);
}


/* undefined zmstate */
void zm_fatalNoYield(zm_VM *vm, int out, const char *fn, int nl)
{
	int op = zm_getCurrentState(vm)->on.resume;

	if (out) {
		zm_fatalInitAt(vm, NULL, fn, nl);
		zm_fatalDo(ZM_FATAL_TCODE, "UNDEFSTATE.BRK",
				   "jump over task definition, search "
				   "for unbound `break` in zmstate %d", op);
	} else if (op == 0) {
		zm_fatalInitAt(vm, NULL, fn, nl);
		zm_fatalDo(ZM_FATAL_YCODE, "UNDEFSTATE.0", "zmstate 0 is "
		           "reserved");
	} else {
		zm_fatalInitAt(vm, NULL, fn, nl);
		zm_fatalDo(ZM_FATAL_TCODE, "UNDEFSTATE.N",
				   "reached end of the task definition "
				   "(zmstate %d not defined or forgive "
				   "to zmyield/zmraise)",
				   op);
	}
}



/* ----------------------------------------------------------------------------
 *  DUMP                                                       (SECTION REPORT)
 * --------------------------------------------------------------------------*/

static zm_Trace* zm_getTraceHead(zm_Trace *t);
static int zm_hasException(zm_State *s, int kind);
static int zm_hasCaller(zm_State *s);
static zm_State* zm_caller(zm_State *s);
static size_t zm_deep(zm_State *sub);


#define ZM_DEFAULT_STDOUT(out)                                                \
    zm_Print defaultout;                                                      \
    if (!out) {                                                               \
        out = &defaultout;                                                    \
        zm_initPrint(out, stdout, 0);                                         \
    }

#define ZM_STRCASE(x) case x: return #x


static const char *zm_exceptionKind(int kind)
{
	switch(kind) {
	ZM_STRCASE(ZM_EXCEPTION_ABORT);
	ZM_STRCASE(ZM_EXCEPTION_UNCAUGHT);
	ZM_STRCASE(ZM_EXCEPTION_CONTINUE);
	ZM_STRCASE(ZM_EXCEPTION_CONTINUEHEAD);
	ZM_STRCASE(ZM_EXCEPTION_STARTIMPLOSION);
	}
	return "unknow exception kind";
}


static const char *zm_implodeFlagName(int implodeby)
{
	switch (implodeby) {
	ZM_STRCASE(ZM_IMPLODEBY_EXCEPTION);
	ZM_STRCASE(ZM_IMPLODEBY_SUB);
	ZM_STRCASE(ZM_IMPLODEBY_ROOT);
	ZM_STRCASE(ZM_IMPLODEBY_CUR);
	}
	return "unknow implode flag";
}


#if ZM_DEBUG_LEVEL >= 1
static const char *zm_busyCheckFlagName(int busycheck)
{
	switch (busycheck) {
	ZM_STRCASE(ZM_WSCHECK_ALL);
	ZM_STRCASE(ZM_WSCHECK_NONE);
	ZM_STRCASE(ZM_WSCHECK_SKIPFIRST);
	}

	return "unknow busycheck flag";
}
#endif


#define ZM_STRCASE2(m, c)  case m: return ((compact) ? (c) : #m)

static const char* zm_getModeName(zm_State *s, int compact)
{
	switch (s->pmode) {
	ZM_STRCASE2( ZM_PMODE_NORMAL,           "ONRM");
	ZM_STRCASE2( ZM_PMODE_END,              "OEND");
	ZM_STRCASE2( ZM_PMODE_CLOSE,            "OCLS");
	ZM_STRCASE2( ZM_PMODE_OFF,              "ONUL");
	ZM_STRCASE2( ZM_PMODE_ASYNCIMPLODE,     "OIMP");
	default:
	return (!compact) ?  "ZM_PMODE_?????" : "O???";
	}
}


static void zm_printTraceElement(zm_Print *out, zm_Trace *t)
{
	zm_print(out, "task-id: %zx\t\n", t->taskid);
	zm_print(out, "machine: %s", t->machinename);

	if (t->on)
		zm_print(out, "\t[zmstate: %d]", t->on);

	zm_print(out, "\n");

	if (t->filename) {
		zm_print(out, "filename: %s\n", t->filename);
		zm_print(out, "nline: %d\n", t->nline);
	}
}


void zm_printTrace(zm_Print *out, zm_Exception *e)
{
	zm_Trace *t = e->etrace;

	zm_print(out, "Trace: \n");

	zm_addIndent(out, 3);

	while (t) {
		zm_printTraceElement(out, t);

		if (t->next)
			zm_print(out, "--------------------\n");

		t = t->next;
	}

	zm_addIndent(out, -3);
}


static void zm_printStateException(zm_Print *out, zm_VM *vm, zm_State *estate)
{
	zm_Exception *e = estate->exception;
	const char *k = NULL;
	int usr = true;
	int rse = false;
	int imp = false;

	switch(e->kind) {
	case ZM_EXCEPTION_ABORT: k = "abort"; break;
	case ZM_EXCEPTION_UNCAUGHT: k = "uncaught"; break;
	case ZM_EXCEPTION_CONTINUE: k = "continue"; break;
	case ZM_EXCEPTION_CONTINUEHEAD:
		k = "(continue2)";
		usr = false;
		rse = true;
		break;
	case ZM_EXCEPTION_STARTIMPLOSION:
		k = "(implosion-start)";
		usr = false;
		imp = true;
		break;

	default:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U2, "PRNTEXCEPT.EUN",
			   "exception->kind = %d", e->kind);
		return;
	}

	zm_print(out, "kind: %s\n", k);

	if (usr) {
		if (e->msg)
			zm_print(out, "msg: \"%s\"\n", e->msg);
		else
			zm_print(out, "msg: NULL\n");


		zm_print(out, "ecode: %d\n", e->code);
		zm_print(out, "data: [ref: %zx]\n", e->data);
		zm_print(out, "beforecatch: [ref: %zx]\n", e->beforecatch);
	}

	if (rse) {
		zm_addIndent(out, 3);

		zm_print(out, "raise state: [ref: %zx]%s", e->raisestate,
		          (e->raisestate == estate) ?  " (self)" : "");

		zm_addIndent(out, -3);
	}

	if (imp) {
		zm_print(out, "implosion start: [ref: %zx]\n", e->raisestate);
		zm_print(out, "saved exception: [ref: %zx]\n", e->data);
	}

	if (e->etrace) {
		zm_print(out, "\n");
		zm_printTrace(out, e);
	}
}


#define ZM_CPRINT(c, e)  zm_print(out, (compact) ? (ZM_ILS c) : (ZM_ILS e))


static void zm_printFlags(zm_Print* out, zm_State *s, int compact)
{
	if (zm_isSubTask(s)) {
		zm_print(out, (compact) ? "[sub]" : "(sub)");
	} else if (!compact) {
		zm_print(out, "(ptask)");
	}

	if (s->flag & ZM_STATE_RUN) {
		ZM_CPRINT("[rn]", "(run)");
	} else {
		if (s->flag & ZM_STATE_WAITING) {
			if (s->flag & ZM_STATE_EVENTLOCKED)
				ZM_CPRINT("[we]", "(waiting-event) ");
			else
				ZM_CPRINT("[ws]", "(waiting-subtask) ");
		} else {
			if (s->flag & ZM_STATE_EVENTLOCKED)
				/* #EVB_FLAG */
				ZM_CPRINT("[evb]", "(event-binded) ");
			else
				ZM_CPRINT("[su]", "(suspend) ");

		}
	}

	if (s->flag & ZM_STATE_IMPLOSIONLOCK)
		ZM_CPRINT("[IL]", "(implosion lock) ");


	if (s->flag & ZM_STATE_AUTOFREE)
		/*ZM_CPRINT("[af]", "(autofree) ");*/
		ZM_CPRINT("", "(autofree) ");


	if (s->flag & ZM_STATE_CATCH)
		ZM_CPRINT("[ca]", "(catch) ");


	if (s->flag & ZM_STATE_CONTINUEMARK)
		ZM_CPRINT("[cm]", "(c-mark) ");


	if (s->flag & ZM_STATE_UNUSED)
		ZM_CPRINT("[??]", "( unknow ??? ) ");

}

#undef ZM_CPRINT


void zm_printStateCompact(zm_Print *out, zm_State *s)
{
	ZM_DEFAULT_STDOUT(out);

	#ifdef ZM_DEBUG_MACHINENAME
		zm_print(out, "%s-%zx ", s->debugmachinename, s);
	#else
		zm_print(out, "%zx ", s);
	#endif


	zm_printFlags(out, s, true);

	zm_print(out, ZM_ILS" ");

	/*zm_print(out, "on:%d|%d|%d", s->on.resume, s->on.iter,s->on.c4tch);*/


	if (zm_hasCaller(s))
		zm_print(out, ZM_ILS"caller:%zx ", zm_caller(s));

	if (s->pmode != ZM_PMODE_NORMAL)
		zm_print(out, ZM_ILS"%s ", zm_getModeName(s, true));

	if (s->exception)
		zm_print(out, ZM_ILS"!%zx", s->exception);

	zm_print(out, ZM_ILS"\n");
}


void zm_printState(zm_Print *out, zm_VM *vm, zm_State *s)
{
	ZM_DEFAULT_STDOUT(out);

	zm_print(out, "flag: %d     ", s->flag);

	zm_printFlags(out, s, false);

	zm_print(out, (zm_getCurrentState(vm) == s) ? "(now)\n" : "\n");

	if (zm_isSubTask(s)) {
		size_t i;
		zm_print(out, "caller: [ref: %zx]\n", zm_caller(s));

		zm_print(out, "parent: (subtask) stacksize = %d\n",zm_deep(s));

		for (i = 0; i < zm_deep(s); i++) {
			zm_print(out, "  parent[%d] = [ref: %zx]\n", i,
			         s->parent->stack[i]);
		}
	} else {
		zm_print(out, "parent: NULL (ptask)\n");
	}

	zm_print(out, "subtasks: [ref: %zx]\n", s->subtasks);
	zm_print(out, "siblings: prev=[ref: %zx], next=[ref: %zx]\n",
	         s->siblings.prev, s->siblings.next);

	zm_print(out, "on: resume = %d, iter = %d, catch = %d\n",
	         s->on.resume, s->on.iter, s->on.c4tch);


	if (s->pmode != ZM_PMODE_NORMAL)
		zm_print(out, "pmode: %s\n", zm_getModeName(s, false));

	#ifdef ZM_DEBUG_MACHINENAME
		zm_print(out, "machine: %s\n", s->debugmachinename);
	#endif

	zm_print(out, "next: ");

	if (s->flag & ZM_STATE_RUN) {
		zm_print(out, ZM_ILS"(state)");
	} else {
		if (s->flag & ZM_STATE_EVENTLOCKED) {
			zm_EventBinder* evb = (zm_EventBinder*)s->next;
			zm_print(out, ZM_ILS"(eventbinder->worker: %s)",
				  zm_workerName((zm_Worker*)evb->statenext));
		} else {
			zm_print(out, ZM_ILS"(saved worker: %s)",
				  zm_workerName((zm_Worker*)s->next));
		}
	}
	zm_print(out, ZM_ILS" [ref: %zx]\n", s->next);


	if (!s->exception)
		return;

	zm_print(out, "exception: [ref: %zx]\n", s->exception);

	zm_addIndent(out, 2);

	zm_printStateException(out, vm, s);

	zm_addIndent(out, -2);
}


static void zm_printWorker(zm_Print *out, zm_VM *vm, zm_Worker *w)
{
	ZM_DEFAULT_STDOUT(out);

	zm_print(out, "machine->name: %s\n", w->machine->name);

	zm_print(out, "nstate: %d\n", w->nstate);
	zm_print(out, "cyclestep: %d\n", w->cyclestep);

	zm_print(out, "states.first: [ref: %zx]\n", w->states.first);
	zm_print(out, "states.current: [ref: %zx]\n", w->states.current);
	zm_print(out, "states.previous: [ref: %zx]\n", w->states.previous);

	#ifdef ZM_CHECK_CONSISTENCY
	if ((w->states.current == w->states.previous) &&
	    (w->states.current != w->states.first)) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U2, "PRNTWRK.EQPC",
		           "current == previous != first");
		}
	#endif

	if (w->next) {
		zm_print(out, "next (worker): %s-%zx\n",
		         w->next->machine->name, w->next);
	} else {
			zm_print(out, "next (worker): NULL\n");
	}

	if (w->nstate) {
		int i = 0;
		zm_State *state = w->states.first;

		do {
			zm_print(out, "\n");
			zm_print(out, "   - state: %d", i + 1);
			zm_print(out, " [ref: %zx]\n", state);

			zm_addIndent(out, 5);
			zm_printState(out, vm, state);
			zm_addIndent(out, -5);

			state = state->next;
			i++;
		} while ((state != w->states.first) &&
		         (state != NULL) &&
		         (i <= w->nstate));


		#ifdef ZM_CHECK_CONSISTENCY
		if (i != w->nstate) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U2, "PRNTWRK.1",
			           "nstate = %d but found %d states",
			           w->nstate, i);
		}
		#endif
	} else {
		zm_print(out, "   - no states in this worker -\n");
	}
}


void zm_printInfoVM(zm_Print *out, zm_VM *vm)
{
	ZM_DEFAULT_STDOUT(out);
	zm_addIndent(out, 1);
	zm_print(out, "name: %s\n", (vm->name) ? (vm->name) : "NULL");
	zm_print(out, "ptask count: %d\n", vm->nptask);
	zm_print(out, "worker count: %d\n", vm->nworker);

	zm_print(out, "plock: %d\n", vm->plock);
	zm_print(out, "session.fixedworker: %d\n", vm->session.fixedworker);

	if (vm->nworker) {
		zm_print(out, "workercursor: %s-%zx\n",
		         vm->workercursor->machine->name,
		         vm->workercursor);
	}

	zm_addIndent(out, -1);
	zm_print(out, "\n\n");
}


static void zm_printStateRecursive(zm_Print *out, zm_VM *vm, zm_State *s,
                                                                int deep)
{
	zm_State *first;
	int i = 0;

	ZM_DEFAULT_STDOUT(out);

	if (deep > ZM_PRINTSTATE_MAXDEEP) {
		zm_print(out, "zm_printStateRecursive: [WARNING] "
		         "deep = %d > MAX DEEP", deep);
		return;
	}


	zm_printState(out, vm, s);

	if (!s->subtasks)
		return;

	s = s->subtasks;
	first = s;

	do {
		zm_print(out, "\n");
		zm_print(out, "   - substate (deep=%d): %d [ref: %zx]\n",
		         deep, i++, s);
		zm_addIndent(out, 5);
		zm_printStateRecursive(out, vm, s, deep+1);
		zm_addIndent(out, -5);

		s = s->siblings.next;
	} while (s != first);
}


#ifdef ZM_CHECK_CONSISTENCY

static int zm_matchCaller(zm_State* s, zm_State* cur)
{
	return (zm_getCaller(cur) == s);
}

/*
   task0 (caller: NULL)
     sub1 (caller: task0)
       subsub1 (caller: sub1)

     sub2 (freezed caller: NULL)
        subsub2 (freezed caller: sub2)

    task1 (caller: NULL)


  zm_printCallerStak found:
	  task0 (exec-stack)
	  sub2 (continue-stack)
	  task1 (exec-stack)

  zm_printCallerRecursive(task0)
	  state whose caller is task0 = sub1
	  state whose caller is sub1 = subsub1
	  state whose caller is subsub1 = (none)

  zm_printCallerRecursive(sub2)
	  state whose caller is sub2 = subsub2
	  state whose caller is subsub2 = (none)

  zm_printCallerRecursive(task1)
	  state whose caller is task1 = (none)

*/
static void zm_checkFoundCaller(zm_VM *vm, zm_StateQueue *qiter,
                              zm_State *state, zm_State *caller)
{
	int nmatch = 0;

	while (caller) {
		caller = zm_queueFindPop(qiter, state, zm_matchCaller);
		nmatch++;
	}

	if (nmatch > 1) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U2, "FINDCALL.NN",
				   "findCaller found more than one "
				   "(%d) caller for task %zx",
				   nmatch, state);
	}
}


static int zm_subcheckConsistency(zm_State *s)
{
	zm_State *first;
	int runcount = 0;

	if (s->flag & ZM_STATE_RUN) {
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



static void zm_printCheckConsistency(zm_VM *vm)
{
	size_t i = 0, runcount;
	zm_State *state = vm->ptasks;

	if (!vm->ptasks)
		return;

	do {
		if (!state->siblings.next) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U2, "PRNTTASK.1",
			           "null ref in siblings ring");
		}

		if (i++ > vm->nptask) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U2, "PRNTTASK.2",
			           "siblings count doesn't match");
		}

		runcount = zm_subcheckConsistency(state);


		if (runcount > 1) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U2, "PRNTTASK.RC",
			           "more than one task run in the same"
			           "exec-context (ctx: [ref %zx] count: %d)",
			           state, runcount);
		}

		state = state->siblings.next;

	} while (state != vm->ptasks);
}

#endif


void zm_printTasks(zm_Print *out, zm_VM *vm)
{
	zm_State *state = vm->ptasks;
	size_t i = 0;

	zm_print(out, "*** Tasks:\n");

	if (!vm->ptasks) {
		zm_print(out, "no one task instanced\n\n");
		return;
	}

	zm_addIndent(out, 2);
	do {
		zm_print(out, "\n");
		zm_print(out, "-TASK: %d [ref %zx]\n", i++, state);

		zm_addIndent(out, 1);

		zm_printStateRecursive(out, vm, state, 0);

		state = state->siblings.next;

		zm_addIndent(out, -1);
	} while (state != vm->ptasks);

	zm_addIndent(out, -2);

	zm_print(out, "\n\n");
}


static void zm_printDataTreeBranch(zm_Print *out, zm_State *state, int deep)
{
	zm_State *s, *first;

	ZM_DEFAULT_STDOUT(out);

	if (deep > ZM_PRINTSTATE_MAXDEEP) {
		zm_print(out, "zm_printDataTreeBranch: [WARNING] "
		         "deep = %d > MAX DEEP", deep);
		return;
	}


	if (deep)
		zm_print(out, "|\n");

	zm_printStateCompact(out, state);

	if (!state->subtasks)
		return;

	first = s = state->subtasks;

	do {
		zm_addIndent(out, 5);
		zm_printDataTreeBranch(out, s, deep+1);
		zm_addIndent(out, -5);

		s = s->siblings.next;
	} while (s != first);
}


void zm_printDataTree(zm_Print *out, zm_VM *vm)
{
	zm_State *state = vm->ptasks;

	ZM_DEFAULT_STDOUT(out);

	zm_print(out, "*** Data Tree:\n");

	if (!vm->ptasks) {
		zm_print(out, "no one task instanced\n\n");
		return;
	}

	zm_addIndent(out, 2);
	do {
		zm_addIndent(out, 1);

		zm_printDataTreeBranch(out, state, 0);

		zm_addIndent(out, -1);

		state = state->siblings.next;
	} while (state != vm->ptasks);

	zm_addIndent(out, -2);

	zm_print(out, "\n\n");
}

static void zm_pushStates(zm_StateQueue* q, zm_State *s)
{
	zm_State *first = s;

	do {
		zm_queueAdd(q, s, NULL);

		if (s->subtasks)
			zm_pushStates(q, s->subtasks);

		s = s->siblings.next;
	} while (s != first);
}




static void zm_printCallerRecursive(zm_Print *out, zm_VM *vm,
                                       zm_StateQueue *qiter,
                                            zm_State *state)
{
	zm_State *caller = zm_queueFindPop(qiter, state, zm_matchCaller);

	ZM_DEFAULT_STDOUT(out);

	#ifdef ZM_CHECK_CONSISTENCY
	zm_checkFoundCaller(vm, qiter, state, caller);
	#endif

	if (!caller)
		return;

	zm_addIndent(out, 4);

	zm_print(out, " | \n");
	zm_printStateCompact(out, caller);

	zm_printCallerRecursive(out, vm, qiter, caller);

	zm_addIndent(out, -4);
}


static zm_State* zm_callerStackHead(zm_State *s)
{
	zm_State *caller;
	int n = 0;

	while (n++ < ZM_CALLERSTACK_MAXDEEP) {
		caller = zm_getCaller(s);

		if (caller == NULL)
			return s;

		s = caller;
	}

	return NULL;
}


static void zm_printCallerStackTitle(zm_Print *out, zm_VM *vm, zm_State *head)
{

	if (!head) {
		zm_fatalInit(vm, "zm_printCallerStack");
		zm_fatalDo(ZM_FATAL_U2, "PRNCALSTK.TO",
			   "caller stack is too long "
			   "(see ZM_CALLERSTACK_MAXDEEP)");
	}

	if (zm_isTask(head))
		zm_print(out, "*** Execution Stack:\n");
	else if (zm_hasException(head, ZM_EXCEPTION_CONTINUEHEAD))
		zm_print(out, "*** Continue Exception Stack:\n");
	else
		zm_print(out, "*** Suspended Tasks:\n");
}


void zm_printCallerStack(zm_Print *out, zm_VM *vm)
{
	zm_StateQueue *q;

	ZM_DEFAULT_STDOUT(out);

	if (!vm->ptasks) {
		return;
	}

	q = zm_queueNew();

	zm_pushStates(q, vm->ptasks);

	/* search all leafs */
	while(q->first) {
		zm_StateList *sl = q->first;

		while(sl) {
			zm_State *head, *s = sl->state;

			if (zm_hasCaller(s)) {
				sl = sl->next;
				continue;
			}

			head = zm_callerStackHead(s);

			zm_queueFindPop(q, s, NULL);
			zm_printCallerStackTitle(out, vm, head);

			zm_addIndent(out, 2);

			zm_printStateCompact(out, s);
			zm_printCallerRecursive(out, vm, q, s);
			zm_print(out, "\n");

			zm_addIndent(out, -2);
			break;
		}

		if (!sl)
			break;
	}

	if (q->first) {
		zm_State *s = zm_queuePop0(q, NULL);

		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U2, "PRNCALSTK.CL",
			   "unclassified state %zx", s);
	}

	zm_queueFree(q);
	zm_print(out, "\n\n");
}


void zm_printActiveWorkers(zm_Print *out, zm_VM *vm)
{
	ZM_DEFAULT_STDOUT(out);

	zm_print(out, "*** active workers:\n");

	if (vm->nworker == 0) {
		zm_print(out, "   - no worker in this vm -\n");
	} else {
		size_t i = 0;
		zm_Worker *w = vm->workercursor;

		do {
			zm_print(out, "\n");
			zm_print(out, " - worker %d: ", i++);
			zm_print(out, "%s-%zx\n", w->machine->name, w);

			zm_addIndent(out, 3);
			zm_printWorker(out, vm, w);
			zm_addIndent(out, -3);

			#ifdef ZM_CHECK_CONSISTENCY
			if ((w == w->next) && (w != vm->workercursor)){
				zm_fatalInit(vm, NULL);
				zm_fatalDo(ZM_FATAL_U2, "PRNTVM.1",
				           "worker ring is not closed");
			}
			#endif
			w = w->next;
		} while ((w != vm->workercursor) && (i < vm->nworker));

		#ifdef ZM_CHECK_CONSISTENCY
		if (i !=  vm->nworker) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U2, "PRNTVM.2",
			           "declared nworker = %d but found %d",
			           vm->nworker, i);
		}
		#endif
	}

	zm_print(out, "\n\n");
}


void zm_printVM(zm_Print *out, zm_VM *vm)
{
	ZM_DEFAULT_STDOUT(out);
	zm_print(out, "------------------------------ VM ");
	zm_print(out, "------------------------------\n");

	zm_printInfoVM(out, vm);

	zm_printTasks(out, vm);

	zm_printActiveWorkers(out, vm);

	zm_printDataTree(out, vm);

	zm_printCallerStack(out, vm);

	#ifdef ZM_CHECK_CONSISTENCY
	zm_printCheckConsistency(vm);
	#endif

	zm_print(out, "----------------------------------");
	zm_print(out, "------------------------------\n\n");
}


void zm_printException(zm_Print *out, zm_Exception *e, int trace)
{
	const char *msg = (e->msg) ? (e->msg) : ("");

	ZM_DEFAULT_STDOUT(out);

	zm_print(out, "Exception:\t(%s)\n", zm_exceptionKind(e->kind));
	zm_print(out, "   (%d) \"%s\"\n", e->code, msg);

	if (e->etrace) {
		zm_Trace *t = zm_getTraceHead(e->etrace);

		zm_print(out, " in: %s-%zx (file: %s at line: %d)\n\n",
				  t->machinename,
				  t->taskid,
				  t->filename,
				  t->nline);
	}

	if (trace && e->etrace)
		zm_printTrace(out, e);
}


/* ----------------------------------------------------------------------------
 *  STATE TRAVERSING                                             (SECTION CORE)
 * --------------------------------------------------------------------------*/


static size_t zm_deep(zm_State *sub)
{
	return sub->parent->stacksize;
}


static zm_State* zm_root(zm_State *s)
{
	if (zm_isTask(s))
		return s;

	return s->parent->stack[0];
}


size_t zm_getDeep(zm_State *s)
{
	return (zm_isSubTask(s)) ? s->parent->stacksize : 0;
}

/* what can be used to? if have no meaning remove it TODO */
zm_State* izmGetParent(zm_VM *vm, size_t n, const char *fn, int nl)
{
	zm_State *s = zm_getCurrentState(vm);

	if (zm_isTask(s)) {
		return NULL;
	}

	if (n >= zm_deep(s)) {
		zm_fatalInitAt(vm, "zmGetParent", fn, nl);
		zm_fatalDo(ZM_FATAL_TCODE, "GETP.M", "deep index out of bound");
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


static int zm_hasCaller(zm_State *s)
{
	if (!s->parent)
		return false;

	return s->parent->comeback != NULL;
}


static void zm_setCaller(zm_State *sub, zm_State *s)
{
	sub->parent->comeback = s;
}


static zm_State* zm_caller(zm_State *s)
{
	return s->parent->comeback;
}


zm_State* izmGetRoot(zm_VM *vm)
{
	zm_State *s = zm_getCurrentState(vm);

	if (zm_isTask(s))
		return s;

	return s->parent->stack[0];
}


void* izmGetRootData(zm_VM *vm)
{
	zm_State *s = zm_getCurrentState(vm);

	if (zm_isTask(s))
		return s->data;

	return s->parent->stack[0]->data;
}


zm_State* zm_getCaller(zm_State *s)
{
	return (zm_isSubTask(s)) ? s->parent->comeback : NULL;
}


zm_State* izmGetCaller(zm_VM *vm)
{
	return zm_getCaller(zm_getCurrentState(vm));
}


zm_State* zm_getCurrent(zm_VM *vm)
{
   if (!vm->plock)
	   return NULL;

   return zm_getCurrentState(vm);
}


/* rename in zm_currentMachine TODO */
zm_Machine* zm_getMachine(zm_VM *vm)
{
   if (!vm->plock)
	   return NULL;

   return zm_getCurrentMachine(vm);
}


void* izmGetCallerData(zm_VM *vm)
{
	zm_State *s = zm_getCurrentState(vm);

	return (zm_isSubTask(s)) ? s->parent->comeback->data : NULL;
}


static void zm_stateRewind(zm_Worker *worker, zm_State *s)
{
	worker->states.current = worker->states.first = s;
	worker->states.previous = NULL;
	worker->nstate = 1;
}

/* move states cursor */
static void zm_stateNext(zm_Worker *worker)
{
	worker->states.previous = worker->states.current;
	worker->states.current = worker->states.current->next;
}

static void zm_stateUnlink(zm_VM *vm, zm_Worker *worker)
{
	if (++vm->session.suspendop > 1) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "SCURS.RM",
		           "more than one suspend operation in the "
		           "same session");
	}
	/* move states cursor */
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



/* ----------------------------------------------------------------------------
 *  RESUME - SUSPEND                                             (SECTION CORE)
 * --------------------------------------------------------------------------*/

static void zm_setArgument(zm_State *s, void *argument)
{
	s->rearg = argument;
}


zm_State* izmResult(zm_VM* vm, const char *filename, int nline)
{
	zm_State *s = zm_getCurrentState(vm);

	ZM_ASSERT_VMLOCK("SETRES.VLCK", "zmresult", filename, nline);

	if (zm_isTask(s)) {
		zm_fatalInitAt(vm, "zmresult", filename, nline);
		zm_fatalDo(ZM_FATAL_TCODE, "SETRES.PT",
			   "zmresult can be used only in subtask");
	}

	if (zm_hasFlag(s, ZM_STATE_IMPLOSIONLOCK)) {
		zm_fatalInitAt(vm, "zmresult", filename, nline);
		zm_fatalDo(ZM_FATAL_TCODE, "SETRES.IL",
			   "zmresult cannot be used in a closing task");
	}

	return zm_caller(s);
}

int izmYieldTrace(zm_VM* vm, const char *name, int line)
{
	zm_getCurrentState(vm)->codeframe.filename = name;
	zm_getCurrentState(vm)->codeframe.nline = line;

	return 0;
}


static zm_State* zm_getParent(zm_State *s)
{
	return s->parent->stack[zm_deep(s) - 1];
}


static int zm_hasSameRoot(zm_State *s, zm_State *sub)
{
	if (zm_isTask(s)) {
		return sub->parent->stack[0] == s;
	} else {
		return sub->parent->stack[0] == s->parent->stack[0];
	}
}


static int zm_hasSameContext(zm_State *s1, zm_State *s2)
{
	int t1 = zm_isTask(s1);
	int t2 = zm_isTask(s2);

	if (t1 && t2) {
		return s1 == s2;
	}

	if (!t1)
		return zm_hasSameRoot(s2, s1);
	else
		return zm_hasSameRoot(s1, s2);
}


static void zm_addWorker(zm_VM *vm, zm_Worker *w)
{
	ZM_D("zm_addWorker [%zx] %s", w, w->machine->name);
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



/*
 * can be used only in not empty ring
 */
static void zm_unlinkWorker(zm_VM *vm, zm_Worker *w)
{
	ZM_D("unlinkWorker [%zx] %s", w, w->machine->name);

	if (vm->nworker == 1) {
		/* only one element*/
		vm->workercursor = NULL;
	} else {
		w->prev->next = w->next;
		w->next->prev = w->prev;

		if (vm->workercursor == w) {
			/* This break the sync between workercursor and
			   current session worker*/
			vm->workercursor = vm->workercursor->next;
		}
	}

	vm->nworker--;
}


/*
 * can be used only in not empty ring
 */
static zm_Worker* zm_nextWorker(zm_VM *vm)
{
	vm->workercursor = vm->workercursor->next;

	ZM_D("nextWorker: %s\n", vm->workercursor->machine->name);

	return vm->workercursor;
}

/**
 * can be used only if worker->states.current is running because state->next
 * must point to a state (and not a worker or an evenbinder)
 */
static void zm_rewindWorkerStates(zm_VM *vm, zm_Worker* worker)
{
	worker->states.previous = NULL;
	worker->states.current = worker->states.first;

	if (worker->nstate == 0)
		return;

	#ifdef ZM_CHECK_CONSISTENCY
	if (zm_hasntFlag(worker->states.current, ZM_STATE_RUN)) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "RWINDW.1",
		           "worker contains task but isn't flagged as running");
	}
	#endif
}


/* resume - add to worker
 * (worker  has been temporary stored in next pointer)
 */
static void zm_resumeState(zm_VM *vm, zm_State *s)
{
	/**** worker for a suspended state is temporary stored in s->next*/
	zm_Worker* worker = (zm_Worker*)s->next;

	#ifdef ZM_CHECK_CONSISTENCY
	/* this check is also done in RESBY.IL (should't fail)*/
	if (s->flag & ZM_STATE_IMPLOSIONLOCK) { /* #UNBIND_IMLOCK */
		if (s->on.resume != ZM_TERM) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U1, "RESST.IL",
			           "found lock and implode flag in not closed"
			           "task");
		}
	}

	/* this check is also done in RESBY.RRUN (should't fail)*/
	if (s->flag & ZM_STATE_RUN) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "RESST.R",
		           "found run flag in suspended task");
	}


	if (s->flag & ZM_STATE_EVENTLOCKED) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "RESST.EV",
		           "found event-locked flag in suspended task");
	}
	#endif


	s->flag |= ZM_STATE_RUN;

	zm_disableFlag(s, ZM_STATE_WAITING);

	ZM_D("resumeState: worker = %s\n", worker->machine->name);

	if (!worker) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "RESST.NW",
		           "task has a null worker (possible cause: "
		           "task as been free)");
	}


	/**** Add state to worker ***  */

	if (worker->nstate == 0) {
		zm_stateRewind(worker, s);

		s->next = NULL;

		ZM_D("resumeState - ins worker %s", worker->machine->name);

		/* add worker to vm*/
		zm_addWorker(vm, worker);

		/*ZM_D("resumeState - insert worker ...done <----");*/
		return;
	}

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
				zm_fatalInit(vm, NULL);
				zm_fatalDo(ZM_FATAL_U1, "WSYLST.??",
				           "current=null");
			} else {
				zm_fatalInit(vm, NULL);
				zm_fatalDo(ZM_FATAL_U1, "WSYLST.1",
					   "worker state list sync lost");
			}
		}

		worker->states.previous = s;
		worker->states.first = s;
	}
	s->next = worker->states.current;
	worker->nstate++;
}



/* resume - add to worker
 * (worker  has been temporary stored in next pointer)
 */
static void zm_resumeStateBy(zm_VM *vm, zm_State *s, void *argument,
                                                     const char *ref,
                                                const char *filename,
                                                           int nline)
{
	if (s->flag & ZM_STATE_RUN) {
		if (s == zm_getCurrentState(vm)) {
			zm_fatalInitAt(vm, ref, filename, nline);
			zm_fatalDo(ZM_FATAL_GCODE, "RESBY.RRUN.S",
			           "try to resume the current running task");
		}

		zm_fatalInitAt(vm, ref, filename, nline);
		zm_fatalDo(ZM_FATAL_GCODE, "RESBY.RRUN.J",
		           "try to resume a running task");
	}

	if (s->flag & ZM_STATE_IMPLOSIONLOCK) {
		if (s->on.resume != ZM_TERM) {
			zm_fatalInitAt(vm, ref, filename, nline);
			zm_fatalDo(ZM_FATAL_GCODE, "RESBY.IL",
			           "tring to resume a closed task");
		}
	}


	zm_setArgument(s, argument);
	zm_resumeState(vm, s);
}


/*
 * Resume the caller of a subtask.
 */
static void zm_resumeCaller(zm_VM *vm, zm_State *sub, int iter)
{
	zm_State *c;

	ZM_D("resumeCaller: state = %zx, iter = %d", sub, iter);

	c = zm_caller(sub);

	if (iter) {
		/*  [ON_RESUME_SWITCH]*/
		if (c->on.iter) {
			c->on.resume = c->on.iter;
		}
	}

	/** comeback reset */
	zm_setCaller(sub, NULL);

	/*** resume caller */
	zm_resumeState(vm, c);
}


/**
 * must be used inside a session and can be used max 1 time
 */
static void zm_unlinkCurrentState(zm_VM* vm)
{
	zm_Worker *worker = zm_getCurrentWorker(vm);

	/* Session worker can be different by workercursor (getCurrentWorker
	 * get the session one). In zm_mGo with a non null machine argument
	 * (fixedworker = true) workercursor have no meaning. In zm_go
	 * workercursor and session worker are sync since an zm_unlinkWorker
	 * is performed. */

	ZM_D("unlinkCurrentState w = %s", worker->machine->name);

	zm_stateUnlink(vm, worker);

	worker->nstate--;

	if (worker->nstate == 0)
		zm_unlinkWorker(vm, worker);
}


static void zm_zmstateReserved(zm_VM* vm, int z, const char *c)
{
	const char *m = "zmstate > 250 are reserved";

	if (z == ZM_TERM)
		m = "ZM_TERM (zmstate 255) can't be used directly";

	zm_fatalInit(vm, NULL);
	zm_fatalDo(ZM_FATAL_YCODE, c, m);
}



static void zm_suspendCurrentState(zm_VM* vm, int waiting)
{
	zm_State *state = zm_getCurrentState(vm);

	if (waiting)
		zm_enableFlag(state, ZM_STATE_WAITING);


	zm_unlinkCurrentState(vm);

	/* put temporary worker in next*/
	state->next = (zm_State*)zm_getCurrentWorker(vm);

	zm_disableFlag(state, ZM_STATE_RUN);
}

/**
 *
 */
static void zm_suspendByYield(zm_VM* vm, zm_Yield ms, int waiting)
{

	zm_State *state = zm_getCurrentState(vm);

	ZM_D("suspendCurrentState -- state : [ref %zx]", state);

	if (ms.resume >= ZM_RESERVED)
		zm_zmstateReserved(vm, ms.resume, "YSU.RS");

	if (ms.iter >= ZM_RESERVED)
		zm_zmstateReserved(vm, ms.iter, "YSU.IT");

	if (ms.c4tch >= ZM_RESERVED)
		zm_zmstateReserved(vm, ms.c4tch, "YSU.CT");

	state->on.resume = ms.resume;
	state->on.iter = ms.iter;
	state->on.c4tch = ms.c4tch;

	zm_suspendCurrentState(vm, waiting);
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




/* ----------------------------------------------------------------------------
 *  TASK TERMINATION & EXCEPTION                                 (SECTION CORE)
 * --------------------------------------------------------------------------*/


static void zm_unbindEvent(zm_VM* vm, zm_State *s, void* argument, int scope);
static void zm_abortTask(zm_VM *vm, zm_State *state, const char *refname);


static zm_Exception* zm_newException(int kind)
{
	zm_Exception *e = zm_alloc(zm_Exception);
	e->elock = ZM_ELOCK_OFF;
	e->kind = kind;
	e->code = 0;
	e->msg = NULL;
	e->data = NULL;
	e->etrace = NULL;
	e->raisestate = NULL;
	e->beforecatch = NULL;

	return e;
}


/**
 * Append a state to the exception traceback
 */
static void zm_appendTrace(zm_VM *vm, zm_Exception* e, zm_State *state)
{
	zm_Trace* t = zm_alloc(zm_Trace);

	ZM_D("append Trace Exception state = [ref %zx]", state);

	if (state == zm_getCurrentState(vm)) {
		t->machinename = zm_getCurrentMachineName(vm);
		t->on = state->on.resume;
	} else {
		/* assert !(state->flag & ZM_STATE_RUN) ?? TODO */
		/* assert !(s->flag & ZM_STATE_EVENTLOCKED) ?? TODO */
		/* assert s->next ?? TODO */

		/* trace creation is sync so the only run state in excution
		   stack is the vm current state, others can be only busy
		   waiting mode */

		t->machinename = zm_workerName((zm_Worker*)state->next);

		t->on = 0; /* state->on.resume contain next step */
	}

	t->taskid = (size_t)state;
	t->filename = state->codeframe.filename;
	t->nline = state->codeframe.nline;

	/* store in reverse order: raiser is at the end #EXCEPT_WORKFLOW */
	 t->next = e->etrace;
	 e->etrace = t;
}


static void zm_freeTrace(zm_Exception *e)
{
	zm_Trace *t, *next;

	t = e->etrace;
	while (t) {
		next = t->next;
		zm_free(zm_Trace, t);
		t = next;
	}

	e->etrace = NULL;
}


/*
 *  get the last element of the trace: the raising point
 */
static zm_Trace* zm_getTraceHead(zm_Trace *t)
{
	while (t) {
		if (!t->next)
			break;

		t = t->next;
	}

	return t;
}


static int zm_hasException(zm_State *s, int kind)
{
	if (s->exception)
			if (s->exception->kind == kind)
				return true;

	return false;
}


static zm_State* zm_getLastBeforeCatch(zm_VM *vm, zm_State *s)
{
	zm_State *prev = s;

	ZM_D("getLastBeforeCatch: state s = %zx", s);
	do {
		prev = s;
		s = zm_getCaller(s);

		ZM_D("getLastBeforeCatch: state s = %zx", s);

		if (s == NULL)
			return NULL;

	} while (zm_hasntFlag(s, ZM_STATE_CATCH));


	return prev;
}


static int zm_isSyncImplode(zm_LockAndImplode *li)
{
	return (li->by != ZM_IMPLODEBY_ROOT);
}


static void zm_setBusyCheck(zm_LockAndImplode *li, int mode)
{
	li->busycheck = mode;
}


static void zm_initLockAndImplode(zm_LockAndImplode *li, int implodeby)
{
	li->deepstack = zm_queueNew();
	li->lockstack = zm_queueNew();

	li->by = implodeby;

	switch (implodeby) {
	case ZM_IMPLODEBY_SUB:
		zm_setBusyCheck(li, ZM_WSCHECK_ALL);
		break;

	case ZM_IMPLODEBY_EXCEPTION:
		/* in implode by exception the busycheck will be enabled
		 * after have implode-locked the exception
		 * path trace (that contain waiting subtask)*/
	case ZM_IMPLODEBY_ROOT:
		zm_setBusyCheck(li, ZM_WSCHECK_NONE);
		break;

	case ZM_IMPLODEBY_CUR:
		zm_setBusyCheck(li, ZM_WSCHECK_SKIPFIRST);
		break;
	}

	#if ZM_DEBUG_LEVEL >= 1
	ZM_D("init implode %s", zm_implodeFlagName(implodeby));
	ZM_D("init implode %s", zm_busyCheckFlagName(li->busycheck));
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



/*
 * add the subtask (NOT recursively) of s to deepstack
 */
static void zm_deepStackPush(zm_VM *vm, zm_StateQueue *deepstack, zm_State *s)
{
	zm_State *sub = s->subtasks;

	if (!sub)
		return;


	do {
		ZM_D("M~.-~.-~.-~.-~.-~.-~.- DEEPSTACK: push %zx", sub);
		zm_queueAdd(deepstack, sub, NULL);

		#ifdef ZM_CHECK_CONSISTENCY
		if (!sub->siblings.next) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U1, "DPLCK.NS",
			           "null ref in siblings ring");
		}
		#endif

		sub = sub->siblings.next;
	} while (sub != s->subtasks);
}


/*
 * Analize the busy-waiting state during implode lock.
 * There are three kind of close, each one with some possibile
 * busy-waiting subtask:
 * 1) zmCLOSE - close a subtask: the subtask and all its children should be
 *    suspended. No busy-waiting tasks are expected  =>  CHECK_ALL
 * 2) zmTERM - close current task: current is the only one set in busy-waiting
 *    to wait children close  =>  CHECK_SKIPFIRST
 * 3) zmABORT - raise error exception: the exception path, since catch, is in
 *    busy-waiting (pre) but children of each elements should not be
 *    in waiting mode (post)  =>  pre: CHECK_NONE,  post: CHECK_ALL
 *
 * The only tasks that break these rules are the ones locked by a
 * continue-exception. These subtasks are in busy-waiting mode.
 * This function mark and follow the busy-waiting task comeback since
 * the continue-exception head task. Headers are saved in a stack
 * to be analized by zm_checkContinue.
 */
static void zm_checkBusy(zm_VM *vm, zm_LockAndImplode* li, zm_State *s)
{
	ZM_D("check ws: %zx", s);
	switch (li->busycheck) {
	case ZM_WSCHECK_ALL:
		break;

	case ZM_WSCHECK_SKIPFIRST:
		zm_setBusyCheck(li, ZM_WSCHECK_ALL);
	case ZM_WSCHECK_NONE:
		return;
	}

	ZM_D("check ws: have wating...");
	if (zm_hasntFlag(s, ZM_STATE_WAITING))
		return;

	/* mark continue exception */
	do {
		ZM_D("check ws: follow comeback: %s", s);
		if (zm_hasFlag(s, ZM_STATE_CONTINUEMARK))
			return;

		ZM_D("check ws: follow comeback enable contmark flag");
		zm_enableFlag(s, ZM_STATE_CONTINUEMARK);

		if (zm_hasException(s, ZM_EXCEPTION_CONTINUEHEAD)) {
			/* Found the head of the continue-exception */
			ZM_D("check ws: found contref");
			zm_queueAdd( li->econtinue, s, NULL);
			return;
		}

		s = zm_getCaller(s);
	} while (s);

	zm_fatalInit(vm, NULL);
	zm_fatalDo(ZM_FATAL_U1, "ILWCHK.UC",
	           "continue exception head not found at the end "
	           "of waiting subtaks chain");
}


/*
 * The continue-exception (found with zm_checkBusy) must be must be completely
 * inside the implosion
 */
static void zm_checkContinue(zm_VM *vm, zm_LockAndImplode *li)
{
	zm_State *state;
	int broken = false;

	ZM_D("checkContinue - begin: %zx", li->econtinue);
	while ((state = zm_queuePop0(li->econtinue, NULL))) {
		ZM_D("checkContinue - pop %zx", state);

		if (zm_hasntFlag(state, ZM_STATE_IMPLOSIONLOCK)) {
			broken = true;
			break;
		}
	}

	ZM_D("checkContinue - empty ... broken = %d", broken);

	if (broken) {
		zm_fatalInitByLI(vm, li);
		zm_fatalDo(ZM_FATAL_GCODE, "CONTBREAK",
		           "close operation broke a continue-exception");
	}


	zm_queueFree(li->econtinue);
	li->econtinue = NULL;

	ZM_D("checkContinue - end");
	return;
}


/**
 *
 */
static void zm_initAsyncSerialization(zm_VM* vm, zm_State *state,
                                           zm_LockAndImplode *li)
{
	/* #ASYNC_SERIALIZATION [step 1] */

	if (li->by != ZM_IMPLODEBY_ROOT) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "LIUNLN.RUN",
		           "found a running state in a sync implosion");
	}

	if (li->running) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "LIUNLN.2R", "more than "
		           "one running state in async implosion");
	}

	ZM_D("Init async serialization");

	/** set implode running reference */
	li->running = state;

	/* state cannot be currentstate (see ABRT.SELF, DEEPLCK.SELF)  */
	state->pmode = ZM_PMODE_ASYNCIMPLODE;
}


static void zm_setImplodeLock(zm_VM *vm, zm_LockAndImplode* li, zm_State *state)
{
	size_t deep;

	if (state->flag & ZM_STATE_EVENTLOCKED)
		zm_unbindEvent(vm, state, NULL,
		               ZM_EVENT_UNBIND_FORCE |
		               ZM_EVENT_UNBIND_ABORT);

	/** save current zmop in iter to be extract with zmGetCloseOp*/
	state->on.iter = state->on.resume;
	state->on.resume = ZM_TERM;
	/** state->catch must be preserved to allow catch in ZM_TERM */
	/** #LOCK_SAVE_CATCH (this happend when a task */
	/** is aborted before process the catch) */

	/* This must be done after eventUnbind call because*/
	/* when it resume parent check implosion lock flag */
	/* #UNBIND_IMLOCK*/
	state->flag |= ZM_STATE_IMPLOSIONLOCK;

	state->pmode = ZM_PMODE_CLOSE;

	if (!li)
		return;

	deep = zm_getDeep(state);

	ZM_D("deepLock.setImplodeLock - queuestack add state %zx", state);
	zm_queueAdd(li->lockstack, state, NULL);


	if (!li->count) {
		li->fromdeep = deep;
		li->todeep = deep;
	} else {
		/* from and to are traslated by 1 */
		if (deep < li->fromdeep)
			li->fromdeep = deep;

		if (deep > li->todeep)
			li->todeep = deep;
	}

	zm_checkBusy(vm, li, state);


	if (zm_hasFlag(state, ZM_STATE_RUN)) {
		/* #ASYNC_SERIALIZATION  [step 1]:
		   async serialization is composed by 3 step:
		   1) set a special pmode in the running state
		   2) after serialization set a fake exception in running
		      state that contain the state where implosion start
		   3) the special pmode allow unlink the running state and
		      resume the implosion start state

		   step 1), 2) are sync to this operation while 3) is async
		*/
		zm_initAsyncSerialization(vm, state, li);
	}

	li->count++;

	ZM_D("deepLock.setImplodeLock - end");
}


static void zm_deepLockOverlap(zm_VM *vm, zm_LockAndImplode* li, zm_State *s)
{
	switch (li->by) {
	case ZM_IMPLODEBY_ROOT:
		if (zm_hasException(s, ZM_EXCEPTION_ABORT)) {
			if (li->justlock.exception) {
				zm_fatalInit(vm, NULL);
				zm_fatalDo(ZM_FATAL_U1, "DEEPOV.2E",
				           "deep-lock found more than"
				           "one exceptions");
			}

			li->justlock.exception = s->exception;
		}

		li->justlock.state = s;
		li->justlock.count++;

		return;


	case ZM_IMPLODEBY_EXCEPTION:
		return;

	case ZM_IMPLODEBY_SUB:
	case ZM_IMPLODEBY_CUR:
		/* in SUB and CUR overlap should not be found */
		zm_fatalInitByLI(vm, li);
		zm_fatalDo(ZM_FATAL_U1," DEEPOV.ILK",
			   "lock and implode flag found "
			   "in sync deep lock");
	default:
		zm_fatalInitByLI(vm, li);
		zm_fatalDo(ZM_FATAL_U1," DEEPOV.???",
			   "unknow lock and implode flag");
	}
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
			zm_fatalInitByLI(vm, li);
			zm_fatalDo(ZM_FATAL_U1, "DEEPLCK.SELF",
			           "self-close");
		}

		ZM_D("deepLock - lock %zx", state);

		if (zm_hasntFlag(state, ZM_STATE_IMPLOSIONLOCK)) {
			/* add implode lock and add subtasks to the deepstack */
			zm_setImplodeLock(vm, li, state);

			/* add subtasks to the deepstack */
			zm_deepStackPush(vm, li->deepstack, state);
		} else {
			/* the lib allow only two abort at the same time
			 * one caused by a sync close (for example zmTERM,
			 * zmABORT ...) that lock any other kind of
			 * subtask-abort.
			 * The second close can be only an async one, over
			 * the entire ptask (zm_abortTask).
			 */
			ZM_D("deepLock - just locked");
			zm_deepLockOverlap(vm, li, state);
		}
	}

	zm_queueFree(li->deepstack);


	ZM_D("deepLock - end");
}


static zm_StateQueue** zm_lock2ImplodeStack(zm_LockAndImplode *li)
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
	/* #ASYNC_SERIALIZATION [step 2] */
	zm_Exception *e;

	e = zm_newException( ZM_EXCEPTION_STARTIMPLOSION );

	/** save implosion start in raisestate */
	e->raisestate = start;

	/** store running state exception (if exists) in data */
	e->data = running->exception;

	running->exception = e;
}


static zm_State* zm_popAsyncImplosionStart(zm_State *state)
{
	/* #ASYNC_SERIALIZATION [step 3] */
	zm_Exception *e = (zm_Exception *)state->exception->data;

	zm_State *implosionstart = state->exception->raisestate;

	zm_free(zm_Exception, state->exception);

	state->exception = e;

	return implosionstart;
}


static void zm_serialize(zm_State *state, zm_State *tail)
{
	zm_setCaller(state, tail);

	if (zm_hasntFlag(state, ZM_STATE_RUN))  {
		/* all substate in an implosion is in waiting-subtask except
		 * the implosion start state #IMPLODE_WAITING_CHAIN */
		zm_enableFlag(state, ZM_STATE_WAITING);
	}
}


static zm_State *zm_serializeImplosion(zm_LockAndImplode *li,
                                zm_StateQueue **implodestack,
                                        zm_Exception *except)
{
	size_t from = li->fromdeep;
	size_t to = li->todeep;
	size_t fromto = 1 + to - from;
	size_t i;

	zm_State *state, *last;

	state = zm_queuePop0(implodestack[0], NULL);

	/* set the chaintail state (error raising state) as caller of the
	   first implosion element */
	if (li->chaintail)
		zm_serialize(state, li->chaintail);

	/* when exception is not null (error raising case) set the first
	   implosion element as exception->beforecatch (NOTE this is
	   semanticaly wrong because first implosion element can be
	   different from "state before catch" if zmRESET exclude this
	   element from implosion */
	if (except)
		except->beforecatch = state;

	ZM_D("i-serialize - first = %zx", state);

	last = state;

	for (i = from; i <= to; i++) {
		ZM_D("i-serialize - deep = %d", i);

		while ((state = zm_queuePop0(implodestack[i-from],NULL))) {
			ZM_D("i-serialize - comeback(%zx) = %zx", state,last);
			zm_serialize(state, last);
			last = state;
		}

		zm_queueFree(implodestack[i - from]);
	}


	ZM_D("i-serialize - free implodestack[%d]", fromto);

	zm_nfree(zm_StateQueue*, fromto, implodestack);

	ZM_D("i-serialize - last = %zx", last);

	return last;
}


static void zm_startImplosion(zm_VM *vm, zm_LockAndImplode *li, zm_State *last)
{
	ZM_D("startImplode [ref %zx]?", last);
	if (li->justlock.count) {
		/* link sync with async lock and implode */
		zm_State *state = li->justlock.state;

		ZM_D("startImplode - just lock");

		if (li->justlock.exception)
			state = li->justlock.exception->beforecatch;

		ZM_D("startImplode - before catch = %zx", state);
		/* link async lock and implode with sync one */
		zm_setCaller(state, last);
		return;
	}

	/** last pop element is the first to run*/
	ZM_D("startImplode - have a running state [ref %zx]?", li->running);

	if (li->running) {
		ZM_D("startImplode - async serialization");
		/* #ASYNC_SERIALIZATION [step 2]*/
		/* there is just a running state, set last in root to be */
		/* resumed after running state as been suspended */
		zm_pushAsyncImplosionStart(li->running, last);
	} else {
		ZM_D("startImplode - resume [ref %zx]", last);
		zm_resumeState(vm, last);
	}
}


static void zm_implode(zm_VM *vm, zm_LockAndImplode *li, zm_Exception *e)
{
	zm_State *last;
	zm_StateQueue** implodestack;

	ZM_D("implode - lock to deep stack");

	if (li->count == 0) {
		ZM_D("implode - free lockstack");
		/* no element to close */
		zm_queueFree(li->lockstack);

		if (e) {
			ZM_D("implode - totaly zmRESET-ed");
			/* totaly resetted exception */
			if (li->chaintail)
				zm_resumeState(vm, li->chaintail);
		}

		return;
	}

	implodestack = zm_lock2ImplodeStack(li);

	/* serialize comeback from top to bottom pop elements in each
	 * deep level from the bottom to the top of the stack linking
	 * each element with the previous
	 */
	ZM_D("implode - serialization");

	last = zm_serializeImplosion(li, implodestack, e);

	/* resume or link to exception to run implosion */
	zm_startImplosion(vm, li, last);

	ZM_D("implode - end");
}


static int zm_hasReset(zm_State *s)
{
	return (s->on.c4tch) && zm_hasntFlag(s, ZM_STATE_CATCH);
}


static void zm_lockByException(zm_VM *vm, zm_LockAndImplode *li, zm_State *s)
{
	#ifdef ZM_CHECK_CONSISTENCY
	if (zm_hasFlag(s, ZM_STATE_IMPLOSIONLOCK)) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "LCKBE.IL",
		           "lock and implode flag in exec-stack "
		           "(along exception path)");
	}
	#endif

	if (zm_hasReset(s)) {
		/* zmRESET */
		ZM_D("lockbyexception: apply zmRESET %zx", s);

		/* set resume point to reset one (saved in c4tch) */
		s->on.resume = s->on.c4tch;
		s->on.iter = 0;

		#ifdef ZM_CHECK_CONSISTENCY
		if (zm_isTask(s)) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U1, "LCKBE.RS",
			           "exception-reset applied to a ptask");

		}
		#endif

		/* a reset substate must be suspended: no waiting, no caller */
		zm_disableFlag(s, ZM_STATE_WAITING);
		zm_setCaller(s, NULL);

		return;
	}

	ZM_D("lockbyexception: add implode lock to %zx", s);
	zm_setImplodeLock(vm, li, s);

	/* add subtasks to the deepstack */
	zm_deepStackPush(vm, li->deepstack, s);
}


static void zm_setUncaughtError(zm_VM *vm, zm_Exception *e)
{
	e->kind = ZM_EXCEPTION_UNCAUGHT;

	ZM_D("set uncaught error %zx", e);

	if (vm->uncaught) {
		zm_fatalInit(vm, NULL);
		zm_fatalException(vm->uncaught);
		zm_fatalDo(ZM_FATAL_YCODE, "UNCEX.JP",
		           "raised an error exception without a catch "
		           "but another uncaught error is just present "
			   "(only one uncaught error at a time can be set, "
			   "clean the first one with zm_uCatch)");
	}

	vm->uncaught = e;
}


static void zm_traceUncaught(zm_VM *vm, zm_State *s, zm_Exception* e)
{
	do {
		zm_appendTrace(vm, e, s);
		s = zm_getCaller(s);
	} while (s);
}


static void zm_fatalUncaughtContinue(zm_VM *vm, zm_State *s, zm_Exception *e)
{
	zm_traceUncaught(vm, s, e);
	zm_fatalInit(vm, NULL);
	zm_fatalException(e);
	zm_fatalDo(ZM_FATAL_YCODE, "NOCATCH.C",
	           "raised continue exception but cannot find catch");
}


static int zm_uncaughtException(zm_VM *vm, zm_State *state, zm_Exception *e)
{
	zm_State *root = zm_root(state);

	if (root != state)
		if (zm_getLastBeforeCatch(vm, state))
			return false;

	zm_traceUncaught(vm, state, e);

	zm_setUncaughtError(vm, e);

	zm_abortTask(vm, root, "zmraise");

	return true;
}


static void zm_implodeMonoTask(zm_VM *vm, zm_State *state)
{
	ZM_D("implode mono task (ptask without children)");
	zm_setImplodeLock(vm, NULL, state);

	if (zm_hasntFlag(state, ZM_STATE_RUN))
		zm_resumeState(vm, state);
}



static zm_State *zm_lockAndTrace(zm_VM *vm, zm_LockAndImplode *li, zm_State *s,
                                                               zm_Exception *e)
{
	zm_State *catcher = NULL;
	ZM_D("lockAndImplodeByException - trace");
	/** trace exception */
	do {
		zm_appendTrace(vm, e, s);

		/* caller must be take before lockByException because it can
		   null comeback to accomplish zmRESET */
		catcher = zm_getCaller(s);

		zm_lockByException(vm, li, s);

		s = catcher;
	} while (zm_hasntFlag(catcher, ZM_STATE_CATCH));

	ZM_D("lockAndImplodeByException - catch state = %zx", catcher);

	zm_appendTrace(vm, e, catcher);

	return catcher;
}


static void zm_lockAndImplodeByAbortException(zm_VM *vm, zm_State *state,
                                                         zm_Exception *e)
{
	zm_LockAndImplode li;
	zm_State *catcher;


	ZM_D("lockAndImplodeByException - %zx", state);

	if (zm_uncaughtException(vm, state, e)) {
		return;
	}

	ZM_D("lockAndImplodeByException - init lock and implode");

	li.refname = "zmABORT";
	li.filename = state->codeframe.filename;
	li.nline = state->codeframe.nline;

	zm_initLockAndImplode(&li, ZM_IMPLODEBY_EXCEPTION);

	catcher = zm_lockAndTrace(vm, &li, state, e);

	/* set the tail of the implosion */
	li.chaintail = catcher;

	/* set exception in catch state */
	catcher->exception = e;

	/* precautional reset for zmDROP (serializeImplosion just reset it) */
	e->beforecatch = NULL;

	/* enable busy check after lock the exception path */
	zm_setBusyCheck(&li, ZM_WSCHECK_ALL);

	ZM_D("lockAndImplodeByException - 3");

	zm_deepLock(vm, NULL, &li);

	zm_checkContinue(vm, &li);

	zm_implode(vm, &li, e);
}


static void zm_lockAndImplodeBy(zm_VM *vm, zm_State *state, int implodeby,
                                const char* refname, const char *filename,
                                                                int nline)
{
	zm_LockAndImplode li;


	ZM_D("lockAndImplodeBy - %s state = %zx",
	zm_implodeFlagName(implodeby), state);

	li.refname = refname;
	li.filename = filename;
	li.nline = nline;

	if (zm_isTask(state)) {
		/* ptask */
		if (!state->subtasks) {
			zm_implodeMonoTask(vm, state);
			return;
		}

		zm_initLockAndImplode(&li, implodeby);

		zm_deepLock(vm, state, &li);

		if (zm_isSyncImplode(&li))
			zm_checkContinue(vm, &li);

		zm_implode(vm, &li, NULL);

	} else {
		/* subtask */
		zm_initLockAndImplode(&li, implodeby);

		/* set the tail of the implosion:
		 * - zmTERM     don't change comeback
		 * - zmCLOSE    set the comeback as the state that invoke close
		 */
		li.chaintail = zm_getCaller(state);

		/* calculate: from and to */
		zm_deepLock(vm, state, &li);

		zm_checkContinue(vm, &li);

		zm_implode(vm, &li, NULL);
	}
}


static void zm_abortTask(zm_VM *vm, zm_State *state, const char *refname)
{

	if (zm_hasFlag(state, ZM_STATE_IMPLOSIONLOCK)) {
		/* This check is mandatory because zm_deepLock can set
		 * one exception and can manage only one other exception just
		 * present. After zm_abortTask there can be more than one
		 * exception and a second call of zm_abortTask will cause a
		 * fatal. Moreover this check avoid an useless second lock
		 * and implode. */
		return;
	}

	ZM_D("zm_abortTask: %zx by %s", state, refname);
	zm_lockAndImplodeBy(vm, state, ZM_IMPLODEBY_ROOT, refname, NULL, 0);
}


/*  TODO adjust/clean this code */
#define zm_fatalWrongCtx(ecode, s)                                            \
    do {                                                                      \
    if (!zm_hasSameRoot(current, s)) {                                       \
        zm_fatalInitAt(vm, refname, filename, nline);                         \
        zm_fatalDo(ZM_FATAL_YCODE, ecode, nsamectx);                          \
    } else {                                                                  \
        zm_fatalInitAt(vm, refname, filename, nline);                         \
        zm_fatalDo(ZM_FATAL_YCODE, ecode, wrongres);                          \
    }                                                                         \
    } while(0);

/**
 *
 *	  +subsub3
 *	  |
 *	  |   subsubsub
 *	  |   |
 *	  +subsub2
 *	  |
 *  +--sub1
 *  |
 *  +--iterB
 *  |
 *  |    subiterA
 *  |    |
 *  +--iterA
 *  |
 *  PTask
 *
 * yield down:
 *	from subsubsub: yield iterA      [OK]
 *	from subsubsub: yield subsub3    [OK]
 *	from subsubsub: yield subiterA   [WRONG]
 *
 * yield up (child-yield):           [OK]
 *  from sub1: yield subsub2         [OK]
 *  from sub1: yield subsubsub       [WRONG]
 *
 * yield sibling:
 *	from subsub2: yield subsub3      [OK]
 *	from iterA: yield iterB          [OK]
 *
*/

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

		if (!zm_hasSameRoot(current, sub)) {
			zm_fatalInitAt(vm, refname, filename, nline);
			zm_fatalDo(ZM_FATAL_YCODE, "WRONGCTX.PT", nsamectx);
		}
		return;
	}


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


static zm_yield_t zm_raiseException(zm_VM *vm, zm_Exception* e)
{
	zm_State *state = zm_getCurrentState(vm);

	/* #EXCEPT_WORKFLOW #CONTINUE_EXCEPT */

	ZM_D("raise Exception");

	/* set exception */
	state->exception = e;

	if (e->kind == ZM_EXCEPTION_ABORT)
		return ZM_TASK_RAISE_ABORT_EXCEPTION;
	else
		return ZM_TASK_RAISE_CONTINUE_EXCEPTION;
}


static void zm_unraise(zm_VM *vm, zm_State* state, void *argument,
                          const char *ref, const char *fn ,int nl)
{
	zm_Exception *e = state->exception;

	zm_resumeStateBy(vm, e->raisestate, argument, ref, fn, nl);

	zm_setCaller(e->beforecatch, zm_getCurrentState(vm));

	zm_free(zm_Exception, e);

	state->exception = NULL;
}


void zm_abort(zm_VM *vm, zm_State *state)
{
	const char *refname = "zm_abort";

	if (zm_isSubTask(state)) {
		zm_fatalInit(vm, refname);
		zm_fatalDo(ZM_FATAL_GCODE, "ABRT.SUB",
		           "expected a task but found a subtask");
	}

	if (vm->plock) {
		if (state == zm_getCurrentState(vm)) {
			zm_fatalInit(vm, refname);
			zm_fatalDo(ZM_FATAL_GCODE, "ABRT.ME",
			           "cannot close current processed ptask "
			           "with zm_abort (use: zmyield zmTERM)");

		}

		if (zm_hasSameContext(state, zm_getCurrentState(vm))) {
			zm_fatalInit(vm, refname);
			zm_fatalDo(ZM_FATAL_GCODE, "ABRT.SELF",
			           "cannot abort a ptask that is the parent "
			           "of the current processed task");
		}
	}

	zm_abortTask(vm, state, refname);
}


zm_Exception *zm_uCatch(zm_VM *vm)
{
	zm_Exception *e = vm->uncaught;
	vm->uncaught = NULL;
	return e;
}


void zm_uFree(zm_VM *vm, zm_Exception *e)
{
	switch(e->kind) {
	case ZM_EXCEPTION_UNCAUGHT:
		zm_freeTrace(e);
		break;

	case ZM_EXCEPTION_ABORT:
	case ZM_EXCEPTION_CONTINUE:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_GCODE, "FREER.EC",
		           "zm_ufree can be used only to free "
		           "abort-exception catched with zm_uCatch");

	default:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "FREEX.EUN",
		           "exception.kind = (%s) %d",
		           zm_exceptionKind(e->kind), e->kind);
	}

	e->msg = NULL;
	e->data = NULL;
	zm_free(zm_Exception, e);
}



zm_Exception *izmCatch(zm_VM *vm, int ekindfilter, const char* refname,
                                       const char *filename, int nline)
{
	/* #EXCEPT_WORKFLOW #CONTINUE_EXCEPT */
	zm_State *state = zm_getCurrentState(vm);
	zm_Exception *e = state->exception;

	ZM_D("%s: exception = %zx - filter = %d", refname, e, ekindfilter);

	ZM_ASSERT_VMLOCK("XCATCH.VLCK", refname, filename, nline);

	/* This allow to use zmCatch to check if there is an exception in
	   zmstate that not only for catch (example: yield 4 | vmCATCH(4))*/
	if (e == NULL)
		return NULL;

	switch(e->kind) {
	case ZM_EXCEPTION_ABORT: break;
	case ZM_EXCEPTION_CONTINUE: break;
	case ZM_EXCEPTION_STARTIMPLOSION: return NULL;
	case ZM_EXCEPTION_CONTINUEHEAD: return NULL;

	default:
		zm_fatalInit(vm, refname);
		zm_fatalDo(ZM_FATAL_U1, "XCATCH.UN", "exception.kind = %d (%s)",
		           e->kind, zm_exceptionKind(e->kind));
	}

	if (ekindfilter)
		if (ekindfilter != e->kind)
			return NULL;

	/* unlock exception */
	e->elock = ZM_ELOCK_OFF;

	state->exception = NULL;

	return e;
}


zm_yield_t izmDROP(zm_VM *vm, zm_Exception* e, const char *filename, int nline)
{

	ZM_ASSERT_VMLOCK("DROPERR.VLCK", "zmDROP", filename, nline);

	if (e->kind != ZM_EXCEPTION_ABORT) {
		zm_fatalInitAt(vm, "zmDROP", filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "DROP.WK",
		           "zmDROP need an abort-exception");
	}

	e->elock = ZM_ELOCK_REUSE;

	return zm_raiseException(vm, e);
}


zm_yield_t izmCLOSE(zm_VM *vm, zm_State *state, const char *filename, int nline)
{
	ZM_D("zmCLOSE(%zx)", state);

	ZM_ASSERT_VMLOCK("ICLOSE.VLCK", "zmCLOSE", filename, nline);

	if (zm_isTask(state)) {
		zm_fatalInitAt(vm, "zmCLOSE", filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "ICLOSE.T",
		           "expected a subtask but found a task");
	}

	if (zm_getParent(state) != zm_getCurrentState(vm)) {
		zm_fatalInitAt(vm, "zmCLOSE", filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "ICLOSE.NC",
		           "zmCLOSE can close only its child");
	}

	zm_setCaller(state, zm_getCurrentState(vm));

	zm_lockAndImplodeBy(vm, state, ZM_IMPLODEBY_SUB, "zmCLOSE",filename,
	                    nline);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}


zm_yield_t izmEXCEPT(zm_VM *vm, int kind, int ecode, const char *msg,
                         void* data, const char *filename, int nline)
{
	const char *refname;
	zm_Exception *e;

	e = zm_newException( kind );

	if (kind == ZM_EXCEPTION_ABORT)
		refname = "zmABORT";
	else
		refname = "zmCONTINUE";

	e->code = ecode;
	e->msg = msg;
	e->data = data;
	e->elock = ZM_ELOCK_ON;

	ZM_D("%s(kind = %s, code = %d, msg = %s)", refname,
	         zm_exceptionKind(kind),
	         e->code,
	         (e->msg) ? e->msg : "[NULL]");

	ZM_ASSERT_VMLOCK("RAISENEW.VLCK", refname, filename, nline);

	if (kind == ZM_EXCEPTION_CONTINUE) /* #CONTINUE_EXCEPT */
		e->raisestate = zm_getCurrentState(vm);

	if (zm_getCurrentState(vm)->exception) {
		zm_fatalInitAt(vm, refname, filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "RAISENEW.JP",
			   "zmraise found a pending exception. A new exception "
			   "can be raised only if the pending one as just been "
			   "catched (use zmCatch)");
	}

	return zm_raiseException(vm, e);
}


/*
 * e.g. yield zmUNRAISE
 */
zm_yield_t izmUNRAISE(zm_VM *vm, zm_State* state, void *argument,
                                          const char *fn, int nl)
{
	ZM_D("unraise Exception");

	/* #CONTINUE_EXCEPT*/
	if (zm_isTask(state)) {
		zm_fatalInitAt(vm, "zmUNRAISE", fn, nl);
		zm_fatalDo(ZM_FATAL_YCODE, "UNRAISE.S",
		           "unraise can be applied only to subtask");

	}

	zm_canBeContextStackPush(vm, state, "zmUNRAISE", fn, nl);

	if (!state->exception) {
		zm_fatalInitAt(vm, "zmUNRAISE", fn, nl);
		zm_fatalDo(ZM_FATAL_YCODE, "UNRAISE.NE",
		           "unraise can be applied only to subtask with "
		           "continue-exception (no exception found)");
	}

	if (state->exception->kind != ZM_EXCEPTION_CONTINUEHEAD) {
		zm_fatalInitAt(vm, "zmUNRAISE", fn, nl);
		zm_fatalDo(ZM_FATAL_YCODE, "UNRAISE.WK",
		           "unraise can be applied only to subtask with "
		           "continue-exception (exception: %s)",
		           zm_exceptionKind(state->exception->kind));
	}


	zm_unraise(vm, state, argument, "zmUNRAISE", fn, nl);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}


zm_State *izmContinueHead(zm_VM* vm, zm_Exception *e, const char *fn, int nl)
{
	ZM_ASSERT_VMLOCK("CONTHEAD.VLCK", "zmContinueHead", fn, nl);

	if (e->kind != ZM_EXCEPTION_CONTINUE) {
		zm_fatalInitAt(vm, "zmContinueHead", fn, nl);
		zm_fatalDo(ZM_FATAL_TCODE, "CONTH.NC",
		           "zmContinueHead work only with continue "
		           "exception (found %s)",
		           zm_exceptionKind(e->kind));
	}

	return e->beforecatch;
}


zm_State *izmContinueTail(zm_VM* vm, zm_Exception *e, const char *fn, int nl)
{
	ZM_ASSERT_VMLOCK("CONTTAIL.VLCK", "zmContinueTail", fn, nl);

	if (e->kind != ZM_EXCEPTION_CONTINUE) {
		zm_fatalInitAt(vm, "zmContinueTail", fn, nl);
		zm_fatalDo(ZM_FATAL_TCODE, "CONTT.NC",
		           "zmContinueTail work only with continue "
		           "exception (found  %s)",
		           zm_exceptionKind(e->kind));
	}

	return e->raisestate;
}


zm_yield_t izmCATCH(zm_VM* vm, int n)
{
	zm_enableFlag(zm_getCurrentState(vm), ZM_STATE_CATCH);

	return ZM_B2(n);
}


zm_yield_t izmRESET(zm_VM* vm, int n, const char* filename, int nline)
{
	if (zm_isTask(zm_getCurrentState(vm))) {
			zm_fatalInitAt(vm, "zmRESET", filename, nline);
			zm_fatalDo(ZM_FATAL_YCODE, "RST.PT",
			           "zmRESET can be used "
			           "only in subtask");
	}

	return ZM_B2(n);
}



#if 0
uint8_t izmGetCloseOp(zm_VM *vm, const char *filename, int nline);

#define zmGetCloseOp()                                                        \
        izmGetCloseOp(vm, __FILE__, __LINE__)


uint8_t izmGetCloseOp(zm_VM *vm, const char *filename, int nline)
{
	zm_State *state = zm_getCurrentState(vm);

	ZM_ASSERT_VMLOCK("GETCLSOP.VLCK", "zmGetCloseOp", filename, nline);

	/* #LOCK_SAVE_CATCH */
	if (zm_hasntFlag(state, ZM_STATE_IMPLOSIONLOCK)) {
		zm_fatalInitAt(vm, "zmGetCloseOp", filename, nline);
		zm_fatalDo(ZM_FATAL_TCODE, "GETCLSOP.1",
		           "this function can be call only in "
		           "ZM_TERM");
	}

	return state->on.iter;
}
#endif



/* ----------------------------------------------------------------------------
 *  EVENT                                                        (SECTION CORE)
 * --------------------------------------------------------------------------*/



static void zm_bindEvent(zm_Event *event, zm_State *s)
{

	zm_EventBinder *evb = zm_alloc(zm_EventBinder);

	zm_enableFlag(s, ZM_STATE_EVENTLOCKED);

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




/* note: this method free evb reference */
static void zm_unbindEvent(zm_VM* vm, zm_State *s, void* argument, int scope)
{
	zm_EventBinder *evb = ((zm_EventBinder*)s->next);
	int forced = (scope & ZM_EVENT_UNBIND_FORCE);

	ZM_D("unbindEvent: check flag");

	if (s->flag & ZM_STATE_EVENTLOCKED) {
		zm_disableFlag(s, ZM_STATE_EVENTLOCKED);
	} else {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "UNBNDEV",
		           "zm_State doesn't have a binded event");
	}

	if ((forced) && (evb->event->evcb))
		evb->event->evcb(scope, evb->event, s, argument);


	/* check if evb is the first element of the bindlist */
	if (evb->event->bindlist == evb) {
		if (evb->event->bindlist->next == evb) {
			/* only one element */
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

	/* zmUNBIND */
	if ((forced) && (s->on.iter))
		s->on.resume = s->on.iter;

	ZM_D("unbindEvent: resume");
	/* #UNBIND_IMLOCK*/
	zm_resumeState(vm, s);
	zm_setArgument(s, argument);

	ZM_D("unbindEvent: free event binder");
	zm_free(zm_EventBinder, evb);

	ZM_D("unbindEvent: end");
}


static void zm_triggerWrongReturn(zm_VM *vm, int r)
{
	zm_fatalInitAt(vm, "zm_trigger", NULL, 0);
	{
	if (r & ZM_EVENT_STOP)
		zm_fatalDo(ZM_FATAL_GCODE, "TRIGWRFL.ST",
		           "cannot use ZM_EVENT_STOP in trigger "
		           "pre-fetch");
	else
		zm_fatalDo(ZM_FATAL_GCODE, "TRIGWRFL.UF",
		           "invalid return in trigger callback use "
		           "ZM_EVENT_ACCEPTED or ZM_EVENT_REFUSED");
	}
}


static int zm_triggerEVB(zm_VM *vm, zm_EventBinder *evb, void *arg)
{
	zm_Event *event = evb->event;
	zm_State *s = evb->owner;
	int r = ZM_EVENT_ACCEPTED;

	if (event->evcb) {
		ZM_D("zm_trigger: cb(state = [ref %zx])", s);

		r = event->evcb(ZM_EVENT_TRIGGER, event, s, arg);

		/* event trigger can return ZM_EVENT_ACCEPT or ZM_EVENT_REFUSE:
		   if the event is accepted the relative task will be resumed
		   otherwise task still wait. The trigger callback can modify
		   the argument but this modify affect only one resume.
		 */

		if (r & ZM_EVENT_REFUSED)
			return r;
	}

	zm_unbindEvent(vm, s, arg, ZM_EVENT_TRIGGER | ZM_EVENT_ACCEPTED);

	return r;
}


static int zm_trigger0(zm_VM *vm, zm_Event *event, void *arg)
{
	if (event->evcb) {
		ZM_D("zm_trigger0: call event-callback");
		return event->evcb(ZM_EVENT_TRIGGER, event, NULL, arg);
	}

	ZM_D("zm_trigger0: no event-callback");
	return ZM_EVENT_ACCEPTED;
}


/*
 * argument will be passed to trigger callback (if set) and as zmarg to
 * binded tasks that will accept this event
 */
size_t zm_trigger(zm_VM *vm, zm_Event *event, void *argument)
{
	zm_EventBinder *evb, *nextevb;
	int r, n, count = 0;

	ZM_D("zm_trigger: PRE-FETCH");

	/*** trigger pre-fetch ***/
	r = zm_trigger0(vm, event, argument);

	/* In pre-fetch ZM_EVENT_ACCEPTED and ZM_EVENT_REFUSE act as a filter
	   to accept the entire trigger action. Prefetch can modify
	   argument for successive call.
	 */
	switch (r) {
	case ZM_EVENT_ACCEPTED:
		break;

	case ZM_EVENT_REFUSED:
		return 0;

	default:
		zm_triggerWrongReturn(vm, r);
	}


	evb = event->bindlist;

	if (!evb)
		return 0;

	n = event->count;

	do {
		/*** trigger fetch ***/
		ZM_D("zm_trigger: fetch trigger %d", n);

		/* save evb->next because unbind free evb (note: evb is
		 * a ring linked list, when evb contain only one element
		 * nextevb == evb this can't be a problem because in this
		 * situation do-while end */
		nextevb = evb->next;

		r = zm_triggerEVB(vm, evb, argument);

		if (r & ZM_EVENT_ACCEPTED)
			count++;


		if (r & ZM_EVENT_STOP) /* stop fetching other task */
			break;

		evb = nextevb;
		ZM_D("zm_trigger: cycle end %d", n);
	} while (--n > 0);

	return count;
}


zm_Event* zm_newEvent(zm_event_cb callback, void *data)
{
	zm_Event *event = zm_alloc(zm_Event);

	event->bindlist = NULL;
	event->count = 0;
	event->evcb = callback;
	event->data = data;

	return event;
}




/*
 * Force unbind of one task from an event. If the event have TODO
 TODO TODO TODO
 with event-callback scope flag ZM_EVENT_UNBIND_FORCE
 * If task pointer `s` is NULL unbind all tasks and return the number of
 * unbinded task (equals to event->count).
 */
size_t zm_unbind(zm_VM *vm, zm_Event *event, zm_State* s, void *arg)
{
	if (s) {
		/* unbind task `s` */
		if (zm_hasntFlag(s, ZM_STATE_EVENTLOCKED))
			return 0;

		zm_unbindEvent(vm, s, arg, ZM_EVENT_UNBIND_FORCE);
		return 1;
	} else {
		/* unbind all tasks */
		size_t n = event->count;

		while(event->bindlist) {
			s = event->bindlist->owner;
			zm_unbindEvent(vm, s, arg, ZM_EVENT_UNBIND_FORCE);
		}

		#ifdef ZM_CHECK_CONSISTENCY
		if (event->count) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_U1, "UNBIND.STL",
				   "event counter is not 0 "
				   "after unbind all");
		}
		#endif

		return n;
	}
}


void zm_freeEvent(zm_VM *vm, zm_Event *event)
{
	if (event->count) {
		zm_fatalInit(vm, "zm_freeEvent");
		zm_fatalDo(ZM_FATAL_GCODE, "FREEEV.NE",
		           "try to free an event with some binded task");
	}


	if (event->evcb)
		event->evcb(ZM_EVENT_UNBIND_FORCE, event, NULL, NULL);


	zm_free(zm_Event, event);
}

/*
 * yield to event (listen event)
 */
zm_yield_t izmEVENT(zm_VM* vm, zm_Event *e, const char *filename, int nline)
{
	zm_State *s = zm_getCurrentState(vm);

	ZM_ASSERT_VMLOCK("LISTEV.VLCK", "zmEVENT", filename, nline);

	ZM_D("zm_listenEvent - event bind for state: [ref %zx]", s);

	if (s->flag & ZM_STATE_EVENTLOCKED) {
		/* TODO this is not an unexpected behaviour ?? */
		zm_fatalInitAt(vm, "zmEVENT", filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "LISTEV.1",
		           "this state is just associated to an event");
	}

	zm_bindEvent(e, s);

	return ZM_TASK_BUSY_WAITING_EVENT;
}


/* ----------------------------------------------------------------------------
 *  MACHINE & WORKER                                             (SECTION CORE)
 * --------------------------------------------------------------------------*/

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
	return (zm_Yield)n;
}
#endif


static void zm_machineInit(zm_Machine *machine)
{
	zm_lockOn(NULL);
	if (machine->id == -1)
		machine->id = zmg_mcounter++;
	zm_lockOff(NULL);
}


static zm_Yield zm_machineStep(zm_VM *vm, zm_Worker *worker, zm_State *s)
{
	zm_yield_t n;

	vm->plock = true;

	n = (worker->machine->fun)(vm, s->on.resume, s->rearg);

	vm->plock = false;

	s->rearg = NULL;

	return zm_r2Y(n);
}


static zm_Yield zm_machineStep0(zm_VM *vm, zm_Worker *worker, zm_State *s)
{
	zm_State *savestate = vm->session.state;
	zm_Worker *saveworker = vm->session.worker;
	int savelock = vm->plock;
	zm_yield_t n;

	/*if (sub) not sure about this FIXME
		zm_setCaller(s, zm_getCurrentState(vm)); */

	vm->plock = true;
	vm->session.worker = worker;
	vm->session.state = s;

	n = (worker->machine->fun)(vm, ZM_INIT, NULL);

	vm->session.state = savestate;
	vm->session.worker = saveworker;
	vm->plock = savelock;

	/* if (sub)
		zm_setCaller(s, NULL); */

	return zm_r2Y(n);
}


/*
 * MWH: Machine Worker Hashtable
 */
static void zm_mwhInit(zm_VM *vm)
{
	/* hlist in MT can be initalized too small (if others threads perform
	   many counter increments) anyway mwhAdd will resize it in a MT-safe
	   way */
	size_t len = zmg_mcounter + ZM_MACHINE_HLIST_INC;

	vm->mwh.len = len;

	vm->mwh.hlist = zm_nalloc(zm_Worker*, len);

	memset(vm->mwh.hlist, 0, len * sizeof(zm_Worker*));

}


static void zm_mwhFree(zm_VM *vm)
{
	zm_nfree(zm_Worker *, vm->mwh.len, vm->mwh.hlist);
}


static zm_Worker* zm_mwhGet(zm_VM *vm, zm_Machine *machine)
{
	/* uninitialized machine*/
	if (machine->id == -1)
		return NULL;

	/* chek if associative array must grow */
	if (machine->id >= vm->mwh.len)
		return NULL;

	return vm->mwh.hlist[machine->id];
}


static void zm_mwhGrow(zm_VM *vm, size_t len)
{
	size_t growed = (len - vm->mwh.len) * sizeof(zm_Worker*);

	vm->mwh.hlist = zm_nrealloc(vm->mwh.hlist, zm_Worker*, len);

	memset(vm->mwh.hlist + vm->mwh.len, 0, growed);

	vm->mwh.len = len;
}


static void zm_mwhSet(zm_VM *vm, zm_Machine *machine, zm_Worker *worker)
{
	if (machine->id == -1)
		zm_machineInit(machine);

	if (machine->id >= vm->mwh.len)
		zm_mwhGrow(vm, machine->id + ZM_MACHINE_HLIST_INC);

	vm->mwh.hlist[machine->id] = worker;
}


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
	zm_free(zm_Worker, w);
}


static zm_Worker* zm_getWorker(zm_VM *vm, zm_Machine *machine)
{
	zm_Worker *worker = zm_mwhGet(vm, machine);

	if (worker)
		return worker;

	worker = zm_newWorker(vm, machine);

	zm_mwhSet(vm, machine, worker);

	return worker;
}


/* ----------------------------------------------------------------------------
 *  TASK & SUBTASK                                               (SECTION CORE)
 * --------------------------------------------------------------------------*/


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

	if (zm_hasFlag(current, ZM_STATE_IMPLOSIONLOCK)) {
		zm_fatalInitAt(vm, ref, filename, nline);
		zm_fatalDo(ZM_FATAL_TCODE, "ADDTASK.P",
		           "cannot create task in ZM_TERM");
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


static void zm_runInit(zm_VM *vm, zm_Worker *worker, zm_State *s, int sub)
{
	zm_Yield y = zm_machineStep0(vm, worker, s);

	if (ZM_B4(y.cmd) != ZM_TASK_INIT) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YDONE.WY",
		           "task in constructor mode (ZM_INIT) can yield "
		           "only to zmDONE");
	}
}


zm_State* izm_addTask(zm_VM *vm, zm_Machine *machine, void *data, int sub,
                                         int flag, const char *fn, int nl)
{
	zm_State* state = zm_alloc(zm_State);
	zm_Worker *worker;

	ZM_D("zm_addTask %s: %s", sub ? "subtask" : "ptask", machine->name);

	state->pmode = ZM_PMODE_NORMAL;
	state->flag = flag;
	state->on.resume = ZM_FIRST;
	state->on.iter = 0;
	state->on.c4tch = 0;
	state->rearg = NULL;
	state->data = data;
	state->subtasks = NULL;
	state->exception = NULL;
	state->codeframe.filename = "<not set>";
	state->codeframe.nline = 0;
	#ifdef ZM_DEBUG_MACHINENAME
	state->debugmachinename = machine->name;
	#endif

	worker = zm_getWorker(vm, machine);

	if (sub) {
		const char *fname = (flag & ZM_STATE_AUTOFREE) ?
		                    "zmNewSubTasklet" : "zmNewSubTask";
		zm_State *current = zm_getCurrentState(vm);;

		ZM_ASSERT_VMLOCK("ADDTASK.VLCK", fname, fn, nl);

		zm_addParent(vm, state, fname, fn, nl);

		zm_addStateToSiblingsRing(&(current->subtasks), state);
	} else {
		state->parent = NULL;

		zm_addStateToSiblingsRing(&(vm->ptasks), state);

		vm->nptask++;
	}

	/* task and subtask are created suspended */
	state->next = (zm_State*)worker;

	zm_runInit(vm, worker, state, sub);

	ZM_D("zm_addTask = %zx", state);

	return state;
}


/**
 * If the task is ready to be free the request can be performed in a
 * sync way and the function return true otherwise the function return
 * false
 */
static int zm_requestFreeState(zm_VM *vm, zm_State *state, const char* refname)
{
	ZM_D("request free state by: %s state=%zx", refname, state);

	if (zm_hasFlag(state, ZM_STATE_AUTOFREE)) {
		zm_fatalInit(vm, refname);
		zm_fatalDo(ZM_FATAL_GCODE, "RFREEST.AF",
		           "cannot request to free a state "
		           "that is just marked to be free");
	}

	if (state->pmode == ZM_PMODE_OFF) {
		/*** this pmode is set by ZM_PMODE_END */
		/*** then is possible to free state in a sync way*/
		zm_free(zm_State, state);
		return true;
	}

	if (zm_hasntFlag(state, ZM_STATE_IMPLOSIONLOCK)) {
		zm_fatalInit(vm, refname);
		zm_fatalDo(ZM_FATAL_GCODE, "RFREEST.AT",
		           "cannot free an active task");
	}

	/* if ZM_STATE_IMPLOSIONLOCK the state will be closed but
	 * is not just closed because pmode != ZM_PMODE_OFF
	 * ==> set autofree flag to perform an async free */
	zm_enableFlag(state, ZM_STATE_AUTOFREE);

	return false;
}


int zm_freeTask(zm_VM *vm, zm_State *state)
{
	/* #FREE_TASK_KIND */
	if (zm_isSubTask(state)) {
		zm_fatalInit(vm, "zm_freeTask");
		zm_fatalDo(ZM_FATAL_GCODE, "FREET.S",
		           "expected a ptask but found a subtask");
	}

	return zm_requestFreeState(vm, state, "zm_freeTask");
}


int zm_freeSubTask(zm_VM *vm, zm_State *state)
{
	/* #FREE_TASK_KIND */
	if (zm_isTask(state)) {
		zm_fatalInit(vm, "zm_freeSubTask");
		zm_fatalDo(ZM_FATAL_GCODE, "FREES.T",
		           "expected a subtask but found a ptask");
	}

	return zm_requestFreeState(vm, state, "zm_freeSubTask");
}


int zm_freeState(zm_VM *vm, zm_State *state)
{
	return zm_requestFreeState(vm, state, "zm_freeState");
}


/*
 * yield to a subtask (inside-task yield-operator)
 * alias: zmSUB, zmSSUB
 */
zm_yield_t izmSUB(zm_VM* vm, zm_State *s, void* argument, int allowunraise,
                                           const char *filename, int nline)
{
	const char *rn = (allowunraise) ? "zmSSUB" : "zmSUB";

	ZM_ASSERT_VMLOCK("ACTSUB.VLCK", rn, filename, nline);

	ZM_D("yield SUB(state: [ref %zx])\n", s);

	if (zm_isTask(s)) {
		zm_fatalInitAt(vm, rn, filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.NS",
		           "expected a subtask but found a task "
		           "(use instead: zm_resume)");

	}

	if (zm_hasFlag(s, ZM_STATE_IMPLOSIONLOCK)) {
		zm_fatalInitAt(vm, rn, filename, nline);
		zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.LI",
		           "try to resume a closed subtask");
	}

	zm_canBeContextStackPush(vm,  s, rn, filename, nline);

	if (zm_hasFlag(s, ZM_STATE_WAITING)) {
		if (zm_hasException(s, ZM_EXCEPTION_CONTINUEHEAD)) {
			if (!allowunraise) {
				zm_fatalInitAt(vm, rn, filename, nline);
				zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.C",
						   "try to resume a subtask "
						   "with a continue-exception "
						   "(use instead zmUNRAISE or "
						   "zmSSUB)");
			}

			zm_unraise(vm, s, argument, "zmSSUB", filename, nline);

			return ZM_TASK_SUSPEND_WAITING_SUBTASK;
		}

		if (zm_getParent(zm_getCurrentState(vm)) == s) {
			zm_fatalInitAt(vm, rn, filename, nline);
			zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.WP",
			                 "try to resume a subtask that is the "
			                 "previous element in execution-stack; "
			                 "use instead zmyield zmCALLER");
		} else if (allowunraise) {
			if (zm_hasException(s, ZM_EXCEPTION_CONTINUE)) {
				zm_fatalInitAt(vm, rn, filename, nline);
				zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.WT",
					   "try to resume a waiting subtask or "
					   "unraise the continue-exception "
					   "tail");
			} else {
				zm_fatalInitAt(vm, rn, filename, nline);
				zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.WR",
					   "try to resume a waiting subtask "
					   "or unraise a subtask without "
					   "continue-exception");
			}
		} else {
			zm_fatalInitAt(vm, rn, filename, nline);
			zm_fatalDo(ZM_FATAL_YCODE, "ACTSUB.WS",
					   "try to resume a waiting subtask");
		}
	}

	zm_setCaller(s, zm_getCurrentState(vm));

	zm_resumeStateBy(vm, s, argument, rn, filename, nline);

	return ZM_TASK_SUSPEND_WAITING_SUBTASK;
}


/*
 *
 */
zm_yield_t izm_resume(const char *fname, zm_VM* vm, zm_State *s, void *argument,
                                      int iter, const char *filename, int nline)
{
	if (zm_hasFlag(s, ZM_STATE_RUN | ZM_STATE_WAITING)) {
		if (zm_hasFlag(s, ZM_STATE_RUN)) {
			zm_fatalInitAt(vm, fname, filename, nline);
			zm_fatalDo(ZM_FATAL_GCODE, "ACTTASK.R",
			           "try to active a just active task");
		} else {
			zm_fatalInitAt(vm, fname, filename, nline);
			zm_fatalDo(ZM_FATAL_GCODE, "ACTTASK.WS",
				   "try to resume a busy-waiting task");
		}
	}

	if (zm_isSubTask(s)) {
		zm_fatalInitAt(vm, fname, filename, nline);
		zm_fatalDo(ZM_FATAL_GCODE, "ACTTASK.NP",
		           "expected a task but found a subtask "
		           "(use instead: zmSUB)");
	}


	/* FIXME not sure is still right after remove zmTO */
	if (iter) {
		/*  [ON_RESUME_SWITCH]*/
		if (s->on.iter) {
			s->on.resume = s->on.iter;
		}
	}

	ZM_D("activeTask (%s): iter = %d", fname, iter);

	zm_resumeStateBy(vm, s, argument, fname, filename, nline);

	if (iter)
		return ZM_TASK_SUSPEND;

	return ZM_TASK_TERM;
}






/* ----------------------------------------------------------------------------
 *  VIRTUAL MAPPER                                               (SECTION CORE)
 * --------------------------------------------------------------------------*/

zm_VM* zm_newVM(const char *name)
{
	zm_VM* vm;

	vm = zm_alloc(zm_VM);

	vm->data = NULL;

	vm->plock = false;
	vm->pause = false;

	vm->prepost = NULL;
	/** ptasks contain a pointer to a ptask of the vm or NULL when empty */
	/** all ptask are connected througth siblings so this pointer allow*/
	/** to access all the task (and relative subtask) of the vm*/
	vm->ptasks = NULL;

	vm->nptask = 0;
	vm->nworker = 0;

	zm_mwhInit(vm);

	vm->workercursor = NULL;
	vm->session.state = NULL;
	vm->session.worker = NULL;
	vm->session.fixedworker = false;
	vm->session.suspendop = 0;

	vm->name = name;
	vm->uncaught = NULL;

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
		/* can be replaced with ZM_ASSERT_VMUNLOCK TODO */
		zm_fatalInit(vm, "zm_closeVM");
		zm_fatalDo(ZM_FATAL_TCODE, "CLSVM.LCK",
		           "cannot invoke a closeVM during task execution");
	}

	do {
		#ifdef ZM_CHECK_CONSISTENCY
		if (!state->siblings.next) {
			zm_fatalInit(vm, "zm_closeVM");
			zm_fatalDo(ZM_FATAL_U1, "CLSVM.SN",
			           "null ref in siblings ring");
		}
		n--;
		#endif

		zm_abortTask(vm, state, "zm_closeVM");
		state = state->siblings.next;
	} while (state != vm->ptasks);

	#ifdef ZM_CHECK_CONSISTENCY
	if (n != 0) {
		zm_fatalInit(vm, "zm_closeVM");
		zm_fatalDo(ZM_FATAL_U1, "CLSVM.SC",
		           "siblings count doesn't match");
	}
	#endif


	return false;
}



void zm_freeVM(zm_VM* vm)
{
	int i;
	if (vm->ptasks) {
		zm_fatalInit(vm, "zm_freeVM");
		zm_fatalDo(ZM_FATAL_GCODE, "FREEVM.TP",
		           "cannot free vm if all task hasn't been "
		           "free (use zm_closeVM and wait until "
		           "zm_go return ZM_RUN_IDLE)");
	}


	for (i = 0; i < vm->mwh.len; i++) {
		zm_Worker *worker = vm->mwh.hlist[i];
		if (worker) {
			#if ZM_CHECK_CONSISTENCY
			if (worker->nstate) {
				zm_fatalInit(vm, NULL);
				zm_fatalDo(ZM_FATAL_U1, "FREEVM.NS",
					   "worker '%s' is not empty",
					   zm_workerName(worker));
			}
			#endif

			zm_freeWorker(vm, worker);
		}
	}

	zm_mwhFree(vm);

	zm_free(zm_VM, vm);
}


void zm_break(zm_VM* vm)
{
	vm->pause = true;
}


void zm_setProcessCallback(zm_VM *vm, zm_process_cb p)
{
	vm->prepost = p;
}


/* ----------------------------------------------------------------------------
 *  PROCESS                                                      (SECTION CORE)
 * --------------------------------------------------------------------------*/


static void zm_checkInnerYield(zm_VM *vm, zm_State *state, zm_Yield result)
{
	if (result.resume == 0) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YINN.0",
		           "yield to zmstate 0");
	}

	if (result.c4tch != 0) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YINN.C",
		           "catch in yield can be set only with "
		           "zmSUB(state)");
	}

	if (result.iter != 0) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YINN.I",
		           "iter in yield can be set only with "
		           "zmSUB(state)");
	}

	if (result.resume == ZM_TERM) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YINN.TRM",
		           "task cannot yield directly to ZM_TERM "
		           "use instead: zmTERM");
	}
}


static void zm_resumeSubtaskCaller(zm_VM *vm, zm_State *sub, zm_Yield result,
                                                             const char *ref)
{

	/* only resume can be set in yield parent */

	if (result.resume == 0) {
		zm_fatalInit(vm, ref);
		zm_fatalDo(ZM_FATAL_YCODE, "YRESCALL.R",
		           "resume point must be set in "
		           "yield %s", ref);
	}

	/* CHECK_OR_NOT check or reset ?? */
	if (result.c4tch != 0) {
		int c = zm_hasFlag(sub, ZM_STATE_CATCH);
		zm_fatalInit(vm, ref);
		zm_fatalDo(ZM_FATAL_YCODE, "YRESCALL.C",
		           "%s and %s cannot be used together in a zmyield",
		           ((c) ? "zmCATCH" :"zmRESET"), ref);
	}

	if (result.iter != 0) {
		zm_fatalInit(vm, ref);
		zm_fatalDo(ZM_FATAL_YCODE, "YRESCALL.I",
		           "zmNEXT and %s cannot be used together in a zmyield",
		           ref);
	}

	/* running active subtasks should always have caller */
	assert(zm_hasCaller(sub));

	/* suspend current state (child)*/
	zm_suspendByYield(vm, result, false);

	/* resume caller: iter mode */
	zm_resumeCaller(vm, sub, true);
}


static void zm_freeUnlockedException(zm_VM *vm, zm_Exception *e)
{
	ZM_D("runState - free exception... %zx", e);
	if (e->elock == ZM_ELOCK_REUSE) {
		e->elock = ZM_ELOCK_ON;
		return;
	}

	if (e->elock == ZM_ELOCK_ON) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_TCODE, "YNOCATHC.1",
			   "exception not catched in the "
			   "catch state (use zmCatch)");
	}

	if (e->kind == ZM_EXCEPTION_ABORT)
		zm_freeTrace(e);

	zm_free(zm_Exception, e);

	ZM_D("runState - free exception...free");
}


static zm_Yield zm_runTask(zm_VM *vm, zm_Worker *worker, zm_State *state)
{
	zm_Exception *checkexcept = NULL;
	zm_Yield y;


	ZM_D("runState - begin");

	/* check for exception and catch */
	if (zm_hasFlag(state, ZM_STATE_CATCH)) {
		zm_disableFlag(state, ZM_STATE_CATCH);

		if (state->exception) {
			checkexcept = state->exception;

			/* resume on catch #EXCEPT_WORKFLOW */
			if (state->on.resume != ZM_TERM)
				state->on.resume = state->on.c4tch;
		}
	}


	/* in run mode a resume point must always have to been set */
	if (state->on.resume == 0) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YRES.0",
		           "resume zmstate is set to 0 (possible cause: "
		           "last zmyield hasn't define the "
		           "proper resume point)");
	}

	ZM_D("runState: (resume = %d) machine: %s", state->on.resume,
	     zm_getCurrentMachineName(vm));

	y = zm_machineStep(vm, worker, state);

	ZM_D("runState: resume: %d iter: %d catch: %d cmd: %d",
	     y.resume, y.iter, y.c4tch, y.cmd);

	if (!checkexcept)
		return y;

	/* free exception if is unlock otherwise go fatal */
	zm_freeUnlockedException(vm, checkexcept);

	return y;
}


/*
 * process normal mode yield
 */
static int zm_normalYield(zm_VM *vm, zm_Worker *worker, zm_State *state,
                                                        zm_Yield result)
{
	int cmd =  ZM_B4(result.cmd);

	switch( cmd ) {

	case ZM_TASK_CONTINUE:
		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_CONTINUE");

		zm_checkInnerYield(vm, state, result);

		state->on.resume = result.resume;

		return 0;

	case ZM_TASK_RAISE_CONTINUE_EXCEPTION: {
		zm_Exception *e = state->exception;
		zm_State *head, *catcher;

		/* #CONTINUE_EXCEPT*/

		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_RAISE_CONTINUE");

		head = zm_getLastBeforeCatch(vm, state);

		if (!head)
			zm_fatalUncaughtContinue(vm, state, e);

		/* remove exception from raise state */
		state->exception = NULL;

		/* store head reference inside exception for user (zmContinueHead) */
		e->beforecatch = head;

		/** put exception in catch state */
		catcher = zm_caller(head);
		catcher->exception = e;

		/** create an exception reference to allow unraise  */
		head->exception = zm_newException( ZM_EXCEPTION_CONTINUEHEAD );
		head->exception->raisestate = state;
		head->exception->beforecatch = head;

		/** close the continue head-tail block (see zm_checkBusy) */
		zm_setCaller(head, NULL);

		/** suspend (busy) the raiser and resume the catcher */
		zm_suspendByYield(vm, result, true);
		zm_resumeState(vm, catcher);

		return ZM_PROCESS_STATEUNLINKED;
	}


	case ZM_TASK_RAISE_ABORT_EXCEPTION: {
		zm_Exception *e = state->exception;

		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_RAISE_ERROR");

		/* remove reference from raise state */
		state->exception = NULL;

		/* zmRESET in implicit mode */
		if ((!result.c4tch) && (result.resume)) {
			/* transform implicit reset in explicit one
			 * NOTE: raise and drop are allowed only in
			 * subtask so currentstate is a subtask */
			result.c4tch = result.resume;
		}

		zm_suspendByYield(vm, result, true);

		zm_lockAndImplodeByAbortException(vm, state, e);

		/* TODO is not necessary uncaught field in vm because uncaught
		 * == current state (remove it?) */
		if (vm->uncaught)
			return ZM_PROCESS_EXCEPTION | ZM_PROCESS_STATEUNLINKED;

		return ZM_PROCESS_STATEUNLINKED;
	}


	/** distructor of the task  */
	case ZM_TASK_TERM:
		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_TERM");

		zm_suspendByYield(vm, result, true);

		zm_lockAndImplodeBy(vm, state, ZM_IMPLODEBY_CUR, "yield zmTERM",
		                    state->codeframe.filename,
		                    state->codeframe.nline);

		return ZM_PROCESS_STATEUNLINKED;

	case ZM_TASK_END:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YNORM.YE",
			   "yield zmEND is permitted only in closing mode "
			   "inside ZM_TERM zmstate");
		return 0;

	/** Task Suspend - e.g. yield TO(foo) */
	case ZM_TASK_SUSPEND:
		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_SUSPEND");

		if (zm_isTask(state))
			zm_suspendByYield(vm, result, false);
		else
			zm_resumeSubtaskCaller(vm, state, result, "zmSUSPEND");

		return ZM_PROCESS_STATEUNLINKED;

	/** Task suspend (iter) - e.g. yield zmCALLER */
	case ZM_TASK_SUSPEND_AND_RESUME_CALLER:
		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_SUSPEND_AND_RESUME_CALLER");

		if (zm_isTask(state)) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_YCODE, "YSUSPEND.CALL",
				   "yield zmCALLER can be invoked only in "
				   "subtask (ptask never has a caller)");
		} else {
			zm_resumeSubtaskCaller(vm, state, result, "zmCALLER");
		}

		return ZM_PROCESS_STATEUNLINKED;

	/** Task suspend waiting subtask - e.g. yield SUB(foo) */
	case ZM_TASK_SUSPEND_WAITING_SUBTASK:
		ZM_D("ZM_PMODE_NORMAL | ZM_TASK_SUSPEND_WAIT_SUB");
		/* suspend with waiting = true (ZM_STATE_WAITING) */
		zm_suspendByYield(vm, result, true);

		return ZM_PROCESS_STATEUNLINKED;


	/** Task suspend waiting event - e.g. yield EVENT(...)*/
	case ZM_TASK_BUSY_WAITING_EVENT: {
		/* zmEVENT has just:
		 * - set flag ZM_STATE_EVENTLOCKED #EVB_FLAG
		 * - set state->next to an eventbinder instance pointer
		 *   #EVENT_BIND
		 */
		zm_EventBinder* evb = (zm_EventBinder*)state->next;

		/* temporary recover state->next to perform
		 * suspendCurrentState */
		state->next = (zm_State*)evb->statenext;

		ZM_D("ZM_PMODE_NORMAL | TASK_BUSY_WAITING_EVENT");

		/* Temporary disable event flag to allow debug print in
		   suspend current */
		zm_disableFlag(state, ZM_STATE_EVENTLOCKED);

		/* suspend with waiting = true (ZM_STATE_WAITING) */
		zm_suspendByYield(vm, result, true);

		zm_enableFlag(state, ZM_STATE_EVENTLOCKED);

		/* save worker pointer in (suspend put worker in state->next */
		/* to allow resume operation) to be recover after unbind */
		evb->statenext = state->next;

		/* replace again next state with evb */
		state->next = (zm_State*)evb;

		return ZM_PROCESS_STATEUNLINKED;
	}

	default:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_UP, "WCMOP.U", "unknow yield directive %d",
		           result.cmd);
		return 0;
	}
}


/*
 * process close mode yield
 */
static int zm_closeYield(zm_VM *vm, zm_Worker *worker, zm_State *state,
                                                       zm_Yield result)
{
	int cmd = ZM_B4(result.cmd);

	switch( cmd ) {

	case ZM_TASK_END:
		ZM_D("ZM_PMODE_CLOSE | ZM_TASK_END");
		/*  CHECK_OR_NOT */

		if (zm_hasntFlag(state, ZM_STATE_IMPLOSIONLOCK)) {
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_UP, "YEND.NLI",
			           "task not implode-locked in close mode");
		}

		state->pmode = ZM_PMODE_END;

		return 0;

	default:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_YCODE, "YEND.WY",
		           "in ZM_TERM only yield zmEND is permitted"
		           );

		return 0;
	}
}


static int zm_processTask(zm_VM *vm, zm_Worker *worker, zm_State *state)
{
	ZM_D("process_state[0] - state = [ref %zx]  pmode = %d", state,
	     state->pmode);

	switch(state->pmode) {
	case ZM_PMODE_NORMAL:
	case ZM_PMODE_CLOSE: {
		/* RUN: Excute a step of machine */
		zm_Yield y = zm_runTask(vm, worker, state);

		if (state->pmode == ZM_PMODE_CLOSE)
			return zm_closeYield(vm, worker, state, y);
		else
			return zm_normalYield(vm, worker, state, y);
	}

	case ZM_PMODE_END:
		/* Remove the state from list (should be invoked in
		 * ZM_TERM when all user-resource as been free) */

		ZM_D("ZM_PMODE_END:");

		if (zm_isSubTask(state)) {
			/* resume parent (end mode) done before
			 * unlinkCurrentState to avoid unuseful
			 * unlink/add worker when state and parent
			 * have same worker */
			if (zm_hasCaller(state)) {
				zm_resumeCaller(vm, state, false);
			}
		}

		zm_unlinkCurrentState(vm);

		state->pmode = ZM_PMODE_OFF;

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

		if (zm_hasException(state, ZM_EXCEPTION_CONTINUEHEAD)) {
			zm_free(zm_Exception, state->exception);
			state->exception = NULL;
		} else if (state->exception) {
			/* If there is alredy an exception with
			 * kind != continueref ... something has going wrong */
			zm_fatalInit(vm, NULL);
			zm_fatalDo(ZM_FATAL_UP, "PPS.EEN",
			           "exception still present in ZM_PMODE_END");
		}

		if (zm_hasFlag(state, ZM_STATE_AUTOFREE)) {
			ZM_D("CLOSE TASK: free state");
			zm_free(zm_State, state);
		}

		/** remove state from vm (no more executed)*/
		return ZM_PROCESS_STATEUNLINKED;


	case ZM_PMODE_ASYNCIMPLODE: {
		/* this is a special for lock and implode
		   #ASYNC_SERIALIZATION [step 3]*/
		zm_State *imstart;

		ZM_D("ZM_PMODE_ASYNCIMPLODE");
		imstart = zm_popAsyncImplosionStart(state);

		state->pmode = ZM_PMODE_CLOSE;

		/* All implosion task, except the imstart, must be in
		 * waiting subtask (see #IMPLODE_WAITING_CHAIN)
		 * suspend with waiting = true (ZM_STATE_WAITING) */
		zm_suspendCurrentState(vm, true);

		ZM_D("star implosion from: %zx", imstart);

		zm_resumeState(vm, imstart);

		return ZM_PROCESS_STATEUNLINKED;
	}

	case ZM_PMODE_OFF:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_UP, "PPS.NMTD",
		           "pmode = ZM_PMODE_OFF in processState");
		return 0;

	default:
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_UP, "PPS.UVMOP",
		           "unknow pmode = %d in processState",
		           state->pmode);
		return 0;
	}
}


static int zm_goStep(zm_VM* vm, zm_Worker *worker, zm_State *state)
{
	int r;

	ZM_D("stateGo: process state with worker = %s", worker->machine->name);
	ZM_D("stateGo: process state [ref %zx] ", state);

	vm->session.state = state;
	vm->session.worker = worker;
	vm->session.suspendop = 0;

	if (vm->prepost)
		vm->prepost(vm, worker->machine, state, 0);

	r = zm_processTask(vm, worker, state);

	if (vm->prepost)
		vm->prepost(vm, worker->machine, state, 1);

	ZM_D("stateGo - process state return %d", r);

	/* state unlink cause an implicit cursor move */
	if (!(r & ZM_PROCESS_STATEUNLINKED))
		zm_stateNext(worker);

	r = (r & ZM_PROCESS_EXCEPTION) ? ZM_RUN_EXCEPTION : ZM_RUN_IDLE;

	if (!vm->session.fixedworker)
		/* in non fixedworker mode idle is returned only by zm_go
		   when there are no more worker avaible */
		r = r | ZM_RUN_AGAIN;
	else if (worker->nstate > 0)
		/* in fixedworker mode idle is returned when nstate == 0 */
		r = r | ZM_RUN_AGAIN;

	return r;
}


static zm_Worker* zm_goGetWorker(zm_VM* vm)
{
	zm_Worker *worker;

	if (vm->session.fixedworker) {
		worker = vm->session.worker;
	} else {
		/* get current cursor worker */
		if (!vm->workercursor)
			return NULL;

		worker = vm->workercursor;
	}


	#ifdef ZM_CHECK_CONSISTENCY
	if (worker->nstate < 0) {
		zm_fatalInit(vm, NULL);
		zm_fatalDo(ZM_FATAL_U1, "WGO.WNS", "worker->nstate = %d",
		           worker->nstate);
	}
	#endif

	/* check if there a least one state */
	if (worker->nstate == 0) {
		/* no more state in this worker */
		ZM_D("zm_go: no more state in worker -> IDLE");
		return NULL;
	}

	return worker;
}


static zm_State* zm_goGetState(zm_VM* vm, zm_Worker* worker)
{
	if (!worker->states.current) {
		zm_rewindWorkerStates(vm, worker);

		if ((!vm->session.fixedworker) && (vm->nworker > 1)) {
			worker = zm_nextWorker(vm);
			/* return null to check worker with goGetWorker */
			return NULL;
		}
	}

	return worker->states.current;
}


int zm_go(zm_VM* vm, unsigned int ncycle, zm_Machine* onemachine)
{
	zm_Worker* worker;
	zm_State* state;
	int r;

	ZM_D("GO - init: vm = %d - machine = %zx", vm, onemachine);

	if (onemachine) {
		vm->session.worker = zm_getWorker(vm, onemachine);
		vm->session.fixedworker = true;
	} else {
		vm->session.fixedworker = false;
	}


	while(ncycle > 0) {
		ZM_D("GO - ********** STEP #%d **********", ncycle);

		if (vm->pause) {
			vm->pause = false;
			return ZM_RUN_AGAIN | ZM_RUN_BREAK;
		}

		worker = zm_goGetWorker(vm);

		if (!worker)
			return ZM_RUN_IDLE;

		state = zm_goGetState(vm, worker);

		if (!state)
			continue;

		r = zm_goStep(vm, worker, state);

		if (r != ZM_RUN_AGAIN)
			return r;

		#if ZM_DEBUG_LEVEL >= 4
		zm_printVM(NULL, vm);
		#endif

		ncycle -= worker->cyclestep;
	}

	ZM_D("GO - end (ncycle left %d)\n", ncycle);
	return ZM_RUN_AGAIN;
}

