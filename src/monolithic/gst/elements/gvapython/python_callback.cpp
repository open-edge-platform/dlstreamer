/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "python_callback.h"
#include "python_callback_c.h"

#include "gva_utils.h"
#include "inference_backend/logger.h"

#include <filesystem>
#include <pygobject-3.0/pygobject.h>

#ifndef _WIN32
#include <dlfcn.h>
#include <gmodule.h>
#endif

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN(gst_gva_python_debug_category);
#define GST_CAT_DEFAULT gst_gva_python_debug_category

G_END_DECLS

namespace {
PyObject *extractClass(PyObjectWrapper &pluginModule, const char *class_name, const char *args_string,
                       const char *kwargs_string) {
    DECL_WRAPPER(class_type, PyObject_GetAttrString(pluginModule, class_name));

    if ((args_string) || (kwargs_string)) {
        DECL_WRAPPER(jsonModule, PyImport_ImportModule("json"));
        DECL_WRAPPER(json_load_s, PyObject_GetAttrString(jsonModule, "loads"));
        DECL_WRAPPER(tuple, PyTuple_New(0));
        DECL_WRAPPER(dict, PyDict_New());
        if (args_string) {
            DECL_WRAPPER(class_args, PyUnicode_FromString(args_string));
            DECL_WRAPPER(list, PyObject_CallFunctionObjArgs(json_load_s, (PyObject *)class_args, NULL));
            tuple.reset(PyList_AsTuple((PyObject *)list), "PyList_AsTuple((PyObject*)list)");
        }
        if (kwargs_string) {
            DECL_WRAPPER(class_args, PyUnicode_FromString(kwargs_string));
            dict.reset(PyObject_CallFunctionObjArgs(json_load_s, (PyObject *)class_args, NULL),
                       "PyObject_CallFunctionObjArgs(json_load_s,(PyObject*)class_args,NULL)");
        }
        return PyObject_Call(class_type, (PyObject *)tuple, (PyObject *)dict);
    }
    return PyObject_CallFunctionObjArgs(class_type, NULL);
}

gboolean callPython(GstBuffer *buffer, GstCaps *caps, PyObjectWrapper &py_frame_class, PyObjectWrapper &py_function) {
    DECL_WRAPPER(py_buffer, pyg_boxed_new(buffer->mini_object.type, buffer, FALSE /*copy_boxed*/, FALSE /*own_ref*/));
    DECL_WRAPPER(py_caps, pyg_boxed_new(caps->mini_object.type, caps, FALSE /*copy_boxed*/, FALSE /*own_ref*/));
    DECL_WRAPPER(frame, PyObject_CallFunctionObjArgs(py_frame_class, (PyObject *)py_buffer, Py_None,
                                                     (PyObject *)py_caps, nullptr));
    DECL_WRAPPER(args, Py_BuildValue("(O)", (PyObject *)frame));
    PyObjectWrapper result(PyObject_CallObject(py_function, args));

    if (((PyObject *)result) == nullptr) {
        throw std::runtime_error("Error in Python function");
    }
    return (PyObject_IsTrue(result) == 1) ? 1 : 0;
}
} // namespace

// This function safely imports a Python module from a given file path using Python's importlib.
// It generates and executes Python code to load the module, handling errors and reporting them.
// Arguments:
//   module_name - the name to assign to the imported module in sys.modules
//   file_path   - the path to the Python file (without .py extension)
// Returns:
//   A PyObject* pointer to the imported module, or NULL on error.
PyObject *import_module_full_path(const char *module_name, const char *file_path) {
    auto module_path = std::filesystem::path(file_path);

    // Check if file exists, try with and without .py extension
    if (!std::filesystem::exists(module_path)) {
        module_path.replace_extension(".py");
        if (!std::filesystem::exists(module_path)) {
            GST_ERROR("Error: Python module file not found: %s\n", module_path.string().c_str());
            return NULL;
        }
    }

    // Prepare Python code to import the module safely
    char python_code[4096];
    snprintf(python_code, sizeof(python_code),
             "import importlib.util\n"
             "import importlib.machinery\n"
             "import sys\n"
             "import os\n"
             "\n"
             "def create_spec_any_extension(module_name, file_path):\n"
             "    loader = importlib.machinery.SourceFileLoader(module_name, file_path)\n"
             "    spec = importlib.machinery.ModuleSpec(module_name, loader, origin = file_path)\n"
             "    return spec\n"
             "module_name = '%s'\n"
             "file_path = r'%s'\n"
             "\n"
             "try:\n"
             "    # Check if file exists\n"
             "    if not os.path.exists(file_path):\n"
             "        raise FileNotFoundError(f'Python module file not found: {file_path}')\n"
             "    \n"
             "    # Create spec\n"
             "    spec = create_spec_any_extension(module_name, file_path)\n"
             "    if spec is None:\n"
             "        raise ImportError(f'Cannot create spec for {file_path}')\n"
             "    \n"
             "    # Create module\n"
             "    module = importlib.util.module_from_spec(spec)\n"
             "    if module is None:\n"
             "        raise ImportError('Cannot create module from spec')\n"
             "    \n"
             "    # Add to sys.modules\n"
             "    sys.modules[module_name] = module\n"
             "    \n"
             "    # Execute module\n"
             "    spec.loader.exec_module(module)\n"
             "    \n"
             "    imported_module = module\n"
             "    success = True\n"
             "    error_obj = None\n"
             "\n"
             "except Exception as e:\n"
             "    imported_module = None\n"
             "    success = False\n"
             "    error_obj = e\n",
             module_name, module_path.string().c_str());

    // Execute the generated Python code
    // in the __main__ module's namespace
    PyObject *main_module = PyImport_AddModule("__main__");
    PyObject *main_dict = PyModule_GetDict(main_module);

    PyObject *result = PyRun_String(python_code, Py_file_input, main_dict, main_dict);
    if (result == NULL) {
        PyErr_Print();
        return NULL;
    }
    Py_DECREF(result);

    // Check result
    PyObject *success = PyDict_GetItemString(main_dict, "success");
    if (success && success == Py_True) {
        PyObject *module = PyDict_GetItemString(main_dict, "imported_module");
        if (module != NULL) {
            Py_INCREF(module);
            return module;
        }
    } else {
        PyObject *error = PyDict_GetItemString(main_dict, "error_obj");
        if (error) {
#if PY_VERSION_HEX >= 0x030C0000 /* 3.12 */
            PyErr_DisplayException(error);
#else
            PyObject *tb = PyException_GetTraceback(error);
            PyErr_Display((PyObject *)Py_TYPE(error), error, tb);
            Py_XDECREF(tb);
#endif
        }
    }

    return NULL;
}

