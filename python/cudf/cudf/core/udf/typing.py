# Copyright (c) 2020-2022, NVIDIA CORPORATION.

import operator

from numba import cuda, types
from numba.core.extending import (
    make_attribute_wrapper,
    models,
    register_model,
    typeof_impl,
)
from numba.core.typing import signature as nb_signature
from numba.core.typing.templates import (
    AbstractTemplate,
    AttributeTemplate,
    ConcreteTemplate,
)
from numba.core.typing.typeof import typeof
from numba.cuda.cudadecl import registry as cuda_decl_registry
from pandas._libs.missing import NAType as _NAType

from cudf.core.udf import api
from cudf.core.udf._ops import (
    arith_ops,
    bitwise_ops,
    comparison_ops,
    unary_ops,
)

SUPPORTED_NUMBA_TYPES = (
    types.Number,
    types.Boolean,
    types.NPDatetime,
    types.NPTimedelta,
    types.PyObject,
)


# String object definitions
class DString(types.Type):
    def __init__(self):
        super().__init__(name="dstring")



class StringView(types.Type):
    def __init__(self):
        super().__init__(name="string_view")


string_view = StringView()
dstring = DString()


@typeof_impl.register(StringView)
def typeof_stringview(val, c):
    return string_view

@typeof_impl.register(DString)
def typeof_dstring(val, c):
    return dstring

@register_model(StringView)
class stringview_model(models.StructModel):
    # from string_view.hpp:
    # private:
    #  const char* _data{};          ///< Pointer to device memory contain char array for this string
    #  size_type _bytes{};           ///< Number of bytes in _data for this string
    #  mutable size_type _length{};  ///< Number of characters in this string (computed)

    _members = (
        ("data", types.CPointer(types.char)),
        ("bytes", types.int32),
        ("length", types.int32),
    )

    bytes = 0
    for member_ty in (t[1] for t in _members):
        if isinstance(member_ty, types.CPointer):
            # TODO: is this always right?
            bytes += 8
        else:
            bytes += member_ty.bitwidth / 8
    
    size_bytes = bytes

    def __init__(self, dmm, fe_type):
        super().__init__(dmm, fe_type, self._members)

@register_model(DString)
class dstring_model(models.StructModel):
    # from dstring.hpp:
    # private:
    #   char* m_data{};
    #   cudf::size_type m_bytes{};
    #   cudf::size_type m_size{};

    _members = (
        ("m_data", types.CPointer(types.char)),
        ("m_bytes", types.int32),
        ("m_size", types.int32)
    )
    bytes = 0
    for member_ty in (t[1] for t in _members):
        if isinstance(member_ty, types.CPointer):
            # TODO: is this always right?
            bytes += 8
        else:
            bytes += member_ty.bitwidth / 8
    
    size_bytes = bytes

    def __init__(self, dmm, fe_type):
        super().__init__(dmm, fe_type, self._members)


class StrViewArgHandler:
    """
    As part of Numbas preprocessing step incoming function arguments are
    modified based on the associated type for that argument that was used
    to JIT the kernel. However it only knows how to handle built in array
    types natively. With string UDFs, the jitted type is string_view*,
    which numba does not know how to handle.

    This small piece of code implements the necessary handling. Really all
    it does is says is funnel the handling of string_view* to the handling
    of raw pointer arguments, which numba knows what to do with. 

    See numba.cuda.compiler._prepare_args for details.
    """
    def prepare_args(self, ty, val, **kwargs):
        if isinstance(ty, types.CPointer) and isinstance(ty.dtype, DString):
            return types.uint64, val.ptr
        else:
            return ty, val


str_view_arg_handler = StrViewArgHandler()

