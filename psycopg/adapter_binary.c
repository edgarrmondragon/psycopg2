/* adapter_binary.c - Binary objects
 *
 * Copyright (C) 2003-2010 Federico Di Gregorio <fog@debian.org>
 *
 * This file is part of psycopg.
 *
 * psycopg2 is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link this program with the OpenSSL library (or with
 * modified versions of OpenSSL that use the same license as OpenSSL),
 * and distribute linked combinations including the two.
 *
 * You must obey the GNU Lesser General Public License in all respects for
 * all of the code used other than OpenSSL.
 *
 * psycopg2 is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 */

#define PSYCOPG_MODULE
#include "psycopg/psycopg.h"

#include "psycopg/adapter_binary.h"
#include "psycopg/microprotocols_proto.h"
#include "psycopg/connection.h"

#include <string.h>


/** the quoting code */

static unsigned char *
binary_escape(unsigned char *from, size_t from_length,
               size_t *to_length, PGconn *conn)
{
#if PG_VERSION_HEX >= 0x080104
    if (conn)
        return PQescapeByteaConn(conn, from, from_length, to_length);
    else
#endif
        return PQescapeBytea(from, from_length, to_length);
}

/* binary_quote - do the quote process on plain and unicode strings */

static PyObject *
binary_quote(binaryObject *self)
{
    char *to;
    const char *buffer;
    Py_ssize_t buffer_len;
    size_t len = 0;

    /* if we got a plain string or a buffer we escape it and save the buffer */
    if (PyString_Check(self->wrapped) || PyBuffer_Check(self->wrapped)) {
        /* escape and build quoted buffer */
        if (PyObject_AsReadBuffer(self->wrapped, (const void **)&buffer,
                                  &buffer_len) < 0)
            return NULL;

        to = (char *)binary_escape((unsigned char*)buffer, (size_t) buffer_len,
            &len, self->conn ? ((connectionObject*)self->conn)->pgconn : NULL);
        if (to == NULL) {
            PyErr_NoMemory();
            return NULL;
        }

        if (len > 0)
            self->buffer = PyString_FromFormat(
                (self->conn && ((connectionObject*)self->conn)->equote)
                    ? "E'%s'::bytea" : "'%s'::bytea" , to);
        else
            self->buffer = PyString_FromString("''::bytea");

        PQfreemem(to);
    }

    /* if the wrapped object is not a string or a buffer, this is an error */
    else {
        PyErr_SetString(PyExc_TypeError, "can't escape non-string object");
        return NULL;
    }

    return self->buffer;
}

/* binary_str, binary_getquoted - return result of quoting */

static PyObject *
binary_str(binaryObject *self)
{
    if (self->buffer == NULL) {
        binary_quote(self);
    }
    Py_XINCREF(self->buffer);
    return self->buffer;
}

static PyObject *
binary_getquoted(binaryObject *self, PyObject *args)
{
    return binary_str(self);
}