PythonContextInitializer::PythonContextInitializer() {
    state = PyGILState_UNLOCKED;
    if (Py_IsInitialized()) {
        state = PyGILState_Ensure();
        has_old_state = true;
    } else {
        Py_Initialize();
    }

    sys_path = PySys_GetObject("path");
}

PythonContextInitializer::~PythonContextInitializer() {
    if (has_old_state) {
        PyGILState_Release(state);
    } else {
        PyEval_SaveThread();
    }
}

void PythonContextInitializer::initialize() {
#ifndef _WIN32
    /* load libpython.so, not required on Windows */
    Dl_info libpython_info = Dl_info();
    dladdr((void *)Py_IsInitialized, &libpython_info);
    GModule *libpython = g_module_open(libpython_info.dli_fname, G_MODULE_BIND_LAZY);
#endif
    /* initialize pygobject */
    if (!pygobject_init(3, 0, 0)) {
        // print Python error if available
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }
        throw std::runtime_error("pygobject_init failed");
    }
#ifndef _WIN32
    if (libpython) {
        g_module_close(libpython);
    }
#endif

    /* init arguments passed to a python script*/
    static wchar_t tmp[] = L"";
    static wchar_t *empty_argv[] = {tmp};
    PySys_SetArgv(1, empty_argv);
}

void PythonContextInitializer::extendPath(const std::string &module_path) {
    if (!module_path.empty()) {
        /* PyList_Append increases the reference counter */
        PyList_Append(sys_path, WRAPPER(PyUnicode_FromString(module_path.c_str())));
    }
}

PythonCallback::PythonCallback(const char *module_path, const char *class_name, const char *function_name,
                               const char *args_string, const char *kwargs_string) {
    ITT_TASK(__FUNCTION__);
    caps_ptr = nullptr;
    if (module_path == nullptr) {
        throw std::invalid_argument("module_path cannot be empty");
    }

    auto path = std::filesystem::path(module_path);
    auto filename = path.filename();
    module_name = filename.stem().string();

    PyObjectWrapper pluginModule = import_module_full_path(module_name.c_str(), module_path);
    if (!(PyObject *)pluginModule) {
        log_python_error(nullptr, false);
        throw std::runtime_error("Error loading Python module " + std::string(module_path));
    }
    if (class_name) {
        PyObjectWrapper py_class(extractClass(pluginModule, class_name, args_string, kwargs_string), "py_class");

        if (!(PyObject *)py_class) {
            log_python_error(nullptr, false);
            throw std::runtime_error("Error creating Python class " + std::string(class_name));
        }

        py_function.reset(PyObject_GetAttrString(py_class, function_name), "py_function");
    } else {
        py_function.reset(PyObject_GetAttrString(pluginModule, function_name), "py_function");
    }
    if (!(PyObject *)py_function) {
        throw std::runtime_error("Error getting function '" + std::string(function_name) + "' from Python module " +
                                 std::string(module_path));
    }
}

void PythonCallback::SetCaps(GstCaps *caps) {
    assert(caps && "Expected vaild caps in PythonCallback::SetCaps!");
    caps_ptr = caps;
    if (!(PyObject *)py_frame_class) {
        GstStructure *caps_s = gst_caps_get_structure((const GstCaps *)caps, 0);
        const gchar *name = gst_structure_get_name(caps_s);

        if ((g_strrstr(name, "video")) || (g_strrstr(name, "image"))) {
            // Get gstgva.VideoFrame constructor
            DECL_WRAPPER(gva_module, PyImport_ImportModule("gstgva"));
            if (!py_frame_class.reset(PyObject_GetAttrString(gva_module, "VideoFrame"), "videoframe_class")) {
                throw std::runtime_error("Error getting gstgva.VideoFrame");
            }
        }
#ifdef AUDIO
        else if (g_strrstr(name, "audio")) {
            // Get gstgva.audio.AudioFrame constructor
            DECL_WRAPPER(gva_audio_module, PyImport_ImportModule("gstgva.audio"));
            if (!py_frame_class.reset(PyObject_GetAttrString(gva_audio_module, "AudioFrame"), "audioframe_class")) {
                throw std::runtime_error("Error getting gstgva.audio.AudioFrame");
            }
        }
#endif
        else {
            throw std::runtime_error("Invalid input caps");
        }
    }
}

gboolean PythonCallback::CallPython(GstBuffer *buffer) {
    ITT_TASK(module_name.c_str());
    gboolean result = callPython(buffer, caps_ptr, py_frame_class, py_function);
    return result;
}
