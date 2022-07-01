# Copyright (c) 2018-2022, NVIDIA CORPORATION.

import inspect
import sys
import textwrap

import numpy as np
import pandas as pd
import pytest

import cudf
from cudf.testing._utils import assert_eq


def _is_cudf(lib):
    # Is it safe to use `if lib is cudf`? I think there are some cases where
    # the imports of a package in different modules may not be identical for
    # that purpose, will have to double-check.
    return lib.__name__ == "cudf"


# The parameter name used for pandas/cudf in test functions.
_LIB_PARAM_NAME = "lib"


def pandas_comparison_test(*args, assert_func=assert_eq):
    def deco(test):
        """Run a test function with cudf and pandas and ensure equal results.

        This decorator generates a new function that looks identical to the
        decorated function but calls the original function twice, once each for
        pandas and cudf and asserts that the two results are equal.
        """
        parameters = inspect.signature(test).parameters
        params_str = ", ".join(
            f"{p}" for p in parameters if p != _LIB_PARAM_NAME
        )
        arg_str = ", ".join(
            f"{p}={p}" for p in parameters if p != _LIB_PARAM_NAME
        )

        if arg_str:
            arg_str += ", "

        cudf_arg_str = arg_str + f"{_LIB_PARAM_NAME}=cudf"
        pandas_arg_str = arg_str + f"{_LIB_PARAM_NAME}=pandas"

        src = textwrap.dedent(
            f"""
            import pandas
            import cudf
            import makefun
            @makefun.wraps(
                test,
                remove_args=("{_LIB_PARAM_NAME}",),
            )
            def wrapped_test({params_str}):
                print()
                cudf_output = test({cudf_arg_str})
                pandas_output = test({pandas_arg_str})
                assert_func(pandas_output, cudf_output)
            """
        )
        globals_ = {
            "test": test,
            "assert_func": assert_func,
        }
        exec(src, globals_)
        wrapped_test = globals_["wrapped_test"]
        # In case marks were applied to the original benchmark, copy them over.
        if marks := getattr(test, "pytestmark", None):
            wrapped_test.pytestmark = marks
        return wrapped_test

    if args:
        if len(args) == 1:
            # Was called directly with a test.
            return deco(args[0])
        raise ValueError("This decorator only supports keyword arguments.")
    return deco


@pandas_comparison_test
def test_init(lib):
    print(f"test1, {cudf}", file=sys.stderr)
    data = [
        (5, "cats", "jump", np.nan),
        (2, "dogs", "dig", 7.5),
        (3, "cows", "moo", -2.1, "occasionally"),
    ]
    return lib.DataFrame(data)


@pandas_comparison_test()
def test_init2(lib):
    print(f"test2, {cudf}", file=sys.stderr)
    data = [
        (5, "cats", "jump", np.nan),
        (2, "dogs", "dig", 7.5),
        (3, "cows", "moo", -2.1, "occasionally"),
    ]
    return lib.DataFrame(data)


@pytest.fixture
def dt():
    return [
        (5, "cats", "jump", np.nan),
        (2, "dogs", "dig", 7.5),
        (3, "cows", "moo", -2.1, "occasionally"),
    ]


@pandas_comparison_test
def test_init4(lib, dt):
    print(f"test4, {lib}", file=sys.stderr)
    return lib.DataFrame(dt)


@pandas_comparison_test
@pytest.mark.parametrize(
    "data",
    [
        [
            (5, "cats", "jump", np.nan),
            (2, "dogs", "dig", 7.5),
            (3, "cows", "moo", -2.1, "occasionally"),
        ]
    ],
)
def test_init5(lib, data):
    print(f"test5, {lib}", file=sys.stderr)
    return lib.DataFrame(data)


@pytest.mark.parametrize(
    "data",
    [
        [
            (5, "cats", "jump", np.nan),
            (2, "dogs", "dig", 7.5),
            (3, "cows", "moo", -2.1, "occasionally"),
        ]
    ],
)
@pandas_comparison_test
def test_init6(lib, data):
    print(f"test6, {lib}", file=sys.stderr)
    return lib.DataFrame(data)


@pytest.mark.parametrize(
    "df",
    [
        pd.DataFrame(
            [
                (5, "cats", "jump", np.nan),
                (2, "dogs", "dig", 7.5),
                (3, "cows", "moo", -2.1, "occasionally"),
            ]
        )
    ],
)
@pandas_comparison_test
def test_from_pandas(lib, df):
    print(f"test_from_pandas, {lib}", file=sys.stderr)
    return lib.from_pandas(df) if _is_cudf(lib) else df


def custom_assert(expected, got):
    cudf.testing.assert_frame_equal(cudf.from_pandas(expected), got)


@pytest.mark.parametrize(
    "df",
    [
        pd.DataFrame(
            [
                (5, "cats", "jump", np.nan),
                (2, "dogs", "dig", 7.5),
                (3, "cows", "moo", -2.1, "occasionally"),
            ]
        )
    ],
)
@pandas_comparison_test(assert_func=custom_assert)
def test_from_pandas_custom_assert(lib, df):
    print(f"test_from_pandas_custom_assert, {lib}", file=sys.stderr)
    return lib.from_pandas(df) if _is_cudf(lib) else df
