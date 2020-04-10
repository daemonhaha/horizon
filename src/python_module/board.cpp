#include "board.hpp"
#include "pool/pool_manager.hpp"
#include "nlohmann/json.hpp"
#include "util.hpp"
#include "export_gerber/gerber_export.hpp"
#include "export_pdf/export_pdf_board.hpp"
#include "export_pnp/export_pnp.hpp"
#include "export_step/export_step.hpp"
#include <podofo/podofo.h>

BoardWrapper::BoardWrapper(const horizon::Project &prj)
    : pool(horizon::PoolManager::get().get_by_uuid(prj.pool_uuid)->base_path, prj.pool_cache_directory),
      block(horizon::Block::new_from_file(prj.get_top_block().block_filename, pool)), vpp(prj.vias_directory, pool),
      board(horizon::Board::new_from_file(prj.board_filename, block, pool, vpp))
{
    board.expand();
    board.update_planes();
}


static PyObject *PyBoard_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyBoard *self;
    self = (PyBoard *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->board = nullptr;
    }
    return (PyObject *)self;
}

static void PyBoard_dealloc(PyObject *pself)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    delete self->board;
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyObject *PyBoard_get_gerber_export_settings(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    auto settings = self->board->board.fab_output_settings.serialize();
    return py_from_json(settings);
}

static PyObject *PyBoard_export_gerber(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    PyObject *py_export_settings = nullptr;
    if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &py_export_settings))
        return NULL;
    try {
        auto settings_json = json_from_py(py_export_settings);
        horizon::FabOutputSettings settings(settings_json);
        horizon::GerberExporter ex(&self->board->board, &settings);
        ex.generate();
    }
    catch (const std::exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }
    catch (...) {
        PyErr_SetString(PyExc_IOError, "unknown exception");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *PyBoard_get_pdf_export_settings(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    auto settings = self->board->board.pdf_export_settings.serialize_board();
    return py_from_json(settings);
}

static PyObject *PyBoard_export_pdf(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    PyObject *py_export_settings = nullptr;
    if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &py_export_settings))
        return NULL;
    try {
        auto settings_json = json_from_py(py_export_settings);
        horizon::PDFExportSettings settings(settings_json);
        horizon::export_pdf(self->board->board, settings, nullptr);
    }
    catch (const std::exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }
    catch (const PoDoFo::PdfError &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }
    catch (...) {
        PyErr_SetString(PyExc_IOError, "unknown exception");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *PyBoard_get_pnp_export_settings(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    auto settings = self->board->board.pnp_export_settings.serialize();
    return py_from_json(settings);
}

static PyObject *PyBoard_export_pnp(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    PyObject *py_export_settings = nullptr;
    if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &py_export_settings))
        return NULL;
    try {
        auto settings_json = json_from_py(py_export_settings);
        horizon::PnPExportSettings settings(settings_json);
        horizon::export_PnP(self->board->board, settings);
    }
    catch (const std::exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }
    catch (...) {
        PyErr_SetString(PyExc_IOError, "unknown exception");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *PyBoard_get_step_export_settings(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    auto settings = self->board->board.step_export_settings.serialize();
    return py_from_json(settings);
}

class py_exception : public std::exception {
};

static PyObject *PyBoard_export_step(PyObject *pself, PyObject *args)
{
    auto self = reinterpret_cast<PyBoard *>(pself);
    PyObject *py_export_settings = nullptr;
    PyObject *py_callback = nullptr;
    if (!PyArg_ParseTuple(args, "O!|O", &PyDict_Type, &py_export_settings, &py_callback))
        return NULL;
    if (py_callback && !PyCallable_Check(py_callback)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }
    try {
        auto settings_json = json_from_py(py_export_settings);
        horizon::STEPExportSettings settings(settings_json);
        horizon::export_step(
                settings.filename, self->board->board, self->board->pool, settings.include_3d_models,
                [py_callback](const std::string &s) {
                    if (py_callback) {
                        PyObject *arglist = Py_BuildValue("(s)", s.c_str());
                        PyObject *result = PyObject_CallObject(py_callback, arglist);
                        Py_DECREF(arglist);
                        if (result == NULL)
                            throw py_exception();
                        Py_DECREF(result);
                    }
                },
                nullptr, settings.prefix);
    }
    catch (const py_exception &e) {
        return NULL;
    }
    catch (const std::exception &e) {
        PyErr_SetString(PyExc_IOError, e.what());
        return NULL;
    }
    catch (...) {
        PyErr_SetString(PyExc_IOError, "unknown exception");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyMethodDef PyBoard_methods[] = {
        {"get_gerber_export_settings", PyBoard_get_gerber_export_settings, METH_NOARGS,
         "Return gerber export settings"},
        {"export_gerber", PyBoard_export_gerber, METH_VARARGS, "Export gerber"},
        {"get_pdf_export_settings", PyBoard_get_pdf_export_settings, METH_NOARGS, "Return PDF export settings"},
        {"get_pnp_export_settings", PyBoard_get_pnp_export_settings, METH_NOARGS, "Return PnP export settings"},
        {"get_step_export_settings", PyBoard_get_step_export_settings, METH_NOARGS, "Return STEP export settings"},
        {"export_pdf", PyBoard_export_pdf, METH_VARARGS, "Export PDF"},
        {"export_pnp", PyBoard_export_pnp, METH_VARARGS, "Export pick and place"},
        {"export_step", PyBoard_export_step, METH_VARARGS, "Export STEP"},
        {NULL} /* Sentinel */
};

PyTypeObject BoardType = [] {
    PyTypeObject r = {PyVarObject_HEAD_INIT(NULL, 0)};
    r.tp_name = "horizon.Board";
    r.tp_basicsize = sizeof(PyBoard);

    r.tp_itemsize = 0;
    r.tp_dealloc = PyBoard_dealloc;
    r.tp_flags = Py_TPFLAGS_DEFAULT;
    r.tp_doc = "Board";

    r.tp_methods = PyBoard_methods;
    r.tp_new = PyBoard_new;
    return r;
}();