# Masked scalars of all types
class MaskedType(types.Type):
    """
    A Numba type consisting of a value of some primitive type
    and a validity boolean, over which we can define math ops
    """

    def __init__(self, value):
        # MaskedType in Numba shall be parameterized
        # with a value type

        # TODO - replace object with stringview immediately
        if isinstance(value, (types.PyObject, StringView, DString)):
            self.value_type = dstring
        elif isinstance(value, SUPPORTED_NUMBA_TYPES):
            self.value_type = value
        else:
            # Unsupported Dtype. Numba tends to print out the type info
            # for whatever operands and operation failed to type and then
            # output its own error message. Putting the message in the repr
            # then is one way of getting the true cause to the user
            self.value_type = types.Poison(
                "\n\n\n Unsupported MaskedType. This is usually caused by "
                "attempting to use a column of unsupported dtype in a UDF. "
                f"Supported dtypes are {SUPPORTED_NUMBA_TYPES}"
            )
        super().__init__(name=f"Masked({self.value_type})")

    def __hash__(self):
        """
        Needed so that numba caches type instances with different
        `value_type` separately.
        """
        return self.__repr__().__hash__()

    def unify(self, context, other):
        """
        Often within a UDF an instance arises where a variable could
        be a `MaskedType`, an `NAType`, or a literal based off
        the data at runtime, for example the variable `ret` here:

        def f(x):
            if x == 1:
                ret = x
            elif x > 2:
                ret = 1
            else:
                ret = cudf.NA
            return ret

        When numba analyzes this function it will eventually figure
        out that the variable `ret` could be any of the three types
        from above. This scenario will only work if numba knows how
        to find some kind of common type between the possibilities,
        and this function implements that - the goal is to return a
        common type when comparing `self` to other.

        """

        # If we have Masked and NA, the output should be a
        # MaskedType with the original type as its value_type
        if isinstance(other, NAType):
            return self

        # two MaskedType unify to a new MaskedType whose value_type
        # is the result of unifying `self` and `other` `value_type`
        elif isinstance(other, MaskedType):
            return MaskedType(
                context.unify_pairs(self.value_type, other.value_type)
            )

        # if we have MaskedType and something that results in a
        # scalar, unify between the MaskedType's value_type
        # and that other thing
        unified = context.unify_pairs(self.value_type, other)
        if unified is None:
            # The value types don't unify, so there is no unified masked type
            return None

        return MaskedType(unified)

    def __eq__(self, other):
        # Equality is required for determining whether a cast is required
        # between two different types.
        if not isinstance(other, MaskedType):
            # Require a cast when the other type is not masked
            return False

        # Require a cast for another masked with a different value type
        return self.value_type == other.value_type

# For typing a Masked constant value defined outside a kernel (e.g. captured in
# a closure).
@typeof_impl.register(api.Masked)
def typeof_masked(val, c):
    return MaskedType(typeof(val.value))


# Implemented typing for Masked(value, valid) - the construction of a Masked
# type in a kernel.
@cuda_decl_registry.register
class MaskedConstructor(ConcreteTemplate):
    key = api.Masked
    units = ["ns", "ms", "us", "s"]
    datetime_cases = {types.NPDatetime(u) for u in units}
    timedelta_cases = {types.NPTimedelta(u) for u in units}
    cases = [
        nb_signature(MaskedType(t), t, types.boolean)
        for t in (
            types.integer_domain
            | types.real_domain
            | datetime_cases
            | timedelta_cases
            | {types.boolean}
            | {types.pyobject, string_view, dstring}
        )
    ]


# Provide access to `m.value` and `m.valid` in a kernel for a Masked `m`.
make_attribute_wrapper(MaskedType, "value", "value")
make_attribute_wrapper(MaskedType, "valid", "valid")
make_attribute_wrapper(StringView, "data", "data")

# Typing for `api.Masked`
@cuda_decl_registry.register_attr
class ClassesTemplate(AttributeTemplate):
    key = types.Module(api)

    def resolve_Masked(self, mod):
        return types.Function(MaskedConstructor)


# Registration of the global is also needed for Numba to type api.Masked
cuda_decl_registry.register_global(api, types.Module(api))
# For typing bare Masked (as in `from .api import Masked`
cuda_decl_registry.register_global(
    api.Masked, types.Function(MaskedConstructor)
)


# Tell numba how `MaskedType` is constructed on the backend in terms
# of primitive things that exist at the LLVM level
@register_model(MaskedType)
class MaskedModel(models.StructModel):
    def __init__(self, dmm, fe_type):
        # This struct has two members, a value and a validity
        # let the type of the `value` field be the same as the
        # `value_type` and let `valid` be a boolean
        members = [("value", fe_type.value_type), ("valid", types.bool_)]
        models.StructModel.__init__(self, dmm, fe_type, members)


class NAType(types.Type):
    """
    A type for handling ops against nulls
    Exists so we can:
    1. Teach numba that all occurrences of `cudf.NA` are
       to be read as instances of this type instead
    2. Define ops like `if x is cudf.NA` where `x` is of
       type `Masked` to mean `if x.valid is False`
    """

    def __init__(self):
        super().__init__(name="NA")

    def unify(self, context, other):
        """
        Masked  <-> NA is deferred to MaskedType.unify()
        Literal <-> NA -> Masked
        """
        if isinstance(other, MaskedType):
            # bounce to MaskedType.unify
            return None
        elif isinstance(other, NAType):
            # unify {NA, NA} -> NA
            return self
        else:
            return MaskedType(other)


na_type = NAType()


@typeof_impl.register(_NAType)
def typeof_na(val, c):
    """
    Tie instances of _NAType (cudf.NA) to our NAType.
    Effectively make it so numba sees `cudf.NA` as an
    instance of this NAType -> handle it accordingly.
    """
    return na_type


