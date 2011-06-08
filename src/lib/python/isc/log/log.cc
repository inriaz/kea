// Copyright (C) 2011  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#include <config.h>

#include <log/message_dictionary.h>
#include <log/logger_manager.h>
#include <log/logger.h>

using namespace isc::log;

namespace {

// This is for testing only. The real module will have it always set as
// NULL and will use the global dictionary.
MessageDictionary* testDictionary = NULL;

PyObject*
setTestDictionary(PyObject*, PyObject* args) {
    PyObject* enableO;
    // The API doesn't seem to provide conversion to bool,
    // so we do it little bit manually
    if (!PyArg_ParseTuple(args, "O", &enableO)) {
        return (NULL);
    }
    int enableI(PyObject_IsTrue(enableO));
    if (enableI == -1) {
        return (NULL);
    }
    bool enable(enableI != 0);

    try {
        delete testDictionary;
        testDictionary = NULL;
        if (enable) {
            testDictionary = new MessageDictionary;
        }
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
    Py_RETURN_NONE;
}

PyObject*
createMessage(PyObject*, PyObject* args) {
    const char* mid;
    const char* text;
    // We parse the strings
    if (!PyArg_ParseTuple(args, "ss", &mid, &text)) {
        return (NULL);
    }
    PyObject* origMid;
    // And extract the original representation of the message
    // ID, so we can return it instead of creating another instance.
    // This call shouldn't fail if the previous suceeded.
    if (!PyArg_ParseTuple(args, "Os", &origMid, &text)) {
        return (NULL);
    }

    try {
        MessageDictionary* dict = testDictionary ? testDictionary :
            &MessageDictionary::globalDictionary();

        // We ignore the result, they will be in some kind of dupe list
        // if there's a problem
        dict->add(mid, text);
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }

    // Return the ID
    Py_INCREF(origMid);
    return (origMid);
}

PyObject*
getMessage(PyObject*, PyObject* args) {
    const char* mid;
    if (!PyArg_ParseTuple(args, "s", &mid)) {
        return (NULL);
    }

    try {
        MessageDictionary* dict = testDictionary ? testDictionary :
            &MessageDictionary::globalDictionary();

        const std::string& result(dict->getText(mid));
        if (result.empty()) {
            Py_RETURN_NONE;
        } else {
            return (Py_BuildValue("s", result.c_str()));
        }
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
}

PyObject*
reset(PyObject*, PyObject*) {
    // TODO Should we check we got exactly 0 arguments?
    // But who cares, it's testing function only
    LoggerManager::reset();
    Py_RETURN_NONE;
}

PyObject*
init(PyObject*, PyObject* args) {
    const char* root;
    const char* file(NULL);
    const char* severity("INFO");
    int dbglevel(0);
    if (!PyArg_ParseTuple(args, "s|zsi", &root, &file, &severity, &dbglevel)) {
        return (NULL);
    }

    try {
        LoggerManager::init(root, file, getSeverity(severity), dbglevel);
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
    Py_RETURN_NONE;
}

PyMethodDef methods[] = {
    {"set_test_dictionary", setTestDictionary, METH_VARARGS,
        "Set or unset testing mode for message dictionary. In testing, "
        "the create_message and get_message functions work on different "
        "than the logger-global dictionary, not polluting it."},
    {"create_message", createMessage, METH_VARARGS,
        "Creates a new message in the dictionary. You shouldn't need to "
        "call this directly, it should be called by the generated message "
        "file. Returns the identifier to be used in logging. The text "
        "shouldn't be empty."},
    {"get_message", getMessage, METH_VARARGS,
        "Get a message. This function is for testing purposes and you don't "
        "need to call it. It returns None if the message does not exist."},
    {"reset", reset, METH_NOARGS,
        "Reset all logging. For testing purposes only, do not use."},
    {"init", init, METH_VARARGS,
        "Run-time initialization. You need to call this before you do any "
        "logging, to configure the root logger name. You may also provide "
        "a filename with message translations (or None if you don't want "
        "any), logging severity (one of 'DEBUG', 'INFO', 'WARN', 'ERROR' or "
        "'FATAL') and a debug level (integer in the range 0-99)."},
    {NULL, NULL, 0, NULL}
};

class LoggerWrapper : public PyObject {
// Everything is public here, as it is accessible only inside this .cc file.
public:
    Logger *logger_;
};

extern PyTypeObject logger_type;

int
Logger_init(LoggerWrapper* self, PyObject* args) {
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        return (-1);
    }
    try {
        self->logger_ = new Logger(name);
        return (0);
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (-1);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (-1);
    }
}

void
Logger_destroy(LoggerWrapper* const self) {
    delete self->logger_;
    self->logger_ = NULL;
    Py_TYPE(self)->tp_free(self);
}

// The isc::log doesn't contain function to convert this way
const char*
severityToText(const Severity& severity) {
    switch (severity) {
        case DEFAULT:
            return ("DEFAULT");
        case DEBUG:
            return ("DEBUG");
        case INFO:
            return ("INFO");
        case WARN:
            return ("WARN");
        case ERROR:
            return ("ERROR");
        case FATAL:
            return ("FATAL");
        default:
            return (NULL);
    }
}

PyObject*
Logger_getEffectiveSeverity(LoggerWrapper* self, PyObject*) {
    try {
        return (Py_BuildValue("s",
                              severityToText(
                                  self->logger_->getEffectiveSeverity())));
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
}

PyObject*
Logger_getDebugLevel(LoggerWrapper* self, PyObject*) {
    try {
        return (Py_BuildValue("i", self->logger_->getDebugLevel()));
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
}

PyObject*
Logger_setSeverity(LoggerWrapper* self, PyObject* args) {
    const char* severity;
    int dbgLevel = 0;
    if (!PyArg_ParseTuple(args, "z|i", &severity, &dbgLevel)) {
        return (NULL);
    }
    try {
        self->logger_->setSeverity((severity == NULL) ? DEFAULT :
                                   getSeverity(severity), dbgLevel);
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
    Py_RETURN_NONE;
}

template<class FPtr> // Who should remember the pointer-to-method syntax
PyObject*
Logger_isLevelEnabled(LoggerWrapper* self, FPtr function) {
    try {
        if ((self->logger_->*function)()) {
            Py_RETURN_TRUE;
        } else {
            Py_RETURN_FALSE;
        }
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
}

PyObject*
Logger_isInfoEnabled(LoggerWrapper* self, PyObject*) {
    return (Logger_isLevelEnabled(self, &Logger::isInfoEnabled));
}

PyObject*
Logger_isWarnEnabled(LoggerWrapper* self, PyObject*) {
    return (Logger_isLevelEnabled(self, &Logger::isWarnEnabled));
}

PyObject*
Logger_isErrorEnabled(LoggerWrapper* self, PyObject*) {
    return (Logger_isLevelEnabled(self, &Logger::isErrorEnabled));
}

PyObject*
Logger_isFatalEnabled(LoggerWrapper* self, PyObject*) {
    return (Logger_isLevelEnabled(self, &Logger::isFatalEnabled));
}

PyObject*
Logger_isDebugEnabled(LoggerWrapper* self, PyObject* args) {
    int level = MIN_DEBUG_LEVEL;
    if (!PyArg_ParseTuple(args, "|i", &level)) {
        return (NULL);
    }

    try {
        if (self->logger_->isDebugEnabled(level)) {
            Py_RETURN_TRUE;
        } else {
            Py_RETURN_FALSE;
        }
    }
    catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return (NULL);
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
        return (NULL);
    }
}

PyMethodDef loggerMethods[] = {
    { "get_effective_severity",
        reinterpret_cast<PyCFunction>(Logger_getEffectiveSeverity),
        METH_NOARGS, "Returns the effective logging severity as string" },
    { "get_debug_level", reinterpret_cast<PyCFunction>(Logger_getDebugLevel),
        METH_NOARGS, "Returns the current debug level." },
    { "set_severity",
        reinterpret_cast<PyCFunction>(Logger_setSeverity), METH_VARARGS,
        "Sets the severity of a logger. The parameters are severity as a "
        "string and, optionally, a debug level (integer in range 0-99). "
        "The severity may be NULL, in which case an inherited value is taken."
        },
    { "is_debug_enabled", reinterpret_cast<PyCFunction>(Logger_isDebugEnabled),
        METH_VARARGS, "Returns if the logger would log debug message now. "
            "You can provide a desired debug level."
    },
    { "is_info_enabled", reinterpret_cast<PyCFunction>(Logger_isInfoEnabled),
        METH_NOARGS, "Returns if the logger would log info message now." },
    { "is_warn_enabled", reinterpret_cast<PyCFunction>(Logger_isWarnEnabled),
        METH_NOARGS, "Returns if the logger would log warn message now." },
    { "is_error_enabled", reinterpret_cast<PyCFunction>(Logger_isErrorEnabled),
        METH_NOARGS, "Returns if the logger would log error message now." },
    { "is_fatal_enabled", reinterpret_cast<PyCFunction>(Logger_isFatalEnabled),
        METH_NOARGS, "Returns if the logger would log fatal message now." },
    { NULL, NULL, 0, NULL }
};

PyTypeObject logger_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "isc.log.Logger",
    sizeof(LoggerWrapper),                 // tp_basicsize
    0,                                  // tp_itemsize
    reinterpret_cast<destructor>(Logger_destroy),       // tp_dealloc
    NULL,                               // tp_print
    NULL,                               // tp_getattr
    NULL,                               // tp_setattr
    NULL,                               // tp_reserved
    NULL,                               // tp_repr
    NULL,                               // tp_as_number
    NULL,                               // tp_as_sequence
    NULL,                               // tp_as_mapping
    NULL,                               // tp_hash 
    NULL,                               // tp_call
    NULL,                               // tp_str
    NULL,                               // tp_getattro
    NULL,                               // tp_setattro
    NULL,                               // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                 // tp_flags
    "Wrapper around the C++ isc::log::Logger class."
    "It is not complete, but everything important should be here.",
    NULL,                               // tp_traverse
    NULL,                               // tp_clear
    NULL,                               // tp_richcompare
    0,                                  // tp_weaklistoffset
    NULL,                               // tp_iter
    NULL,                               // tp_iternext
    loggerMethods,                      // tp_methods
    NULL,                               // tp_members
    NULL,                               // tp_getset
    NULL,                               // tp_base
    NULL,                               // tp_dict
    NULL,                               // tp_descr_get
    NULL,                               // tp_descr_set
    0,                                  // tp_dictoffset
    reinterpret_cast<initproc>(Logger_init),            // tp_init
    NULL,                               // tp_alloc
    PyType_GenericNew,                  // tp_new
    NULL,                               // tp_free
    NULL,                               // tp_is_gc
    NULL,                               // tp_bases
    NULL,                               // tp_mro
    NULL,                               // tp_cache
    NULL,                               // tp_subclasses
    NULL,                               // tp_weaklist
    NULL,                               // tp_del
    0                                   // tp_version_tag
};

PyModuleDef iscLog = {
    { PyObject_HEAD_INIT(NULL) NULL, 0, NULL},
    "log",
    "Python bindings for the classes in the isc::log namespace.\n\n"
    "These bindings are close match to the C++ API, but they are not complete "
    "(some parts are not needed) and some are done in more python-like ways.",
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    NULL
};

}

PyMODINIT_FUNC
PyInit_log(void) {
    PyObject* mod = PyModule_Create(&iscLog);
    if (mod == NULL) {
        return (NULL);
    }

    if (PyType_Ready(&logger_type) < 0) {
        return (NULL);
    }

    if (PyModule_AddObject(mod, "Logger",
                           static_cast<PyObject*>(static_cast<void*>(
                               &logger_type))) < 0) {
        return (NULL);
    }
    Py_INCREF(&logger_type);

    return (mod);
}
