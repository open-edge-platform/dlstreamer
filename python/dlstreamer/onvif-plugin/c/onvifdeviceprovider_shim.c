/* ============================================================================
 * onvifdeviceprovider_shim.c
 *
 * Minimal C GStreamer plugin whose only job is to expose the Python-side
 * ``onvifdeviceprovider`` GstDeviceProvider to gst-inspect-1.0,
 * gst-device-monitor-1.0 and any other pure-C tool that walks the registry
 * cache.
 *
 * libgstpython only honours ``__gstelementfactory__`` and does not expose
 * the loading GstPlugin* to Python, so a Gst.DeviceProvider implemented in
 * Python cannot be persisted into the binary registry by libgstpython
 * alone. This shim is a normal GST plugin with its own ``GST_PLUGIN_DEFINE``
 * and filename, so its features are cached correctly. ``plugin_init`` runs
 * embedded Python, imports ``onvifdeviceprovider`` and calls
 * ``register_provider_with_plugin(plugin_addr)`` which registers the
 * Python-backed DeviceProvider against this very plugin.
 *
 * Build (see ../scripts/build_onvif_shim.sh):
 *     gcc -shared -fPIC -O2 -Wall \
 *         $(pkg-config --cflags --libs gstreamer-1.0) \
 *         $(python3-config --cflags --ldflags --embed) \
 *         -o libgstonvifdeviceprovider.so onvifdeviceprovider_shim.c
 *
 * Runtime requirements:
 *   - ``PYTHONPATH`` must let ``import onvifdeviceprovider`` succeed
 *     (typically the sibling ``python/`` directory plus the project's
 *     ``python/`` root so ``import dlstreamer.onvif`` resolves).
 *   - The built ``libgstonvifdeviceprovider.so`` must live somewhere on
 *     ``GST_PLUGIN_PATH``.
 * ========================================================================== */

#include <gst/gst.h>
#include <dlfcn.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

GST_DEBUG_CATEGORY_STATIC (onvif_shim_debug);
#define GST_CAT_DEFAULT onvif_shim_debug

/* Locate the directory containing this very .so and extend sys.path so
 * ``import onvifdeviceprovider`` resolves to the sibling Python source
 * tree (``<so_dir>/python/onvifdeviceprovider.py``), and ``import
 * dlstreamer.onvif`` resolves to ``<so_dir>/../../`` — without needing
 * the user to fiddle with PYTHONPATH. Any path the user already exported
 * still takes precedence because we only ``append``. */
static void
extend_sys_path (void)
{
  Dl_info info;
  if (dladdr ((void *) &extend_sys_path, &info) == 0 || !info.dli_fname) {
    GST_WARNING ("dladdr failed; cannot auto-extend sys.path");
    return;
  }

  /* dirname() may modify its input — work on a copy. */
  char so_path[4096];
  snprintf (so_path, sizeof (so_path), "%s", info.dli_fname);
  char *so_dir = dirname (so_path);
  if (!so_dir)
    return;

  /* python/ sibling holds ``onvifdeviceprovider.py``;
   * two levels up holds the ``dlstreamer`` package root. */
  char snippet[8192];
  snprintf (snippet, sizeof (snippet),
      "import sys, os\n"
      "_d = r'''%s'''\n"
      "for _p in (os.path.join(_d, 'python'),\n"
      "           os.path.normpath(os.path.join(_d, '..', '..'))):\n"
      "    if os.path.isdir(_p) and _p not in sys.path:\n"
      "        sys.path.append(_p)\n", so_dir);

  if (PyRun_SimpleString (snippet) != 0) {
    GST_WARNING ("failed to extend sys.path from shim");
    if (PyErr_Occurred ())
      PyErr_Print ();
  }
}

