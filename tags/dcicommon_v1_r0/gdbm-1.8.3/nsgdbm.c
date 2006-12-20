/*
 * nsgdbm.c --
 *
 *	Routines for thread-safe compatibility with AOLserver.
 */

#include "autoconf.h"
#include "gdbmdefs.h"
#include "gdbmerrno.h"
#ifdef gdbm_malloc
#undef gdbm_malloc
#endif
#ifdef gdbm_free
#undef gdbm_free
#endif

typedef struct _Ns_Tls *Ns_Tls;
extern void Ns_TlsAlloc(Ns_Tls *, void (*)(void *));
extern void Ns_TlsSet(Ns_Tls *, void *);
extern void *Ns_TlsGet(Ns_Tls *);
extern void Ns_MasterLock(void);
extern void Ns_MasterUnlock(void);
extern void *ns_malloc(size_t);
extern void ns_free(void *);


void *
gdbm_malloc(size_t n)
{
    return ns_malloc(n);
}


void
gdbm_free(void *ptr)
{
    ns_free(ptr);
}


gdbm_error *
gdbm_perrno(void)
{
    static Ns_Tls tls;
    gdbm_error *errPtr;

    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, gdbm_free);
	}
	Ns_MasterUnlock();
    }
    errPtr = Ns_TlsGet(&tls);
    if (errPtr == NULL) {
	errPtr = gdbm_malloc(sizeof(gdbm_error));
	*errPtr = GDBM_NO_ERROR;
	Ns_TlsSet(&tls, errPtr);
    }
    return errPtr;
}
