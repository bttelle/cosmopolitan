/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:4;tab-width:8;coding:utf-8 -*-│
│ vi: set et ft=c ts=4 sts=4 sw=4 fenc=utf-8                               :vi │
╞══════════════════════════════════════════════════════════════════════════════╡
│ Python 3                                                                     │
│ https://docs.python.org/3/license.html                                       │
╚─────────────────────────────────────────────────────────────────────────────*/
#include "libc/sock/syslog.h"
#include "libc/sysv/consts/log.h"
#include "third_party/python/Include/ceval.h"
#include "third_party/python/Include/import.h"
#include "third_party/python/Include/listobject.h"
#include "third_party/python/Include/longobject.h"
#include "third_party/python/Include/modsupport.h"
#include "third_party/python/Include/object.h"
#include "third_party/python/Include/osdefs.h"
#include "third_party/python/Include/pyerrors.h"
#include "third_party/python/Include/sysmodule.h"
#include "third_party/python/Include/tupleobject.h"
#include "third_party/python/Include/unicodeobject.h"
#include "third_party/python/Include/yoink.h"

PYTHON_PROVIDE("syslog");
PYTHON_PROVIDE("syslog.LOG_ALERT");
PYTHON_PROVIDE("syslog.LOG_AUTH");
PYTHON_PROVIDE("syslog.LOG_CONS");
PYTHON_PROVIDE("syslog.LOG_CRIT");
PYTHON_PROVIDE("syslog.LOG_CRON");
PYTHON_PROVIDE("syslog.LOG_DAEMON");
PYTHON_PROVIDE("syslog.LOG_DEBUG");
PYTHON_PROVIDE("syslog.LOG_EMERG");
PYTHON_PROVIDE("syslog.LOG_ERR");
PYTHON_PROVIDE("syslog.LOG_INFO");
PYTHON_PROVIDE("syslog.LOG_KERN");
PYTHON_PROVIDE("syslog.LOG_LOCAL0");
PYTHON_PROVIDE("syslog.LOG_LOCAL1");
PYTHON_PROVIDE("syslog.LOG_LOCAL2");
PYTHON_PROVIDE("syslog.LOG_LOCAL3");
PYTHON_PROVIDE("syslog.LOG_LOCAL4");
PYTHON_PROVIDE("syslog.LOG_LOCAL5");
PYTHON_PROVIDE("syslog.LOG_LOCAL6");
PYTHON_PROVIDE("syslog.LOG_LOCAL7");
PYTHON_PROVIDE("syslog.LOG_LPR");
PYTHON_PROVIDE("syslog.LOG_MAIL");
PYTHON_PROVIDE("syslog.LOG_MASK");
PYTHON_PROVIDE("syslog.LOG_NDELAY");
PYTHON_PROVIDE("syslog.LOG_NEWS");
PYTHON_PROVIDE("syslog.LOG_NOTICE");
PYTHON_PROVIDE("syslog.LOG_NOWAIT");
PYTHON_PROVIDE("syslog.LOG_ODELAY");
PYTHON_PROVIDE("syslog.LOG_PERROR");
PYTHON_PROVIDE("syslog.LOG_PID");
PYTHON_PROVIDE("syslog.LOG_SYSLOG");
PYTHON_PROVIDE("syslog.LOG_UPTO");
PYTHON_PROVIDE("syslog.LOG_USER");
PYTHON_PROVIDE("syslog.LOG_UUCP");
PYTHON_PROVIDE("syslog.LOG_WARNING");
PYTHON_PROVIDE("syslog.closelog");
PYTHON_PROVIDE("syslog.openlog");
PYTHON_PROVIDE("syslog.setlogmask");
PYTHON_PROVIDE("syslog.syslog");

asm(".ident\t\"\\n\\n\
syslogmodule (mit)\\n\
Copyright 1994 by Lance Ellinghouse\\n\
Cathedral City, California Republic, United States of America\"");
asm(".include \"libc/disclaimer.inc\"");

/***********************************************************
Copyright 1994 by Lance Ellinghouse,
Cathedral City, California Republic, United States of America.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Lance Ellinghouse
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

LANCE ELLINGHOUSE DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL LANCE ELLINGHOUSE BE LIABLE FOR ANY SPECIAL,
INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

******************************************************************/

/******************************************************************

Revision history:

2010/04/20 (Sean Reifschneider)
  - Use basename(sys.argv[0]) for the default "ident".
  - Arguments to openlog() are now keyword args and are all optional.
  - syslog() calls openlog() if it hasn't already been called.

1998/04/28 (Sean Reifschneider)
  - When facility not specified to syslog() method, use default from openlog()
    (This is how it was claimed to work in the documentation)
  - Potential resource leak of o_ident, now cleaned up in closelog()
  - Minor comment accuracy fix.

95/06/29 (Steve Clift)
  - Changed arg parsing to use PyArg_ParseTuple.
  - Added PyErr_Clear() call(s) where needed.
  - Fix core dumps if user message contains format specifiers.
  - Change openlog arg defaults to match normal syslog behavior.
  - Plug memory leak in openlog().
  - Fix setlogmask() to return previous mask value.

******************************************************************/

