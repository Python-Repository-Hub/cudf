# Copyright (c) 2020-2021, NVIDIA CORPORATION.

from enum import IntEnum

from cudf.api.types import is_decimal_dtype

from libcpp cimport bool
from libcpp.memory cimport unique_ptr
from libcpp.utility cimport move

import numpy as np

from cudf._lib.column cimport Column
from cudf._lib.cpp.column.column cimport column
from cudf._lib.cpp.column.column_view cimport column_view, mutable_column_view

from cudf._lib.types import SUPPORTED_NUMPY_TO_LIBCUDF_TYPES

from cudf._lib.cpp.types cimport data_type, size_type, type_id

from cudf._lib.column import (
    LIBCUDF_TO_SUPPORTED_NUMPY_TYPES,
    SUPPORTED_NUMPY_TO_LIBCUDF_TYPES,
)

cimport cudf._lib.cpp.types as libcudf_types
cimport cudf._lib.cpp.unary as libcudf_unary
from cudf._lib.cpp.unary cimport unary_operator, underlying_type_t_unary_op
from cudf._lib.types cimport dtype_to_data_type, underlying_type_t_type_id


class UnaryOp(IntEnum):
    SIN = <underlying_type_t_unary_op> unary_operator.SIN
    COS = <underlying_type_t_unary_op> unary_operator.COS
    TAN = <underlying_type_t_unary_op> unary_operator.TAN
    ASIN = <underlying_type_t_unary_op> unary_operator.ARCSIN
    ACOS = <underlying_type_t_unary_op> unary_operator.ARCCOS
    ATAN = <underlying_type_t_unary_op> unary_operator.ARCTAN
    SINH = <underlying_type_t_unary_op> unary_operator.SINH
    COSH = <underlying_type_t_unary_op> unary_operator.COSH
    TANH = <underlying_type_t_unary_op> unary_operator.TANH
    ARCSINH = <underlying_type_t_unary_op> unary_operator.ARCSINH
    ARCCOSH = <underlying_type_t_unary_op> unary_operator.ARCCOSH
    ARCTANH = <underlying_type_t_unary_op> unary_operator.ARCTANH
    EXP = <underlying_type_t_unary_op> unary_operator.EXP
    LOG = <underlying_type_t_unary_op> unary_operator.LOG
    SQRT = <underlying_type_t_unary_op> unary_operator.SQRT
    CBRT = <underlying_type_t_unary_op> unary_operator.CBRT
    CEIL = <underlying_type_t_unary_op> unary_operator.CEIL
    FLOOR = <underlying_type_t_unary_op> unary_operator.FLOOR
    ABS = <underlying_type_t_unary_op> unary_operator.ABS
    RINT = <underlying_type_t_unary_op> unary_operator.RINT
    INVERT = <underlying_type_t_unary_op> unary_operator.BIT_INVERT
    NOT = <underlying_type_t_unary_op> unary_operator.NOT


def unary_operation(Column input, object op):
    cdef column_view c_input = input.view()
    cdef unary_operator c_op = <unary_operator>(<underlying_type_t_unary_op>
                                                op)
    cdef unique_ptr[column] c_result

    with nogil:
        c_result = move(
            libcudf_unary.unary_operation(
                c_input,
                c_op
            )
        )

    return Column.from_unique_ptr(move(c_result))


def is_null(Column input):
    cdef column_view c_input = input.view()
    cdef unique_ptr[column] c_result

    with nogil:
        c_result = move(libcudf_unary.is_null(c_input))

    return Column.from_unique_ptr(move(c_result))


def is_valid(Column input):
    cdef column_view c_input = input.view()
    cdef unique_ptr[column] c_result

    with nogil:
        c_result = move(libcudf_unary.is_valid(c_input))

    return Column.from_unique_ptr(move(c_result))


def cast(Column input, object dtype=np.float64):
    cdef column_view c_input = input.view()
    cdef data_type c_dtype = dtype_to_data_type(dtype)

    cdef unique_ptr[column] c_result

    with nogil:
        c_result = move(libcudf_unary.cast(c_input, c_dtype))

    result = Column.from_unique_ptr(move(c_result))
    if is_decimal_dtype(result.dtype):
        result.dtype.precision = dtype.precision
    return result


def is_nan(Column input):
    cdef column_view c_input = input.view()
    cdef unique_ptr[column] c_result

    with nogil:
        c_result = move(libcudf_unary.is_nan(c_input))

    return Column.from_unique_ptr(move(c_result))


def is_non_nan(Column input):
    cdef column_view c_input = input.view()
    cdef unique_ptr[column] c_result

    with nogil:
        c_result = move(libcudf_unary.is_not_nan(c_input))

    return Column.from_unique_ptr(move(c_result))
