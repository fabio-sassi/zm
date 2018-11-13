/*
 * Copyright (c) 2015-2018, Fabio Sassi <fabio dot s81 at gmail dot com>
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

#ifndef __ZM_VM_H__
#define __ZM_VM_H__


#define ZM_VERSION "0.1.3"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifndef ZM_NO_STDINT_H
	#include <stdint.h>
#endif

#ifndef bool
	#define bool int
#endif

#ifndef false
	#define false 0
#endif

#ifndef true
	#define true 1
#endif



#define ZM_CHECK_CONSISTENCY 1

#define ZM_PRINTSTATE_MAXDEEP 100

#ifndef ZM_DEBUG_LEVEL
	#define ZM_DEBUG_LEVEL 0
#endif

#if ZM_DEBUG_LEVEL >= 3
	#define ZM_DEBUG_MACHINENAME
#endif


#if (!defined(ZM_LITTLE_ENDIAN)) && (!defined(ZM_BIG_ENDIAN))
	#define ZM_BYTEORDER_LE 1
	#define ZM_BYTE_ORDER_RUNTIME 1
#else
	#ifdef ZM_LITTLE_ENDIAN
		#define ZM_BYTEORDER_LE 1
	#else
		#define ZM_BYTEORDER_LE 0
	#endif
#endif



typedef uint32_t zm_yield_t; /* machine result */


#define ZM_MACHINE_HLIST_INC 8

extern size_t zmg_mcounter;


#define ZM_B4(x) ((x) << 24)
#define ZM_B3(x) ((x) << 16)
#define ZM_B2(x) ((x) << 8)


enum { /* * yield-command * */
	/* implicit or explicit */
	ZM_TASK_CONTINUE = ZM_B4(0),

	/* implicit (macro TO) or explicit */
	ZM_TASK_SUSPEND = ZM_B4(1),

	/* implicit (macro SUB) */
	ZM_TASK_SUSPEND_WAITING_SUBTASK = ZM_B4(2),

	/* explicit with shortcut in zmstate ZMTERM - yield zmEND */
	ZM_TASK_END = ZM_B4(3),

	/* explicit with shortcut - yield zmTERM */
	ZM_TASK_TERM = ZM_B4(4),

	/* explicit with shortcut - yield zmCALLER - for iterator */
	ZM_TASK_SUSPEND_AND_RESUME_CALLER = ZM_B4(5),

	/* implicit (macro zmEVENT) */
	ZM_TASK_BUSY_WAITING_EVENT = ZM_B4(6),

	/* implicit macro zmCONTINUE */
	ZM_TASK_RAISE_CONTINUE_EXCEPTION = ZM_B4(7),

	/* implicit macro zmABORT */
	ZM_TASK_RAISE_ABORT_EXCEPTION = ZM_B4(8),

	ZM_TASK_INIT = ZM_B4(9)
};




enum {
	ZM_PMODE_NORMAL = 0,

	ZM_PMODE_CLOSE,

	ZM_PMODE_END,

	ZM_PMODE_OFF,

	ZM_PMODE_ASYNCIMPLODE
};




/* * Special zmstate * */
#define ZM_FIRST 1
#define ZM_INIT 254
#define ZM_TERM 255
#define ZM_RESERVED 251

/* *** State Flag *** */


/* bit: 1 - 0: suspended  1: running */
#define ZM_STATEFLAG_RUN 1

/* bit: 2 - 0: suspend - 1: waiting */
#define ZM_STATEFLAG_WAITING 2

/* bit: 3 - 1: locked by an event */
#define ZM_STATEFLAG_EVENTLOCKED 4

/* bit: 4 - state that catch an exception */
#define ZM_STATEFLAG_CATCH 8

/* bit: 5 - abort locked */
#define ZM_STATEFLAG_IMPLOSIONLOCK  16

/* bit: 6 - 0: manual-free 1: auto-free */
#define ZM_STATEFLAG_AUTOFREE  32

/* bit: 7 - continue exception mark */
#define ZM_STATEFLAG_CONTINUEMARK 64

/* bit: 8 - unused */
#define ZM_STATEFLAG_UNUSED 128




/* *** EVENT MODE *** */
#define ZM_EVENT_ACCEPTED         1 /* state will be resumed */
#define ZM_EVENT_REFUSED          0 /* state will not be resumed */
#define ZM_EVENT_STOP             32 /* stop eval other binded task */