/* syslog module */

/*  only one instance, only one syslog, so globals should be ok  */
static PyObject *S_ident_o = NULL;                      /*  identifier, held by openlog()  */
static char S_log_open = 0;


static PyObject *
syslog_get_argv(void)
{
    /* Figure out what to use for as the program "ident" for openlog().
     * This swallows exceptions and continues rather than failing out,
     * because the syslog module can still be used because openlog(3)
     * is optional.
     */

    Py_ssize_t argv_len, scriptlen;
    PyObject *scriptobj;
    Py_ssize_t slash;
    PyObject *argv = PySys_GetObject("argv");

    if (argv == NULL) {
        return(NULL);
    }

    argv_len = PyList_Size(argv);
    if (argv_len == -1) {
        PyErr_Clear();
        return(NULL);
    }
    if (argv_len == 0) {
        return(NULL);
    }

    scriptobj = PyList_GetItem(argv, 0);
    if (!PyUnicode_Check(scriptobj)) {
        return(NULL);
    }
    scriptlen = PyUnicode_GET_LENGTH(scriptobj);
    if (scriptlen == 0) {
        return(NULL);
    }

    slash = PyUnicode_FindChar(scriptobj, SEP, 0, scriptlen, -1);
    if (slash == -2)
        return NULL;
    if (slash != -1) {
        return PyUnicode_Substring(scriptobj, slash, scriptlen);
    } else {
        Py_INCREF(scriptobj);
        return(scriptobj);
    }

    return(NULL);
}


static PyObject *
syslog_openlog(PyObject * self, PyObject * args, PyObject *kwds)
{
    long logopt = 0;
    long facility = LOG_USER;
    PyObject *new_S_ident_o = NULL;
    static char *keywords[] = {"ident", "logoption", "facility", 0};
    char *ident = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
                          "|Ull:openlog", keywords, &new_S_ident_o, &logopt, &facility))
        return NULL;

    if (new_S_ident_o) {
        Py_INCREF(new_S_ident_o);
    }

    /*  get sys.argv[0] or NULL if we can't for some reason  */
    if (!new_S_ident_o) {
        new_S_ident_o = syslog_get_argv();
    }

    Py_XDECREF(S_ident_o);
    S_ident_o = new_S_ident_o;

    /* At this point, S_ident_o should be INCREF()ed.  openlog(3) does not
     * make a copy, and syslog(3) later uses it.  We can't garbagecollect it
     * If NULL, just let openlog figure it out (probably using C argv[0]).
     */
    if (S_ident_o) {
        ident = PyUnicode_AsUTF8(S_ident_o);
        if (ident == NULL)
            return NULL;
    }

    openlog(ident, logopt, facility);
    S_log_open = 1;

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject *
syslog_syslog(PyObject * self, PyObject * args)
{
    PyObject *message_object;
    const char *message;
    int   priority = LOG_INFO;

    if (!PyArg_ParseTuple(args, "iU;[priority,] message string",
                          &priority, &message_object)) {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, "U;[priority,] message string",
                              &message_object))
            return NULL;
    }

    message = PyUnicode_AsUTF8(message_object);
    if (message == NULL)
        return NULL;

    /*  if log is not opened, open it now  */
    if (!S_log_open) {
        PyObject *openargs;

        /* Continue even if PyTuple_New fails, because openlog(3) is optional.
         * So, we can still do loggin in the unlikely event things are so hosed
         * that we can't do this tuple.
         */
        if ((openargs = PyTuple_New(0))) {
            PyObject *openlog_ret = syslog_openlog(self, openargs, NULL);
            Py_XDECREF(openlog_ret);
            Py_DECREF(openargs);
        }
    }

    Py_BEGIN_ALLOW_THREADS;
    syslog(priority, "%s", message);
    Py_END_ALLOW_THREADS;
    Py_RETURN_NONE;
}

