/*
 *  Unix SMB/CIFS implementation.
 *  libsmbconf - Samba configuration library - Python bindings
 *
 *  Copyright (C) John Mulligan <phlogistonjohn@asynchrono.us> 2022
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <Python.h>
#include "includes.h"
#include "python/py3compat.h"

#include "lib/smbconf/smbconf.h"
#include "lib/smbconf/smbconf_txt.h"

static PyObject *PyExc_SMBConfError;

typedef struct {
	PyObject_HEAD

	/* C values embedded in our python type */
	TALLOC_CTX * mem_ctx;
	struct smbconf_ctx *conf_ctx;
} py_SMBConf_Object;

static void py_raise_SMBConfError(sbcErr err)
{
	PyObject *v = NULL;
	PyObject *args = NULL;

	/*
	 * TODO: have the exception type accept arguments in new/init
	 * and make the error value accessible as a property
	 */
	args = Py_BuildValue("(is)", err, sbcErrorString(err));
	if (args == NULL) {
		PyErr_Format(PyExc_SMBConfError, "[%d]: %s", err,
			     sbcErrorString(err));
		return;
	}
	v = PyObject_Call(PyExc_SMBConfError, args, NULL);
	Py_DECREF(args);
	if (v != NULL) {
		PyErr_SetObject((PyObject *) Py_TYPE(v), v);
		Py_DECREF(v);
	}
}

/*
 * py_from_smbconf_service returns a python tuple that is basically equivalent
 * to the struct smbconf_service type content-wise.
 */
static PyObject *py_from_smbconf_service(struct smbconf_service *svc)
{
	uint32_t count;
	PyObject *plist = PyList_New(svc->num_params);
	if (plist == NULL) {
		return NULL;
	}

	for (count = 0; count < svc->num_params; count++) {
		PyObject *pt = Py_BuildValue("(ss)",
					     svc->param_names[count],
					     svc->param_values[count]);
		if (pt == NULL) {
			Py_CLEAR(plist);
			return NULL;
		}
		if (PyList_SetItem(plist, count, pt) < 0) {
			Py_CLEAR(pt);
			Py_CLEAR(plist);
			return NULL;
		}
	}
	return Py_BuildValue("(sO)", svc->name, plist);
}

static PyObject *obj_new(PyTypeObject * type, PyObject * args, PyObject * kwds)
{
	py_SMBConf_Object *self = (py_SMBConf_Object *) type->tp_alloc(type, 0);
	if (self == NULL) {
		return NULL;
	}

	self->mem_ctx = talloc_new(NULL);
	if (self->mem_ctx == NULL) {
		Py_DECREF(self);
		return NULL;
	}

	return (PyObject *) self;
}

static void obj_dealloc(py_SMBConf_Object * self)
{
	if (self->conf_ctx != NULL) {
		smbconf_shutdown(self->conf_ctx);
	}
	talloc_free(self->mem_ctx);
	Py_TYPE(self)->tp_free((PyObject *) self);
}

static bool obj_ready(py_SMBConf_Object * self)
{
	if (self->conf_ctx == NULL) {
		PyErr_Format(PyExc_RuntimeError,
			     "attempt to use an uninitialized SMBConf object");
		return false;
	}
	return true;
}