register_model(NAType)(models.OpaqueModel)


# Ultimately, we want numba to produce PTX code that specifies how to implement
# an operation on two singular `Masked` structs together, which is defined
# as producing a new `Masked` with the right validity and if valid,
# the correct value. This happens in two phases:
#   1. Specify that `Masked` <op> `Masked` exists and what it should return
#   2. Implement how to actually do (1) at the LLVM level
# The following code accomplishes (1) - it is really just a way of specifying
# that the <op> has a CUDA overload that accepts two `Masked` that
# are parameterized with `value_type` and what flavor of `Masked` to return.
class MaskedScalarArithOp(AbstractTemplate):
    def generic(self, args, kws):
        """
        Typing for `Masked` <op> `Masked`
        Numba expects a valid numba type to be returned if typing is successful
        else `None` signifies the error state (this pattern is commonly used
        in Numba)
        """
        if isinstance(args[0], MaskedType) and isinstance(args[1], MaskedType):
            # In the case of op(Masked, Masked), the return type is a Masked
            # such that Masked.value is the primitive type that would have
            # been resolved if we were just operating on the
            # `value_type`s.
            return_type = self.context.resolve_function_type(
                self.key, (args[0].value_type, args[1].value_type), kws
            ).return_type
            return nb_signature(MaskedType(return_type), args[0], args[1])


class MaskedScalarUnaryOp(AbstractTemplate):
    def generic(self, args, kws):
        if len(args) == 1 and isinstance(args[0], MaskedType):
            return_type = self.context.resolve_function_type(
                self.key, (args[0].value_type,), kws
            ).return_type
            return nb_signature(MaskedType(return_type), args[0])


class MaskedScalarNullOp(AbstractTemplate):
    def generic(self, args, kws):
        """
        Typing for `Masked` + `NA`
        Handles situations like `x + cudf.NA`
        """
        if isinstance(args[0], MaskedType) and isinstance(args[1], NAType):
            # In the case of op(Masked, NA), the result has the same
            # dtype as the original regardless of what it is
            return nb_signature(
                args[0],
                args[0],
                na_type,
            )
        elif isinstance(args[0], NAType) and isinstance(args[1], MaskedType):
            return nb_signature(args[1], na_type, args[1])


class MaskedScalarScalarOp(AbstractTemplate):
    def generic(self, args, kws):
        """
        Typing for `Masked` <op> a scalar (and vice-versa).
        handles situations like `x + 1`
        """
        # In the case of op(Masked, scalar), we resolve the type between
        # the Masked value_type and the scalar's type directly
        to_resolve_types = None
        if isinstance(args[0], MaskedType) and isinstance(
            args[1], SUPPORTED_NUMBA_TYPES
        ):
            to_resolve_types = (args[0].value_type, args[1])
        elif isinstance(args[0], SUPPORTED_NUMBA_TYPES) and isinstance(
            args[1], MaskedType
        ):
            to_resolve_types = (args[1].value_type, args[0])
        else:
            # fail typing
            return None
        return_type = self.context.resolve_function_type(
            self.key, to_resolve_types, kws
        ).return_type
        return nb_signature(
            MaskedType(return_type),
            args[0],
            args[1],
        )


@cuda_decl_registry.register_global(operator.is_)
class MaskedScalarIsNull(AbstractTemplate):
    """
    Typing for `Masked is cudf.NA`
    """

    def generic(self, args, kws):
        if isinstance(args[0], MaskedType) and isinstance(args[1], NAType):
            return nb_signature(types.boolean, args[0], na_type)
        elif isinstance(args[1], MaskedType) and isinstance(args[0], NAType):
            return nb_signature(types.boolean, na_type, args[1])


@cuda_decl_registry.register_global(operator.truth)
class MaskedScalarTruth(AbstractTemplate):
    """
    Typing for `if Masked`
    Used for `if x > y`
    The truthiness of a MaskedType shall be the truthiness
    of the `value` stored therein
    """

    def generic(self, args, kws):
        if isinstance(args[0], MaskedType):
            return nb_signature(types.boolean, MaskedType(types.boolean))


@cuda_decl_registry.register_global(api.pack_return)
class UnpackReturnToMasked(AbstractTemplate):
    """
    Turn a returned MaskedType into its value and validity
    or turn a scalar into the tuple (scalar, True).
    """

    def generic(self, args, kws):
        if isinstance(args[0], MaskedType):
            # MaskedType(dtype, valid) -> MaskedType(dtype, valid)
            return nb_signature(args[0], args[0])
        elif isinstance(args[0], SUPPORTED_NUMBA_TYPES):
            # scalar_type -> MaskedType(scalar_type, True)
            return_type = MaskedType(args[0])
            return nb_signature(return_type, args[0])