static PyObject *
binary_prepare(binaryObject *self, PyObject *args)
{
    connectionObject *conn;

    if (!PyArg_ParseTuple(args, "O", &conn))
        return NULL;

    Py_XDECREF(self->conn);
    if (conn) {
        self->conn = (PyObject*)conn;
        Py_INCREF(self->conn);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
binary_conform(binaryObject *self, PyObject *args)
{
    PyObject *res, *proto;

    if (!PyArg_ParseTuple(args, "O", &proto)) return NULL;

    if (proto == (PyObject*)&isqlquoteType)
        res = (PyObject*)self;
    else
        res = Py_None;

    Py_INCREF(res);
    return res;
}

/** the Binary object **/

/* object member list */

static struct PyMemberDef binaryObject_members[] = {
    {"adapted", T_OBJECT, offsetof(binaryObject, wrapped), READONLY},
    {"buffer", T_OBJECT, offsetof(binaryObject, buffer), READONLY},
    {NULL}
};

/* object method table */

static PyMethodDef binaryObject_methods[] = {
    {"getquoted", (PyCFunction)binary_getquoted, METH_NOARGS,
     "getquoted() -> wrapped object value as SQL-quoted binary string"},
    {"prepare", (PyCFunction)binary_prepare, METH_VARARGS,
     "prepare(conn) -> prepare for binary encoding using conn"},
    {"__conform__", (PyCFunction)binary_conform, METH_VARARGS, NULL},
    {NULL}  /* Sentinel */
};

/* initialization and finalization methods */

static int
binary_setup(binaryObject *self, PyObject *str)
{
    Dprintf("binary_setup: init binary object at %p, refcnt = "
        FORMAT_CODE_PY_SSIZE_T,
        self, Py_REFCNT(self)
      );

    self->buffer = NULL;
    self->conn = NULL;
    Py_INCREF(str);
    self->wrapped = str;

    Dprintf("binary_setup: good binary object at %p, refcnt = "
        FORMAT_CODE_PY_SSIZE_T,
        self, Py_REFCNT(self));
    return 0;
}

static int
binary_traverse(PyObject *obj, visitproc visit, void *arg)
{
    binaryObject *self = (binaryObject *)obj;

    Py_VISIT(self->wrapped);
    Py_VISIT(self->buffer);
    Py_VISIT(self->conn);
    return 0;
}

static void
binary_dealloc(PyObject* obj)
{
    binaryObject *self = (binaryObject *)obj;

    Py_CLEAR(self->wrapped);
    Py_CLEAR(self->buffer);
    Py_CLEAR(self->conn);

    Dprintf("binary_dealloc: deleted binary object at %p, refcnt = "
        FORMAT_CODE_PY_SSIZE_T,
        obj, Py_REFCNT(obj)
      );

    Py_TYPE(obj)->tp_free(obj);
}

static int
binary_init(PyObject *obj, PyObject *args, PyObject *kwds)
{
    PyObject *str;

    if (!PyArg_ParseTuple(args, "O", &str))
        return -1;

    return binary_setup((binaryObject *)obj, str);
}

static PyObject *
binary_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return type->tp_alloc(type, 0);
}

static void
binary_del(PyObject* self)
{
    PyObject_GC_Del(self);
}

static PyObject *
binary_repr(binaryObject *self)
{
    return PyString_FromFormat("<psycopg2._psycopg.Binary object at %p>", self);
}

/* object type */

#define binaryType_doc \
"Binary(buffer) -> new binary object"

PyTypeObject binaryType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "psycopg2._psycopg.Binary",
    sizeof(binaryObject),
    0,
    binary_dealloc, /*tp_dealloc*/
    0,          /*tp_print*/
    0,          /*tp_getattr*/
    0,          /*tp_setattr*/

    0,          /*tp_compare*/
    (reprfunc)binary_repr, /*tp_repr*/
    0,          /*tp_as_number*/
    0,          /*tp_as_sequence*/
    0,          /*tp_as_mapping*/
    0,          /*tp_hash */

    0,          /*tp_call*/
    (reprfunc)binary_str, /*tp_str*/
    0,          /*tp_getattro*/
    0,          /*tp_setattro*/
    0,          /*tp_as_buffer*/

    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC, /*tp_flags*/

    binaryType_doc, /*tp_doc*/

    binary_traverse, /*tp_traverse*/
    0,          /*tp_clear*/

    0,          /*tp_richcompare*/
    0,          /*tp_weaklistoffset*/

    0,          /*tp_iter*/
    0,          /*tp_iternext*/

    /* Attribute descriptor and subclassing stuff */

    binaryObject_methods, /*tp_methods*/
    binaryObject_members, /*tp_members*/
    0,          /*tp_getset*/
    0,          /*tp_base*/
    0,          /*tp_dict*/

    0,          /*tp_descr_get*/
    0,          /*tp_descr_set*/
    0,          /*tp_dictoffset*/

    binary_init, /*tp_init*/
    0, /*tp_alloc  will be set to PyType_GenericAlloc in module init*/
    binary_new, /*tp_new*/
    (freefunc)binary_del, /*tp_free  Low-level free-memory routine */
    0,          /*tp_is_gc For PyObject_IS_GC */
    0,          /*tp_bases*/
    0,          /*tp_mro method resolution order */
    0,          /*tp_cache*/
    0,          /*tp_subclasses*/
    0           /*tp_weaklist*/
};


/** module-level functions **/

PyObject *
psyco_Binary(PyObject *module, PyObject *args)
{
    PyObject *str;

    if (!PyArg_ParseTuple(args, "O", &str))
        return NULL;

    return PyObject_CallFunctionObjArgs((PyObject *)&binaryType, str, NULL);
}