static PyObject *obj_requires_messaging(py_SMBConf_Object * self,
					PyObject * Py_UNUSED(ignored))
{
	if (!obj_ready(self)) {
		return NULL;
	}
	if (smbconf_backend_requires_messaging(self->conf_ctx)) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

static PyObject *obj_is_writable(py_SMBConf_Object * self,
				 PyObject * Py_UNUSED(ignored))
{
	if (!obj_ready(self)) {
		return NULL;
	}
	if (smbconf_is_writeable(self->conf_ctx)) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

static PyObject *obj_share_names(py_SMBConf_Object * self,
				 PyObject * Py_UNUSED(ignored))
{
	sbcErr err;
	uint32_t count;
	uint32_t num_shares;
	char **share_names = NULL;
	PyObject *slist = NULL;
	TALLOC_CTX *mem_ctx = NULL;

	if (!obj_ready(self)) {
		return NULL;
	}

	mem_ctx = talloc_new(self->mem_ctx);
	if (mem_ctx == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	err =
	    smbconf_get_share_names(self->conf_ctx, mem_ctx, &num_shares,
				    &share_names);
	if (err != SBC_ERR_OK) {
		talloc_free(mem_ctx);
		py_raise_SMBConfError(err);
		return NULL;
	}

	slist = PyList_New(num_shares);
	if (slist == NULL) {
		talloc_free(mem_ctx);
		return NULL;
	}
	for (count = 0; count < num_shares; count++) {
		PyObject *ustr = PyUnicode_FromString(share_names[count]);
		if (ustr == NULL) {
			Py_CLEAR(slist);
			talloc_free(mem_ctx);
			return NULL;
		}
		if (PyList_SetItem(slist, count, ustr) < 0) {
			Py_CLEAR(ustr);
			Py_CLEAR(slist);
			talloc_free(mem_ctx);
			return NULL;
		}
	}
	talloc_free(mem_ctx);
	return slist;
}

static PyObject *obj_get_share(py_SMBConf_Object * self, PyObject * args)
{
	sbcErr err;
	char *servicename = NULL;
	struct smbconf_service *svc = NULL;
	PyObject *plist = NULL;
	TALLOC_CTX *mem_ctx = NULL;

	if (!PyArg_ParseTuple(args, "s", &servicename)) {
		return NULL;
	}

	if (!obj_ready(self)) {
		return NULL;
	}

	mem_ctx = talloc_new(self->mem_ctx);
	if (mem_ctx == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	err = smbconf_get_share(self->conf_ctx, mem_ctx, servicename, &svc);
	if (err != SBC_ERR_OK) {
		talloc_free(mem_ctx);
		py_raise_SMBConfError(err);
		return NULL;
	}
	/*
	 * if py_from_smbconf_service returns NULL, then an exception should
	 * already be set. No special error handling needed.
	 */
	plist = py_from_smbconf_service(svc);
	talloc_free(mem_ctx);
	return plist;
}

static PyObject *obj_get_config(py_SMBConf_Object * self,
				PyObject * Py_UNUSED(ignored))
{
	sbcErr err;
	PyObject *svclist = NULL;
	TALLOC_CTX *mem_ctx = NULL;
	uint32_t count;
	uint32_t num_shares;
	struct smbconf_service **svcs = NULL;

	if (!obj_ready(self)) {
		return NULL;
	}

	mem_ctx = talloc_new(self->mem_ctx);
	if (mem_ctx == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	err = smbconf_get_config(self->conf_ctx, mem_ctx, &num_shares, &svcs);
	if (err != SBC_ERR_OK) {
		talloc_free(mem_ctx);
		py_raise_SMBConfError(err);
		return NULL;
	}

	svclist = PyList_New(num_shares);
	if (svclist == NULL) {
		talloc_free(mem_ctx);
		return NULL;
	}
	for (count = 0; count < num_shares; count++) {
		PyObject *svcobj = py_from_smbconf_service(svcs[count]);
		if (svcobj == NULL) {
			Py_CLEAR(svclist);
			talloc_free(mem_ctx);
			return NULL;
		}
		if (PyList_SetItem(svclist, count, svcobj) < 0) {
			Py_CLEAR(svcobj);
			Py_CLEAR(svclist);
			talloc_free(mem_ctx);
			return NULL;
		}
	}

	talloc_free(mem_ctx);
	return svclist;
}

PyDoc_STRVAR(obj_requires_messaging_doc,
"requires_messaging() -> bool\n"
"\n"
"Returns true if the backend requires interprocess messaging.\n");

PyDoc_STRVAR(obj_is_writable_doc,
"is_writeable() -> bool\n"
"\n"
"Returns true if the SMBConf object's backend is writable.\n");

PyDoc_STRVAR(obj_share_names_doc,
"share_names() -> list[str]\n"
"\n"
"Return a list of the share names currently configured.\n"
"Includes the global section as a share name.\n");

PyDoc_STRVAR(obj_get_share_doc,
"get_share() -> (str, list[(str, str)])\n"
"\n"
"Given the name of a share, return a tuple of \n"
"(share_name, share_parms) where share_params is a list of\n"
"(param_name, param_value) tuples.\n"
"The term global can be specified to get global section parameters.\n");

PyDoc_STRVAR(obj_get_config_doc,
"get_config() -> list[(str, list[(str, str)])]\n"
"Return a list of tuples for every section/share of the current\n"
"configuration. Each tuple in the list is the same as described\n"
"for get_share().\n");

static PyMethodDef py_smbconf_obj_methods[] = {
	{ "requires_messaging", (PyCFunction) obj_requires_messaging,
	 METH_NOARGS, obj_requires_messaging_doc },
	{ "is_writeable", (PyCFunction) obj_is_writable, METH_NOARGS,
	 obj_is_writable_doc },
	{ "share_names", (PyCFunction) obj_share_names, METH_NOARGS,
	 obj_share_names_doc },
	{ "get_share", (PyCFunction) obj_get_share, METH_VARARGS,
	 obj_get_share_doc },
	{ "get_config", (PyCFunction) obj_get_config, METH_NOARGS,
	 obj_get_config_doc },
	{ 0 },
};

PyDoc_STRVAR(py_SMBConf_type_doc,
"SMBConf objects provide uniform access to Samba configuration backends.\n"
"\n"
"The SMBConf type should not be instantiated directly. Rather, use a\n"
"backend specific init function like init_txt.\n");

static PyTypeObject py_SMBConf_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "smbconf.SMBConf",
	.tp_doc = py_SMBConf_type_doc,
	.tp_basicsize = sizeof(py_SMBConf_Object),
	.tp_methods = py_smbconf_obj_methods,
	.tp_new = obj_new,
	.tp_dealloc = (destructor) obj_dealloc,
};

static PyObject *py_init_txt(PyObject * module, PyObject * args)
{
	py_SMBConf_Object *obj;
	sbcErr err;
	char *path = NULL;
	struct smbconf_ctx *conf_ctx = NULL;

	if (!PyArg_ParseTuple(args, "s", &path)) {
		return NULL;
	}

	obj = (py_SMBConf_Object *) obj_new(&py_SMBConf_Type, NULL, NULL);
	if (obj == NULL) {
		return NULL;
	}

	err = smbconf_init_txt(obj->mem_ctx, &conf_ctx, path);
	if (err != SBC_ERR_OK) {
		Py_DECREF(obj);
		py_raise_SMBConfError(err);
		return NULL;
	}
	obj->conf_ctx = conf_ctx;
	return (PyObject *) obj;
}

static PyMethodDef pysmbconf_methods[] = {
	{ "init_txt", (PyCFunction) py_init_txt, METH_VARARGS,
	 "Return an SMBConf object for the given text config file." },
	{ 0 },
};

PyDoc_STRVAR(py_smbconf_doc,
"The smbconf module is a wrapper for Samba's smbconf library.\n"
"This library supports common functions to access the contents\n"
"of a configuration backend, such as the text-based smb.conf file\n"
"or the read-write registry backend.\n"
"The read-only functions on the SMBConf type function on both backend\n"
"types. Future, write based functions need a writable backend (registry).\n"
"\n"
"Note that the registry backend will be provided by a different\n"
"library module from the source3 tree (implemenation TBD).\n");

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	.m_name = "smbconf",
	.m_doc = py_smbconf_doc,
	.m_size = -1,
	.m_methods = pysmbconf_methods,
};

MODULE_INIT_FUNC(smbconf)
{
	PyObject *m = PyModule_Create(&moduledef);
	if (m == NULL) {
		return NULL;
	}

	if (PyType_Ready(&py_SMBConf_Type) < 0) {
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(&py_SMBConf_Type);
	if (PyModule_AddObject(m, "SMBConf", (PyObject *) & py_SMBConf_Type) <
	    0) {
		Py_DECREF(&py_SMBConf_Type);
		Py_DECREF(m);
		return NULL;
	}

	PyExc_SMBConfError =
	    PyErr_NewException(discard_const_p(char, "smbconf.SMBConfError"),
			       NULL, NULL);
	if (PyExc_SMBConfError == NULL) {
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(PyExc_SMBConfError);
	if (PyModule_AddObject(m, "SMBConfError", PyExc_SMBConfError) < 0) {
		Py_DECREF(PyExc_SMBConfError);
		Py_DECREF(m);
		return NULL;
	}

	return m;
}