#define ZM_EVENT_TRIGGER         256
#define ZM_EVENT_UNBIND_REQUEST  512
#define ZM_EVENT_UNBIND_ABORT    1024
#define ZM_EVENT_UNBIND   (ZM_EVENT_UNBIND_REQUEST | ZM_EVENT_UNBIND_ABORT)

#define ZM_TRIGGER                 ZM_EVENT_TRIGGER
#define ZM_UNBIND_REQUEST          ZM_EVENT_UNBIND_REQUEST
#define ZM_UNBIND_ABORT            ZM_EVENT_UNBIND_ABORT
#define ZM_UNBIND                  ZM_EVENT_UNBIND

/* **** RUN RETURN FLAG *** */
#define ZM_RUN_IDLE 0
#define ZM_RUN_AGAIN 1
#define ZM_RUN_EXCEPTION 2
#define ZM_RUN_BREAK 4


/* **** INTERNAL PROCESS MODE *** */
#define ZM_PROCESS_STATEUNLINKED 1
#define ZM_PROCESS_EXCEPTION 2


/* **** IMPLODE MODE *** */
#define ZM_IMPLODEBY_EXCEPTION 1
#define ZM_IMPLODEBY_ROOT 2
#define ZM_IMPLODEBY_SUB 3
#define ZM_IMPLODEBY_CUR 4

/* **** BUSY WAITING CHECK MODE *** */
#define ZM_WSCHECK_NONE 1
#define ZM_WSCHECK_ALL 2
#define ZM_WSCHECK_SKIPFIRST 3



typedef struct zm_VM_ zm_VM;

typedef struct zm_Exception_ zm_Exception;

typedef struct zm_Trace_ zm_Trace;

typedef struct zm_State_ zm_State;


typedef struct {
#if ZM_BYTEORDER_LE
		uint8_t resume;
		uint8_t c4tch;
		uint8_t iter;
		uint8_t cmd;
#else
		uint8_t cmd;
		uint8_t iter;
		uint8_t c4tch;
		uint8_t resume;
#endif
} zm_Yield;


typedef struct {
	size_t stacksize;
	zm_State **stack;
	zm_State *comeback;
} zm_Parent;


/* * State * */

struct zm_State_ {
	uint8_t flag;

	zm_Parent *parent;

	struct {
		zm_State *next;
		zm_State *prev;
	} siblings;

	struct {
		uint8_t resume;
		uint8_t c4tch;
		uint8_t iter;
	} on;

	uint8_t pmode;

	void *data;
	void *rearg; /* resume arguments */

	zm_State *subtasks;
	zm_Exception *exception;
	zm_State *next;

	#ifdef ZM_DEBUG_MACHINENAME
		const char* debugmachinename;
	#endif

	struct {
		const char *filename;
		size_t nline;
	} codeframe;
};






/* * State Linked List  * */

typedef struct zm_StateList_ zm_StateList;

struct zm_StateList_ {
	void *data;
	zm_State *state;
	zm_StateList *next;
};


typedef struct zm_StateQueue_ zm_StateQueue;

struct zm_StateQueue_ {
	zm_StateList *first;
	zm_StateList *last;
};


/* * Events * */

typedef struct zm_Event_ zm_Event;
typedef struct zm_EventBinder_ zm_EventBinder;

/* event callback (trigger and unbind) */
typedef int (*zm_event_cb)(zm_VM *vm,
                           int scope,
                           zm_Event *event,
                           zm_State *state,
                           void *argument);


struct zm_Event_ {
	uint32_t flag;

	zm_EventBinder *bindlist;
	size_t count;

	zm_event_cb evcb;

	void *data;
};


struct zm_EventBinder_ {
	zm_EventBinder *next; /* ring linked-list */
	zm_EventBinder *prev;

	void *statenext; /* contain state->next to be restore in unbind */
	zm_State *owner;
	zm_Event *event;
};



/* * Exception * */



struct zm_Trace_ {
	size_t taskid;
	zm_Trace *next;

	const char *machinename;
	const char *filename;
	int nline;
	int on; /* zmstate that raise the exception */
};


struct zm_Exception_ {
	int kind;
	int elock;

	/* ** user data*/
	int code;
	const char *msg;
	void *data;

	zm_State *beforecatch;

	zm_State *raisestate; /*used in continue #CONTINUE_EXCEPT */

	zm_Trace *etrace;
};