static PyObject *
syslog_closelog(PyObject *self, PyObject *unused)
{
    if (S_log_open) {
        closelog();
        Py_CLEAR(S_ident_o);
        S_log_open = 0;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
syslog_setlogmask(PyObject *self, PyObject *args)
{
    long maskpri, omaskpri;

    if (!PyArg_ParseTuple(args, "l;mask for priority", &maskpri))
        return NULL;
    omaskpri = setlogmask(maskpri);
    return PyLong_FromLong(omaskpri);
}

static PyObject *
syslog_log_mask(PyObject *self, PyObject *args)
{
    long mask;
    long pri;
    if (!PyArg_ParseTuple(args, "l:LOG_MASK", &pri))
        return NULL;
    mask = LOG_MASK(pri);
    return PyLong_FromLong(mask);
}

static PyObject *
syslog_log_upto(PyObject *self, PyObject *args)
{
    long mask;
    long pri;
    if (!PyArg_ParseTuple(args, "l:LOG_UPTO", &pri))
        return NULL;
    mask = LOG_UPTO(pri);
    return PyLong_FromLong(mask);
}

/* List of functions defined in the module */

static PyMethodDef syslog_methods[] = {
    {"openlog",         (PyCFunction) syslog_openlog,           METH_VARARGS | METH_KEYWORDS},
    {"closelog",        syslog_closelog,        METH_NOARGS},
    {"syslog",          syslog_syslog,          METH_VARARGS},
    {"setlogmask",      syslog_setlogmask,      METH_VARARGS},
    {"LOG_MASK",        syslog_log_mask,        METH_VARARGS},
    {"LOG_UPTO",        syslog_log_upto,        METH_VARARGS},
    {NULL,              NULL,                   0}
};

/* Initialization function for the module */


static struct PyModuleDef syslogmodule = {
    PyModuleDef_HEAD_INIT,
    "syslog",
    NULL,
    -1,
    syslog_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_syslog(void)
{
    PyObject *m;

    /* Create the module and add the functions */
    m = PyModule_Create(&syslogmodule);
    if (m == NULL)
        return NULL;

    /* Add some symbolic constants to the module */

    /* Priorities */
    PyModule_AddIntMacro(m, LOG_EMERG);
    PyModule_AddIntMacro(m, LOG_ALERT);
    PyModule_AddIntMacro(m, LOG_CRIT);
    PyModule_AddIntMacro(m, LOG_ERR);
    PyModule_AddIntMacro(m, LOG_WARNING);
    PyModule_AddIntMacro(m, LOG_NOTICE);
    PyModule_AddIntMacro(m, LOG_INFO);
    PyModule_AddIntMacro(m, LOG_DEBUG);

    /* openlog() option flags */
    PyModule_AddIntMacro(m, LOG_PID);
    PyModule_AddIntMacro(m, LOG_CONS);
    PyModule_AddIntMacro(m, LOG_NDELAY);
#ifdef LOG_ODELAY
    PyModule_AddIntMacro(m, LOG_ODELAY);
#endif
#ifdef LOG_NOWAIT
    PyModule_AddIntMacro(m, LOG_NOWAIT);
#endif
#ifdef LOG_PERROR
    PyModule_AddIntMacro(m, LOG_PERROR);
#endif

    /* Facilities */
    PyModule_AddIntMacro(m, LOG_KERN);
    PyModule_AddIntMacro(m, LOG_USER);
    PyModule_AddIntMacro(m, LOG_MAIL);
    PyModule_AddIntMacro(m, LOG_DAEMON);
    PyModule_AddIntMacro(m, LOG_AUTH);
    PyModule_AddIntMacro(m, LOG_LPR);
    PyModule_AddIntMacro(m, LOG_LOCAL0);
    PyModule_AddIntMacro(m, LOG_LOCAL1);
    PyModule_AddIntMacro(m, LOG_LOCAL2);
    PyModule_AddIntMacro(m, LOG_LOCAL3);
    PyModule_AddIntMacro(m, LOG_LOCAL4);
    PyModule_AddIntMacro(m, LOG_LOCAL5);
    PyModule_AddIntMacro(m, LOG_LOCAL6);
    PyModule_AddIntMacro(m, LOG_LOCAL7);

#ifndef LOG_SYSLOG
#define LOG_SYSLOG              LOG_DAEMON
#endif
#ifndef LOG_NEWS
#define LOG_NEWS                LOG_MAIL
#endif
#ifndef LOG_UUCP
#define LOG_UUCP                LOG_MAIL
#endif
#ifndef LOG_CRON
#define LOG_CRON                LOG_DAEMON
#endif

    PyModule_AddIntMacro(m, LOG_SYSLOG);
    PyModule_AddIntMacro(m, LOG_CRON);
    PyModule_AddIntMacro(m, LOG_UUCP);
    PyModule_AddIntMacro(m, LOG_NEWS);

#ifdef LOG_AUTHPRIV
    PyModule_AddIntMacro(m, LOG_AUTHPRIV);
#endif

    return m;
}

#ifdef __aarch64__
_Section(".rodata.pytab.1 //")
#else
_Section(".rodata.pytab.1")
#endif
 const struct _inittab _PyImport_Inittab_syslog = {
    "syslog",
    PyInit_syslog,
};