/* CPython extension modules (e.g. ``_ctypes``) resolve libpython symbols
 * (``PyTuple_Type`` etc.) at dlopen-time. When this shim is loaded by
 * gst-plugin-scanner with ``RTLD_LOCAL`` (the default), libpython comes in
 * as a NEEDED dependency and its symbols stay local — so any subsequent
 * ``import _ctypes`` fails with ``undefined symbol: PyTuple_Type``.
 * Re-open libpython explicitly with ``RTLD_GLOBAL`` before
 * ``Py_Initialize()`` to make its symbol table visible to extensions. */
static void
ensure_libpython_global (void)
{
  char name[64];
  /* Try the most specific name first (e.g. libpython3.12.so.1.0). */
  snprintf (name, sizeof (name), "libpython%d.%d.so.1.0",
      PY_MAJOR_VERSION, PY_MINOR_VERSION);
  if (dlopen (name, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD))
    return;
  if (dlopen (name, RTLD_NOW | RTLD_GLOBAL))
    return;
  snprintf (name, sizeof (name), "libpython%d.%d.so",
      PY_MAJOR_VERSION, PY_MINOR_VERSION);
  if (dlopen (name, RTLD_NOW | RTLD_GLOBAL))
    return;
  dlopen ("libpython3.so", RTLD_NOW | RTLD_GLOBAL);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  PyGILState_STATE gstate;
  PyObject *module = NULL;
  PyObject *func = NULL;
  PyObject *args = NULL;
  PyObject *res = NULL;
  gboolean ok = FALSE;

  GST_DEBUG_CATEGORY_INIT (onvif_shim_debug, "onvifshim", 0,
      "ONVIF Device Provider Python shim");

  if (!Py_IsInitialized ()) {
    ensure_libpython_global ();
    /* No active interpreter (typical inside the gst-plugin-scanner
     * subprocess). Spin up an isolated one without installing signal
     * handlers so we do not interfere with the host process. */
    Py_InitializeEx (0);
    PyEval_SaveThread ();
  }

  gstate = PyGILState_Ensure ();

  extend_sys_path ();

  module = PyImport_ImportModule ("onvifdeviceprovider");
  if (!module) {
    GST_ERROR ("onvifdeviceprovider: failed to import 'onvifdeviceprovider' "
        "module (check PYTHONPATH / sys.path)");
    if (PyErr_Occurred ())
      PyErr_Print ();
    goto out;
  }

  func = PyObject_GetAttrString (module, "register_provider_with_plugin");
  if (!func || !PyCallable_Check (func)) {
    GST_ERROR ("onvifdeviceprovider: module has no callable "
        "'register_provider_with_plugin'");
    if (PyErr_Occurred ())
      PyErr_Print ();
    goto out;
  }

  /* "K" packs an unsigned long long; the GstPlugin* address is opaque to
   * Python and round-tripped to ctypes.c_void_p on the other side. */
  args = Py_BuildValue ("(K)", (unsigned long long) (uintptr_t) plugin);
  if (!args)
    goto out;

  res = PyObject_CallObject (func, args);
  if (!res) {
    GST_ERROR ("onvifdeviceprovider: register_provider_with_plugin raised");
    PyErr_Print ();
    goto out;
  }

  ok = PyObject_IsTrue (res) ? TRUE : FALSE;
  if (!ok)
    GST_ERROR ("onvifdeviceprovider: register_provider_with_plugin "
        "returned False");

out:
  Py_XDECREF (res);
  Py_XDECREF (args);
  Py_XDECREF (func);
  Py_XDECREF (module);
  PyGILState_Release (gstate);
  /* Intentionally NOT Py_Finalize(): other consumers (gst-python overrides,
   * the host application) may still be using Python after we return. */
  return ok;
}

#ifndef PACKAGE
#define PACKAGE "dlstreamer-onvif"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlsonvif,
    "ONVIF camera WS-Discovery DeviceProvider (Python-backed)",
    plugin_init,
    "1.0",
    "LGPL",
    PACKAGE,
    "https://github.com/dlstreamer/dlstreamer")