#define ZM_EXCEPTION_ABORT 1
#define ZM_EXCEPTION_CONTINUE 2
#define ZM_EXCEPTION_CONTINUE2 3
#define ZM_EXCEPTION_STARTIMPLOSION 4
#define ZM_EXCEPTION_UNCAUGHT 5


/* * Machine: avaible in global scope ** */

/*
 * A machine is like a symbol in global scope that rappresent a task class.
 * As a class, a machine allow to instance many task.  process is done
 * in 3 step:
 *
 * class definition: ZMTASKDEF(X) define a zm_Machine and a pointer X to it
 *                   to be used as class definition symbol.
 * task instance:    zm_newTask(vm, X, ...) create, only at the first
 *                   invocation with the class X, a zm_Worker associated to X.
 *                   This is the reference of X inside a vm (zm_VM) and will
 *                   be used in successive task instance procedure.
 *                   zm_newTask return an instance task (zm_State) associated
 *                   to the worker X and to vm.
 *
 * In a vm there is only one worker for each machine. An associative array
 * (mhw) inside vm allow to match a machine with relative zm_Worker.
 *
 * In this way user can use global machine symbol as the reference to
 * the relative zm_Worker instance (for example to instance new task).
*/
typedef struct {
	int id;
	zm_yield_t (*fun)(zm_VM* zm, uint8_t zmop, void *zmarg);
	const char* name;
} zm_Machine;

/* * Worker * */

typedef struct zm_Worker_ zm_Worker;

struct zm_Worker_ {
	unsigned int cyclestep;

	zm_Machine *machine;

	struct {
		/* point to the first state in the linked list */
		zm_State* first;
		zm_State* current;
		/* this is from Giulia (not real translable, it's a joke):
		 * mimi siocco sei e con lampur ambipur amico diventerai.*/
		zm_State* previous;
	} states;

	int nstate;

	zm_Worker *next;
	zm_Worker *prev;
};



/* * Virtual Mapper * */

/* callback: zm_process_cb */
typedef void (*zm_process_cb)(zm_VM* vm, zm_Machine* m, zm_State* s, int post);


struct zm_VM_ {
	uint8_t flag;

	const char *name;
	void *data;

	int plock;
	int pause;

	/* pre/post process state: vm, machine, state, ispost*/
	zm_process_cb prepost;

	zm_State *ptasks;
	size_t nptask;

	size_t nworker;

	struct {
		/* simple associative array: key=machine->id - value=worker*/
		zm_Worker **hlist;
		size_t len;
	} mhw;

	/* active workers pointer are stored in a ring linked list*/
	zm_Worker *workercursor;

	zm_Exception* uncaught;

	struct {
		zm_State *state;
		zm_Worker *worker;
		int fixedworker;
		int suspendop;
	} session;
};


/* * Print Utility * */


struct zm_Print_ {
	FILE *file;
	int indent;
	struct {
		char *data;
		size_t used;
		size_t size;
	} buffer;
};

typedef struct zm_Print_ zm_Print;


typedef struct {
	int by;
	int wscheck;

	size_t fromdeep;
	size_t todeep;
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
	ZM_FATAL_UN,
	/* unexpected error in process task */
	ZM_FATAL_UNP,
	/* generic error */
	ZM_FATAL_ERROR,
	/* generic user code error (inside task) */
	ZM_FATAL_UCODE,
	/* error using yield */
	ZM_FATAL_YIELD,
	/* sync error */
	ZM_FATAL_SYNC,
	/* uncaught */
	ZM_FATAL_NOCATCH
} zm_kfatal_t;


typedef void (*zm_tlock_cb)(void *data, int lock);

#define zm_hasFlag(s, FLAG)   ((s)->flag & (FLAG))
#define zm_hasntFlag(s, FLAG)   (((s)->flag ^ (FLAG)) & (FLAG))

/*---------------------------------------------------------------------------
 *  MACRO
 *  -----------------------------------------------------------------------*/


/* NOTE: zm_isSomething: only external library use.
         zm_hasFlag: internal use
 */
#define zm_isBusy(s)       zm_hasFlag(s, ZM_STATEFLAG_WAITING)
#define zm_isRunning(s)    zm_hasFlag(s, ZM_STATEFLAG_RUN)
#define zm_isntRunning(s)  zm_hasntFlag(s, ZM_STATEFLAG_RUN)
#define zm_isSuspended(s)  ((zm_isntRunning(s)) && (!zm_isBusy(s)))
#define zm_isAlive(s)      zm_hasntFlag(s, ZM_STATEFLAG_IMPLOSIONLOCK)
#define zm_isntAlive(s)    zm_hasFlag(s, ZM_STATEFLAG_IMPLOSIONLOCK)

