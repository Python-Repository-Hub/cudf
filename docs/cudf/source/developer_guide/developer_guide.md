# Developer Guide

```{note}
At present, this guide only covers the main cuDF library.
In the future, it may be expanded to also cover dask_cudf, cudf_kafka, and custreamz.
```

The cuDF library is a GPU-accelerated, [Pandas-like](https://pandas.pydata.org/) DataFrame library.
Under the hood, all of cuDF's functionality relies on the CUDA-accelerated `libcudf` C++ library.
Thus, cuDF's internals are designed to efficiently and robustly map pandas APIs to `libcudf` functions.
For more information about the `libcudf` library, a good starting point is the
[developer guide](https://github.com/rapidsai/cudf/blob/main/cpp/docs/DEVELOPER_GUIDE.md).

This document assumes familiarity with the
[overall contributing guide](https://github.com/rapidsai/cudf/blob/main/CONTRIBUTING.md).
The goal of this document is to provide more specific guidance for Python developers.
It covers the structure of the Python code and discusses best practices.
Additionally, it includes longer sections on more specific topics like testing and benchmarking.
More specific information on these can be found in the pages below:

```{toctree}
:maxdepth: 1

library_design
```

The rest of this document focuses on a higher-level overview of best practices in cuDF.

## Directory structure and file naming

cuDF generally presents the same importable modules and subpackages as pandas.
All Cython code is contained in `python/cudf/cudf/_lib`.

**Open question**: Should we start enforcing a stricter file/directory layout? Any suggestions?


## Code style

cuDF employs a number of linters to ensure consistent style across the code base:

- [`flake8`](https://github.com/pycqa/flake8) checks for general code formatting compliance. 
- [`black`](https://github.com/psf/black) is an automatic code formatter.
- [`isort`](https://pycqa.github.io/isort/) ensures imports are sorted consistently.
- [`mypy`](http://mypy-lang.org/) performs static type checking.
  In conjunction with [type hints](https://docs.python.org/3/library/typing.html),
  `mypy` can help catch various bugs that are otherwise difficult to find.
- [`pydocstyle`](https://github.com/PyCQA/pydocstyle/) lints docstring style.

Configuration information for these tools is contained in a number of files.
The primary source of truth is the `.pre-commit-config.yaml` file at the root of the repo.
As described in the
[overall contributing guide](https://github.com/rapidsai/cudf/blob/main/CONTRIBUTING.md),
we recommend using [`pre-commit`](https://pre-commit.com/) to manage all linters.

## Deprecating and removing code

cuDF generally follows the policy of deprecating code for one release prior to removal.
For example, if we decide to remove an API during the 22.08 release cycle,
it will be marked as deprecated in the 22.08 release and removed in the 22.10 release.
All internal usage of deprecated APIs in cuDF should be removed when the API is deprecated.
This prevents users from encountering unexpected deprecation warnings when using other (non-deprecated) APIs.
The documentation for the API should also be updated to reflect its deprecation.

When deprecating an API, developers should open a corresponding GitHub issue to track the API removal.
The GitHub issue should be labeled "deprecation" and added to the next release’s project board.
If necessary, the removal timeline can be discussed on this issue.
Upon removal this issue may be closed.
Additionally, when removing an API, make sure to remove all tests and documentation.

Deprecation messages should follow these guidelines:
- Emit a FutureWarning.
- Use a single line (no newline characters)
- Indicate a replacement API, if any exists.
  Deprecations are an opportunity to show users better ways to do things.
- Don't specify a version when removal will occur.
  This gives us more flexibility.

For example:
```python
warnings.warn(
    "`Series.foo` is deprecated and will be removed in a future version of cudf. "
    "Use `Series.new_foo` instead.",
    FutureWarning
)
```

```{warning}
Deprecations should be signaled using a `FutureWarning` **not a `DeprecationWarning`**!
`DeprecationWarning` is hidden by default except in code run in the `__main__` module.
```

## `pandas` compatibility

Maintaining compatibility with the pandas API is a primary goal of cuDF.
Developers should always look at pandas APIs when adding a new feature to cuDF.
However, there are occasionally good reasons to deviate from pandas behavior.

The most common reasons center around performance.
Some APIs cannot match pandas behavior exactly without incurring exorbitant runtime costs.
Others may require using additional memory, which is always at a premium in GPU workflows.
If you are developing a feature and believe that perfect pandas compatibility is infeasible or undesirable,
you should consult with other members of the team to assess how to proceed.

When such a deviation from pandas behavior is necessary, it should be documented.
For more information on how to do that, see [link to documentation#Comparing to pandas].

## Python vs Cython

cuDF makes substantial use of [Cython](https://cython.org/).
Cython is a powerful tool, but it is less user-friendly than pure Python.
It is also more difficult to debug or profile.
Therefore, developers should generally prefer Python code over Cython where possible.

The primary use-case for Cython in cuDF is to expose libcudf C++ APIs to Python.
This Cython usage is generally composed of two parts:
1. A `pxd` file simply declaring C++ APIs so that they may be used in Cython, and
2. A `pyx` file containing Cython functions that wrap those C++ APIs so that they can be called from Python.

The latter wrappers should generally be kept as thin as possible to minimize Cython usage.
For more information see [our Cython layer design documentation](cythonlayer).

In some rare cases we may actually benefit from writing pure Cython code to speed up particular code paths.
Given that most numerical computations in cuDF actually happen in libcudf, however,
such use cases are quite rare.
Any attempt to write pure Cython code for this purpose should be justified with benchmarks.

## Exception handling

TBD, to be written by Michael.