# String functions
@cuda_decl_registry.register_global(len)
class MaskedDStringLength(AbstractTemplate):
    """
    provide the length of a cudf::string_view like struct
    """

    def generic(self, args, kws):
        if isinstance(args[0], MaskedType) and isinstance(
            args[0].value_type, DString
        ):
            return nb_signature(MaskedType(types.int32), args[0])


@cuda_decl_registry.register_global(len)
class StringLiteralLength(AbstractTemplate):
    """
    provide the length of a python string literal by first
    converting to a cudf::string_view first
    """

    def generic(self, args, kws):
        if isinstance(args[0], types.StringLiteral) and len(args) == 1:
            return nb_signature(types.int32, args[0])


for binary_op in arith_ops + bitwise_ops + comparison_ops:
    # Every op shares the same typing class
    cuda_decl_registry.register_global(binary_op)(MaskedScalarArithOp)
    cuda_decl_registry.register_global(binary_op)(MaskedScalarNullOp)
    cuda_decl_registry.register_global(binary_op)(MaskedScalarScalarOp)

for unary_op in unary_ops:
    cuda_decl_registry.register_global(unary_op)(MaskedScalarUnaryOp)


class MaskedStringStartsWith(AbstractTemplate):
    key = "MaskedType.startswith"

    def generic(self, args, kws):
        return nb_signature(
            types.boolean, MaskedType(dstring), recvr=self.this
        )


class MaskedStringEndsWith(AbstractTemplate):
    key = "MaskedType.endswith"

    def generic(self, args, kws):
        return nb_signature(
            types.boolean, MaskedType(dstring), recvr=self.this
        )


class MaskedStringFind(AbstractTemplate):
    key = "MaskedType.find"

    def generic(self, args, kws):
        return nb_signature(
            types.int32, MaskedType(dstring), recvr=self.this
        )


class MaskedStringRFind(AbstractTemplate):
    key = "MaskedType.rfind"

    def generic(self, args, kws):
        return nb_signature(
            types.int32, MaskedType(dstring), recvr=self.this
        )

class MaskedStringUpper(AbstractTemplate):
    key = "MaskedType.upper"

    def generic(self, args, kws):
        return nb_signature(
            MaskedType(dstring), recvr=self.this
        )

class MaskedStringLower(AbstractTemplate):
    key = "MaskedType.lower"

    def generic(self, args, kws):
        return nb_signature(
            MaskedType(dstring), recvr=self.this
        )

@cuda_decl_registry.register_attr
class MaskedDStringAttrs(AttributeTemplate):
    key = MaskedType(dstring)

    def resolve_startswith(self, mod):
        return types.BoundFunction(
            MaskedStringStartsWith, MaskedType(dstring)
        )

    def resolve_endswith(self, mod):
        return types.BoundFunction(
            MaskedStringEndsWith, MaskedType(dstring)
        )

    def resolve_find(self, mod):
        return types.BoundFunction(
            MaskedStringFind, MaskedType(dstring)
        )

    def resolve_rfind(self, mod):
        return types.BoundFunction(
            MaskedStringRFind, MaskedType(dstring)
        )

    def resolve_upper(self, mod):
        return types.BoundFunction(
            MaskedStringUpper, MaskedType(dstring)
        )

    def resolve_lower(self, mod):
        return types.BoundFunction(
            MaskedStringLower, MaskedType(dstring)
        )

    def resolve_value(self, mod):
        return dstring

    def resolve_valid(self, mod):
        return types.boolean



_dstring_len = cuda.declare_device(
    "len", types.int32(types.CPointer(dstring))
)
_dstring_startswith = cuda.declare_device(
    "startswith",
    types.boolean(types.CPointer(dstring), types.CPointer(dstring)),
)
_dstring_endswith = cuda.declare_device(
    "endswith",
    types.boolean(types.CPointer(dstring), types.CPointer(dstring)),
)
_dstring_find = cuda.declare_device(
    "find",
    types.int32(types.CPointer(dstring), types.CPointer(dstring)),
)
_dstring_rfind = cuda.declare_device(
    "rfind",
    types.int32(types.CPointer(dstring), types.CPointer(dstring)),
)

_dstring_upper = cuda.declare_device(
    "upper",
    types.int32(types.CPointer(dstring), types.CPointer(dstring)),
)

_dstring_lower = cuda.declare_device(
    "lower",
    types.int32(types.CPointer(dstring), types.CPointer(dstring))
)

_create_dstring_from_stringview = cuda.declare_device(
    "create_dstring_from_stringview",
    types.int32(types.CPointer(string_view), types.CPointer(dstring))
)