#define zm_isTask(s)    ((s)->parent == NULL)
#define zm_isSubTask(s) ((s)->parent != NULL)

#define zm_newTask(vm, m, data) \
        izm_addTask((vm), (m), (data), false, 0,  __FILE__, __LINE__)

#define zm_newTasklet(vm, m, data)                                            \
        izm_addTask((vm), (m), (data), false, ZM_STATEFLAG_AUTOFREE,          \
                                                 __FILE__, __LINE__)

#define zm_resume(vm, x, arg) izm_resume("zm_resume", (vm), (x), (arg),       \
                                              true, __FILE__, __LINE__)

/* Inside Task API */

#define zmCatch()                                                             \
        izmCatch(vm, 0, "zmCatch", __FILE__, __LINE__)

#define zmCatchAbort()                                                        \
        izmCatch(vm, ZM_EXCEPTION_ABORT, "zmCatchAbort", __FILE__, __LINE__)

#define zmCatchContinue()                                                     \
        izmCatch(vm, ZM_EXCEPTION_CONTINUE, "zmCatchContinue",                \
                                           __FILE__, __LINE__)

#define zmNewSubTask(m, data)                                                 \
        izm_addTask((vm), (m), (data), true, 0, __FILE__, __LINE__)

#define zmNewSubTasklet(m, data)                                              \
        izm_addTask((vm), (m), (data), true, ZM_STATEFLAG_AUTOFREE,           \
                                                __FILE__, __LINE__)

#define zmNewSub zmNewSubTask
#define zmNewSu  zmNewSubTasklet

#define zmFreeSubTask(task) \
        zm_freeSubTask(vm, (task));

#define zmFreeSub zmFreeSubTask

/* ** task traversing ** */
#define zmParent(n)   izmGetParent(vm, (n), __FILE__, __LINE__)
#define zmDeep()      izmGetDeep(vm)
#define zmRoot()      izmGetRoot(vm)
#define zmCaller()    izmGetCaller(vm)

/* retrive data in task stack */
#define zmRootData(s)      ((s*)izmGetRootData(vm))
#define zmCallerData(s)    ((s*)izmGetCallerData(vm))
#define zmMachine()    (zm_getMachine(vm))
#define zmCurrent()    (zm_getCurrent(vm))



/* *** Inside Task Yield API *** */

/* ** exceptions ** */
#define zmABORT(ecode, msg, data)                                             \
        izmEXCEPTION(vm, true, (ecode), (msg), (void*)(data),                 \
                                          __FILE__, __LINE__)

#define zmCONTINUE(ecode, msg, data)                                          \
        izmEXCEPTION(vm, false, (ecode), (msg), (void*)(data),                \
                                           __FILE__, __LINE__)

#define zmDROP(e) izmDROP(vm, (e), __FILE__, __LINE__)

/* ** subtask ** */
#define zmSUB(x, arg)         izmSUB(vm, (x), (arg), false, __FILE__, __LINE__)
#define zmUNRAISE(s, arg)     izmUNRAISE(vm, (s), (arg), __FILE__, __LINE__)
#define zmSSUB(x, arg)        izmSUB(vm, (x), (arg), true, __FILE__, __LINE__)
#define zmCALLER              ZM_TASK_SUSPEND_AND_RESUME_CALLER
#define zmSU(task, data, arg) zmSUB(zmNewSu((task), (data)), (arg))


/* ** task ** */
#define zmSUSPEND    ZM_TASK_SUSPEND

/* ** event ** */
#define zmEVENT(e) (izmEVENT(vm,  (e), __FILE__, __LINE__))

/* ** close ** */
#define zmCLOSE(sub) izmCLOSE(vm, (sub), __FILE__, __LINE__)

/* ** special yield */
#define zmDONE       ZM_TASK_INIT
#define zmEND        ZM_TASK_END
#define zmTERM       ZM_TASK_TERM

/* ** resume modifier ** */
#define zmNEXT(x)    ZM_B3(x)
#define zmCATCH(n)   izmCATCH(vm, n)
#define zmRESET(n)   izmRESET(vm, n, __FILE__, __LINE__)
#define zmUNBIND(x)  ZM_B3(x)

