/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "python_callback_c.h"

#include "python_callback.h"

#include "utils.h"
#include <filesystem>
#include <nlohmann/json.hpp>
using nlohmann::json;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN(gst_gva_python_debug_category);
#define GST_CAT_DEFAULT gst_gva_python_debug_category

G_END_DECLS

namespace {

class PythonError {
    PyObjectWrapper py_stringio_constructor;
    PyObjectWrapper py_traceback_print_exception;

  public:
    PythonError() {
        DECL_WRAPPER(io_module, PyImport_ImportModule("io"));
        py_stringio_constructor.reset(PyObject_GetAttrString(io_module, "StringIO"));
        DECL_WRAPPER(traceback_module, PyImport_ImportModule("traceback"));
        py_traceback_print_exception.reset(PyObject_GetAttrString(traceback_module, "print_exception"));
    }
    void log_python_error(PyObject *ptype, PyObject *pvalue, PyObject *ptraceback, GstGvaPython *gvapython,
                          gboolean is_fatal) {
        DECL_WRAPPER(py_stringio_instance, PyObject_CallObject(py_stringio_constructor, NULL));
        PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);
        DECL_WRAPPER(py_args,
                     Py_BuildValue("OOOOO", ptype ? ptype : Py_None, pvalue ? pvalue : Py_None,
                                   ptraceback ? ptraceback : Py_None, Py_None, (PyObject *)py_stringio_instance));
        DECL_WRAPPER(py_traceback_result, PyObject_CallObject(py_traceback_print_exception, py_args));
        DECL_WRAPPER(py_getvalue, PyObject_GetAttrString(py_stringio_instance, "getvalue"));
        DECL_WRAPPER(py_result, PyObject_CallObject(py_getvalue, nullptr));
        if (is_fatal && gvapython != nullptr) {
            GST_ELEMENT_ERROR(gvapython, RESOURCE, NOT_FOUND, ("%s", PyUnicode_AsUTF8(py_result)), (NULL));
        } else {
            GST_ERROR("%s", PyUnicode_AsUTF8(py_result));
        }
    }
};

} // namespace

void log_python_error(GstGvaPython *gvapython, gboolean is_fatal) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    // Can't be static because should be detroyed while Python context is initialized
    PythonError pythonError;
    pythonError.log_python_error(ptype, pvalue, ptraceback, gvapython, is_fatal);
    PyErr_Restore(ptype, pvalue, ptraceback);
}

void create_arguments(void **args, void **kwargs) {
    try {
        // smart pointers cannot be used because of mixed c and c++ code
        *args = new json(json::value_t::array);
        *kwargs = new json(json::value_t::object);
    } catch (const std::exception &e) {
        GST_ERROR("%s", e.what());
    }
}

void delete_arguments(void *args) {
    json *json_args = static_cast<json *>(args);
    try {
        delete json_args;
    } catch (const std::exception &e) {
        GST_ERROR("%s", e.what());
    }
}

gchar *get_arguments_string(void *args) {
    try {
        if (args) {
            json *json_args = static_cast<json *>(args);
            auto string = json_args->dump();
            return g_strdup(string.c_str());
        }
    } catch (const std::exception &e) {
        GST_ERROR("%s", e.what());
    }
    return NULL;
}

gboolean update_keyword_arguments(const char *argument, void **args) {
    gboolean result = TRUE;
    try {
        json *json_args = static_cast<json *>(*args);
        json new_argument = json::parse(argument);
        json_args->update(new_argument);
    } catch (json::parse_error &e) {
        GST_ERROR("argument %s is not a valid JSON value, error: %s", argument, e.what());
        result = FALSE;
    } catch (const std::exception &e) {
        GST_ERROR("error processing argument: %s, error: %s", argument, Utils::createNestedErrorMsg(e).c_str());
        result = FALSE;
    }
    if (!result) {
        delete_arguments(*args);
        *args = NULL;
    }
    return result;
}

gboolean update_arguments(const char *argument, void **args) {
    gboolean result = TRUE;
    try {
        json *json_args = static_cast<json *>(*args);
        json new_argument = json::parse(argument);
        switch (new_argument.type()) {
        case json::value_t::array: {
            json_args->insert(json_args->end(), new_argument.begin(), new_argument.end());
            break;
        }
        default: {
            (*json_args) += new_argument;
        }
        }
    } catch (json::parse_error &e) {
        GST_ERROR("argument %s is not a valid JSON value, error: %s", argument, e.what());
        result = FALSE;
    } catch (const std::exception &e) {
        GST_ERROR("error processing argument: %s, error: %s", argument, Utils::createNestedErrorMsg(e).c_str());
        result = FALSE;
    }
    if (!result) {
        delete_arguments(*args);
        *args = NULL;
    }
    return result;
}

PythonCallback *create_python_callback(const char *module_path, const char *class_name, const char *function_name,
                                       const char *args_string, const char *keyword_args_string) {
    if (module_path == nullptr || function_name == nullptr) {
        GST_ERROR("module_path, function_name must not be NULL");
        return nullptr;
    }

    auto context_initializer = PythonContextInitializer();
    context_initializer.initialize();
    // add user-specified callback module into Python path
    auto path = std::filesystem::path(module_path);
    auto parent_path = path.parent_path();
    if (!parent_path.empty()) {
        context_initializer.extendPath(parent_path.string());
    }

    try {
        // smart pointers cannot be used because of mixed c and c++ code
        return new PythonCallback(module_path, class_name, function_name, args_string, keyword_args_string);
    } catch (const std::exception &e) {
        GST_ERROR("%s", Utils::createNestedErrorMsg(e).c_str());
        return nullptr;
    }
}

gboolean set_python_callback_caps(struct PythonCallback *python_callback, GstCaps *caps) {
    if (python_callback == nullptr) {
        GST_ERROR("python_callback is not initialized");
        return FALSE;
    }
    auto context_initializer = PythonContextInitializer();
    try {
        python_callback->SetCaps(caps);
        return TRUE;
    } catch (const std::exception &e) {
        GST_ERROR("%s", Utils::createNestedErrorMsg(e).c_str());
        log_python_error(nullptr, false);
        return FALSE;
    }
}

GstFlowReturn invoke_python_callback(GstGvaPython *gvapython, GstBuffer *buffer) {
    if (gvapython->python_callback == nullptr) {
        GST_ELEMENT_ERROR(gvapython, RESOURCE, NOT_FOUND, ("Python_callback is not initialized."), (NULL));
        return GST_FLOW_ERROR;
    }
    auto context_initializer = PythonContextInitializer();
    try {
        if (gvapython->python_callback->CallPython(buffer)) {
            return GST_FLOW_OK;
        } else {
            return GST_BASE_TRANSFORM_FLOW_DROPPED;
        }
    } catch (const std::exception &e) {
        GST_ERROR("%s", Utils::createNestedErrorMsg(e).c_str());
        log_python_error(gvapython, true);
        return GST_FLOW_ERROR;
    }
}

void delete_python_callback(struct PythonCallback *python_callback) {
    auto context_initializer = PythonContextInitializer();
    try {
        delete python_callback;
    } catch (const std::exception &e) {
        GST_ERROR("%s", e.what());
    }
}