/* Inside operator API */
#define zmyield    return zmyieldtrace(vm, __FILE__, __LINE__) |
#define zmraise    return 0 |
#define zmstate    case
#define zmdata     (zm_getCurrent(vm)->data)
#define zmresult   (izmResult(vm, __FILE__, __LINE__)->rearg)
#define zmpass     {}

/* Task def API*/
#define ZMTASKDEF(x)                                                          \
    zm_yield_t x ## __function__(zm_VM*, uint8_t zmop, void* zmarg);          \
    zm_Machine x ## __byval__ = {-1, x ## __function__, #x};                  \
    zm_Machine* x = &x ## __byval__;                                          \
    zm_yield_t (x ## __function__)(zm_VM* vm, uint8_t zmop, void *zmarg)      \
    {


#define ZMTASKLN(base, x)                                                     \
    zm_yield_t base ## __function__(zm_VM*, uint8_t, void*);           \
    zm_Machine x ## __byval__ = {-1, base ## __function__, #x};               \
    zm_Machine* x = &x ## __byval__                                           \




#define ZMSWITCHBEGIN switch(zmop) {

#define ZMSWITCHDEFAULT                                                       \
        default:                                                              \
            if ( zmop == ZM_TERM )                                            \
                zmyield zmEND;                                                \
            else if ( zmop == ZM_INIT )                                       \
                zmyield zmDONE;                                               \
                                                                              \
            zm_fatalUndefState(vm, __FILE__, __LINE__);                       \
            return 0;                                                         \


#define ZMAFTERSWITCH                                                         \
        zm_fatalOn(NULL, __FILE__, __LINE__);                                 \
        zm_fatalDo(ZM_FATAL_YIELD, "UNDEFBREAKSTATE.1", vm,                   \
                "break in main zmstate-switch (this happend using a "         \
                "`break` inside a zmstate in a wrong way)");                  \

#define ZMSWITCHEND                                                           \
        ZMSWITCHDEFAULT                                                       \
        }                                                                     \
        ZMAFTERSWITCH




/* taskdef / end - without graph parentesis syntax */
#define ZMSTATES ZMSWITCHBEGIN
#define ZMSTART ZMSWITCHBEGIN
#define ZMEND ZMSWITCHEND }

#define ZMSELF(t) t *self = (t*)zmdata;


/* memory utilty */
#define zm_alloc(s) ((s*)zm_malloc(sizeof(s), true))
#define zm_free(s, ptr) zm_mfree(sizeof(s), ptr)
#define zm_nalloc(s, n) ((s*)zm_malloc(sizeof(s) * (n), true))
#define zm_nrealloc(ptr, s, n) ((s*)zm_mrealloc((ptr), sizeof(s) * (n), true))
#define zm_nfree(s, n, ptr) zm_mfree(sizeof(s) * (n), ptr)

void *zm_malloc(size_t size, int memfatal);
void *zm_mrealloc(void *ptr, size_t size, int memfatal);
void zm_mfree(size_t size, void *ptr);


/* indent and buffer print utilty */
void zm_initPrint(zm_Print *p, FILE *stream, int indent, int b);
void zm_setIndent(zm_Print *out, int indent);
void zm_addIndent(zm_Print *out, int indent);
void zm_iprint(zm_Print *out, const char *fmt, ...);
void zm_print(zm_Print *out, const char *fmt, ...);
char* zm_popPrintBuffer(zm_Print *out, size_t *size);
void zm_removePrintBuffer(zm_Print *out);

/* report fatal utility */
typedef void (*zm_fatal_cb)(zm_VM *vm, char *msg, void *data);

void zm_fatalInit();
void zm_fatalOn(const char *refname, const char *fn, int nl);
void zm_fatalDo(zm_kfatal_t kind, const char *ecode, zm_VM *vm,
                                          const char *fmt, ...);
void zm_fatalUndefState(zm_VM *vm, const char *fn, int nl);
void zm_atFatal(zm_fatal_cb cb, void *data);

/* generic queue state */
zm_StateQueue* zm_queueNew();
void zm_queueFree(zm_StateQueue *q);
int zm_queueIsntEmpty(zm_StateQueue *queue);
int zm_queueIsEmpty(zm_StateQueue *queue);
zm_StateList* zm_queueAdd(zm_StateQueue* queue, zm_State *s, void *data);
zm_StateList* zm_queuePopStateList0(zm_StateQueue* queue);
zm_State* zm_queuePop0(zm_StateQueue* queue, void** data);

/* print vm structure */
void zm_printStateCompact(zm_Print *out, zm_State *s);
void zm_printState(zm_Print *out, zm_State *s);
void zm_printTasks(zm_Print *out, zm_VM *vm);
void zm_printVM(zm_Print *out, zm_VM *vm);
void zm_printTreeVM(zm_Print *out, zm_VM *vm);


/* yield operator */
zm_yield_t izmCATCH(zm_VM* vm, int n);
zm_yield_t izmRESET(zm_VM* vm, int n, const char* filename, int nline);

zm_yield_t izmCLOSE(zm_VM *vm, zm_State *state, const char *fn, int nl);
zm_yield_t izmDROP(zm_VM *vm, zm_Exception* e, const char *fn, int nl);
zm_yield_t izmEXCEPTION(zm_VM *vm, bool abort, int ecode, const char *msg,
                                       void *data, const char *fn, int nl);

zm_yield_t izmUNRAISE(zm_VM *vm, zm_State* state, void *argument,
                                          const char *fn, int nl);

zm_yield_t izmSUB(zm_VM* vm, zm_State *s, void*argument, bool allowunraise,
                                                    const char *fn, int nl);

zm_yield_t izmEVENT(zm_VM* vm, zm_Event *e, const char *fn, int nl);

int zmyieldtrace(zm_VM* vm, const char *fn, int nl);

/* inside task functions */

zm_State* izmResult(zm_VM* vm, const char *filename, int nline);

zm_Exception *izmCatch(zm_VM *vm, int ekindfilter, const char* ref,
                                      const char *fn, int nline);

zm_State* izmGetParent(zm_VM *vm, size_t n, const char *fn, int nl);

size_t izmGetDeep(zm_VM *vm);

zm_State* izmGetRoot(zm_VM *vm);

zm_State* izmGetCaller(zm_VM *vm);

void* izmGetRootData(zm_VM *vm);

void* izmGetCallerData(zm_VM *vm);


#define zmContinueBlock(e)                                                     \
       izmContinueBlock(vm, (e), __FILE__, __LINE__);

zm_State *izmContinueBlock(zm_VM* vm, zm_Exception *e, const char *fn, int nl);


/* event */
zm_Event* zm_newEvent(void *data);

void zm_setEventCB(zm_VM *vm, zm_Event* event, zm_event_cb cb, int scope);

void zm_freeEvent(zm_VM *vm, zm_Event *event);

size_t zm_trigger(zm_VM *vm, zm_Event *event, void *argument);

size_t zm_unbind(zm_VM *vm, zm_Event *event, zm_State* s, void *argument);

size_t zm_unbindAll(zm_VM *vm, zm_Event *event, void *argument);

/* functions */
zm_yield_t izm_resume(const char *fname, zm_VM* vm, zm_State *s, void *argument,
                                     int iter, const char *filename, int nline);

zm_State* izm_addTask(zm_VM *vm, zm_Machine *machine, void *data, bool sub,
                                      uint8_t flag, const char *fn, int nl);

int zm_freeTask(zm_VM *vm, zm_State *state);

#define zm_freeSub zm_freeSubTask

int zm_freeSubTask(zm_VM *vm, zm_State *state);

int zm_freeState(zm_VM *vm, zm_State *state);

void zm_abort(zm_VM *vm, zm_State *state);

size_t zm_getDeep(zm_State *s);

zm_State* zm_getCaller(zm_State *s);

zm_State* zm_getCurrent(zm_VM *vm);

zm_Machine* zm_getMachine(zm_VM *vm);

zm_Exception *zm_uCatch(zm_VM *vm);

void zm_uFree(zm_VM *vm, zm_Exception *e);

void zm_printTrace(zm_Print *out, zm_Exception *e);

void zm_printException(zm_Print *out, zm_Exception *e, int trace);


/* vm - virtual mapper */
zm_VM* zm_newVM(const char *name);
int zm_closeVM(zm_VM* vm);
void zm_freeVM(zm_VM* vm);
void zm_setProcessCallback(zm_VM *vm, zm_process_cb p);

/* multi thread support */
void zm_enableMT(zm_tlock_cb cb, void* data);

/* process */
void zm_break(zm_VM* vm);
#define zm_mGo zm_goMachine
int zm_goMachine(zm_VM* vm, zm_Machine* m, unsigned int ncycle);
int zm_go(zm_VM* vm, unsigned int ncycle);



#endif
