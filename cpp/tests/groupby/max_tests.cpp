/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <tests/groupby/groupby_test_util.hpp>

#include <cudf_test/base_fixture.hpp>
#include <cudf_test/column_wrapper.hpp>
#include <cudf_test/iterator_utilities.hpp>
#include <cudf_test/type_lists.hpp>

#include <cudf/detail/aggregation/aggregation.hpp>
#include <cudf/dictionary/update_keys.hpp>

#include <limits>

using namespace cudf::test::iterators;

namespace cudf {
namespace test {
template <typename V>
struct groupby_max_test : public cudf::test::BaseFixture {
};

using K = int32_t;
TYPED_TEST_SUITE(groupby_max_test, cudf::test::FixedWidthTypesWithoutFixedPoint);

TYPED_TEST(groupby_max_test, basic)
{
  using V = TypeParam;
  using R = cudf::detail::target_type_t<V, aggregation::MAX>;

  fixed_width_column_wrapper<K> keys{1, 2, 3, 1, 2, 2, 1, 3, 3, 2};
  fixed_width_column_wrapper<V> vals{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

  fixed_width_column_wrapper<K> expect_keys{1, 2, 3};
  fixed_width_column_wrapper<R> expect_vals({6, 9, 8});

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

TYPED_TEST(groupby_max_test, empty_cols)
{
  using V = TypeParam;
  using R = cudf::detail::target_type_t<V, aggregation::MAX>;

  fixed_width_column_wrapper<K> keys{};
  fixed_width_column_wrapper<V> vals{};

  fixed_width_column_wrapper<K> expect_keys{};
  fixed_width_column_wrapper<R> expect_vals{};

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

TYPED_TEST(groupby_max_test, zero_valid_keys)
{
  using V = TypeParam;
  using R = cudf::detail::target_type_t<V, aggregation::MAX>;

  fixed_width_column_wrapper<K> keys({1, 2, 3}, all_nulls());
  fixed_width_column_wrapper<V> vals({3, 4, 5});

  fixed_width_column_wrapper<K> expect_keys{};
  fixed_width_column_wrapper<R> expect_vals{};

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

TYPED_TEST(groupby_max_test, zero_valid_values)
{
  using V = TypeParam;
  using R = cudf::detail::target_type_t<V, aggregation::MAX>;

  fixed_width_column_wrapper<K> keys{1, 1, 1};
  fixed_width_column_wrapper<V> vals({3, 4, 5}, all_nulls());

  fixed_width_column_wrapper<K> expect_keys{1};
  fixed_width_column_wrapper<R> expect_vals({0}, all_nulls());

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

TYPED_TEST(groupby_max_test, null_keys_and_values)
{
  using V = TypeParam;
  using R = cudf::detail::target_type_t<V, aggregation::MAX>;

  fixed_width_column_wrapper<K> keys({1, 2, 3, 1, 2, 2, 1, 3, 3, 2, 4},
                                     {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1});
  fixed_width_column_wrapper<V> vals({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 4},
                                     {1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0});

  //  { 1, 1,     2, 2, 2,   3, 3,    4}
  fixed_width_column_wrapper<K> expect_keys({1, 2, 3, 4}, no_nulls());
  //  { 0, 3,     1, 4, 5,   2, 8,    -}
  fixed_width_column_wrapper<R> expect_vals({3, 5, 8, 0}, {1, 1, 1, 0});

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

struct groupby_max_string_test : public cudf::test::BaseFixture {
};

TEST_F(groupby_max_string_test, basic)
{
  fixed_width_column_wrapper<K> keys{1, 2, 3, 1, 2, 2, 1, 3, 3, 2};
  strings_column_wrapper vals{"año", "bit", "₹1", "aaa", "zit", "bat", "aaa", "$1", "₹1", "wut"};

  fixed_width_column_wrapper<K> expect_keys{1, 2, 3};
  strings_column_wrapper expect_vals({"año", "zit", "₹1"});

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

TEST_F(groupby_max_string_test, zero_valid_values)
{
  fixed_width_column_wrapper<K> keys{1, 1, 1};
  strings_column_wrapper vals({"año", "bit", "₹1"}, all_nulls());

  fixed_width_column_wrapper<K> expect_keys{1};
  strings_column_wrapper expect_vals({""}, all_nulls());

  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));

  auto agg2 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg2), force_use_sort_impl::YES);
}

TEST_F(groupby_max_string_test, max_sorted_strings)
{
  // testcase replicated in issue #8717
  cudf::test::strings_column_wrapper keys(
    {"",   "",   "",   "",   "",   "",   "06", "06", "06", "06", "10", "10", "10", "10", "14", "14",
     "14", "14", "18", "18", "18", "18", "22", "22", "22", "22", "26", "26", "26", "26", "30", "30",
     "30", "30", "34", "34", "34", "34", "38", "38", "38", "38", "42", "42", "42", "42"},
    {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
  cudf::test::strings_column_wrapper vals(
    {"", "", "",   "", "", "", "06", "", "", "", "10", "", "", "", "14", "",
     "", "", "18", "", "", "", "22", "", "", "", "26", "", "", "", "30", "",
     "", "", "34", "", "", "", "38", "", "", "", "42", "", "", ""},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,
     0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
  cudf::test::strings_column_wrapper expect_keys(
    {"06", "10", "14", "18", "22", "26", "30", "34", "38", "42", ""},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0});
  cudf::test::strings_column_wrapper expect_vals(
    {"06", "10", "14", "18", "22", "26", "30", "34", "38", "42", ""},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0});

  // fixed_width_column_wrapper<size_type> expect_argmax(
  // {6, 10, 14, 18, 22, 26, 30, 34, 38, 42, -1},
  // {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0});
  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(keys,
                  vals,
                  expect_keys,
                  expect_vals,
                  std::move(agg),
                  force_use_sort_impl::NO,
                  null_policy::INCLUDE,
                  sorted::YES);
}

struct groupby_dictionary_max_test : public cudf::test::BaseFixture {
};

TEST_F(groupby_dictionary_max_test, basic)
{
  using V = std::string;

  // clang-format off
  fixed_width_column_wrapper<K> keys{     1,     2,    3,     1,     2,     2,     1,    3,    3,    2 };
  dictionary_column_wrapper<V>  vals{ "año", "bit", "₹1", "aaa", "zit", "bat", "aaa", "$1", "₹1", "wut"};
  fixed_width_column_wrapper<K> expect_keys   {     1,     2,    3 };
  dictionary_column_wrapper<V>  expect_vals_w({ "año", "zit", "₹1" });
  // clang-format on

  auto expect_vals = cudf::dictionary::set_keys(expect_vals_w, vals.keys());

  test_single_agg(keys,
                  vals,
                  expect_keys,
                  expect_vals->view(),
                  cudf::make_max_aggregation<cudf::groupby_aggregation>());
  test_single_agg(keys,
                  vals,
                  expect_keys,
                  expect_vals->view(),
                  cudf::make_max_aggregation<cudf::groupby_aggregation>(),
                  force_use_sort_impl::YES);
}

TEST_F(groupby_dictionary_max_test, fixed_width)
{
  using V = int64_t;

  // clang-format off
  fixed_width_column_wrapper<K> keys{     1,     2,    3,     1,     2,     2,     1,    3,    3,    2 };
  dictionary_column_wrapper<V>  vals{ 0xABC, 0xBBB, 0xF1, 0xAAA, 0xFFF, 0xBAA, 0xAAA, 0x01, 0xF1, 0xEEE};
  fixed_width_column_wrapper<K> expect_keys    {     1,     2,    3 };
  fixed_width_column_wrapper<V>  expect_vals_w({ 0xABC, 0xFFF, 0xF1 });
  // clang-format on

  test_single_agg(keys,
                  vals,
                  expect_keys,
                  expect_vals_w,
                  cudf::make_max_aggregation<cudf::groupby_aggregation>());
  test_single_agg(keys,
                  vals,
                  expect_keys,
                  expect_vals_w,
                  cudf::make_max_aggregation<cudf::groupby_aggregation>(),
                  force_use_sort_impl::YES);
}

template <typename T>
struct FixedPointTestAllReps : public cudf::test::BaseFixture {
};

TYPED_TEST_SUITE(FixedPointTestAllReps, cudf::test::FixedPointTypes);

TYPED_TEST(FixedPointTestAllReps, GroupBySortMaxDecimalAsValue)
{
  using namespace numeric;
  using decimalXX  = TypeParam;
  using RepType    = cudf::device_storage_type_t<decimalXX>;
  using fp_wrapper = cudf::test::fixed_point_column_wrapper<RepType>;
  using K          = int32_t;

  for (auto const i : {2, 1, 0, -1, -2}) {
    auto const scale = scale_type{i};
    // clang-format off
    auto const keys  = fixed_width_column_wrapper<K>{1, 2, 3, 1, 2, 2, 1, 3, 3, 2};
    auto const vals  = fp_wrapper{                  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, scale};
    // clang-format on

    auto const expect_keys     = fixed_width_column_wrapper<K>{1, 2, 3};
    auto const expect_vals_max = fp_wrapper{{6, 9, 8}, scale};

    auto agg3 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
    test_single_agg(
      keys, vals, expect_keys, expect_vals_max, std::move(agg3), force_use_sort_impl::YES);
  }
}

TYPED_TEST(FixedPointTestAllReps, GroupByHashMaxDecimalAsValue)
{
  using namespace numeric;
  using decimalXX  = TypeParam;
  using RepType    = cudf::device_storage_type_t<decimalXX>;
  using fp_wrapper = cudf::test::fixed_point_column_wrapper<RepType>;
  using K          = int32_t;

  for (auto const i : {2, 1, 0, -1, -2}) {
    auto const scale = scale_type{i};
    // clang-format off
    auto const keys  = fixed_width_column_wrapper<K>{1, 2, 3, 1, 2, 2, 1, 3, 3, 2};
    auto const vals  = fp_wrapper{                  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, scale};
    // clang-format on

    auto const expect_keys     = fixed_width_column_wrapper<K>{1, 2, 3};
    auto const expect_vals_max = fp_wrapper{{6, 9, 8}, scale};

    auto agg7 = cudf::make_max_aggregation<cudf::groupby_aggregation>();
    test_single_agg(keys, vals, expect_keys, expect_vals_max, std::move(agg7));
  }
}

struct groupby_max_struct_test : public cudf::test::BaseFixture {
};

TEST_F(groupby_max_struct_test, basic)
{
  auto const keys = fixed_width_column_wrapper<int32_t>{1, 2, 3, 1, 2, 2, 1, 3, 3, 2};
  auto const vals = [] {
    auto child1 =
      strings_column_wrapper{"año", "bit", "₹1", "aaa", "zit", "bat", "aab", "$1", "€1", "wut"};
    auto child2 = fixed_width_column_wrapper<int32_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    return structs_column_wrapper{{child1, child2}};
  }();

  auto const expect_keys = fixed_width_column_wrapper<int32_t>{1, 2, 3};
  auto const expect_vals = [] {
    auto child1 = strings_column_wrapper{"año", "zit", "₹1"};
    auto child2 = fixed_width_column_wrapper<int32_t>{1, 5, 3};
    return structs_column_wrapper{{child1, child2}};
  }();

  auto agg = cudf::make_max_aggregation<groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));
}

TEST_F(groupby_max_struct_test, slice_input)
{
  constexpr int32_t dont_care{1};
  auto const keys_original = fixed_width_column_wrapper<int32_t>{
    dont_care, dont_care, 1, 2, 3, 1, 2, 2, 1, 3, 3, 2, dont_care};
  auto const vals_original = [] {
    auto child1 = strings_column_wrapper{"dont_care",
                                         "dont_care",
                                         "año",
                                         "bit",
                                         "₹1",
                                         "aaa",
                                         "zit",
                                         "bat",
                                         "aab",
                                         "$1",
                                         "€1",
                                         "wut",
                                         "dont_care"};
    auto child2 = fixed_width_column_wrapper<int32_t>{
      dont_care, dont_care, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, dont_care};
    return structs_column_wrapper{{child1, child2}};
  }();

  auto const keys        = cudf::slice(keys_original, {2, 12})[0];
  auto const vals        = cudf::slice(vals_original, {2, 12})[0];
  auto const expect_keys = fixed_width_column_wrapper<int32_t>{1, 2, 3};
  auto const expect_vals = [] {
    auto child1 = strings_column_wrapper{"año", "zit", "₹1"};
    auto child2 = fixed_width_column_wrapper<int32_t>{1, 5, 3};
    return structs_column_wrapper{{child1, child2}};
  }();

  auto agg = cudf::make_max_aggregation<groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));
}

TEST_F(groupby_max_struct_test, null_keys_and_values)
{
  constexpr int32_t null{0};
  auto const keys =
    fixed_width_column_wrapper<int32_t>{{1, 2, 3, 1, 2, 2, 1, null, 3, 2, 4}, null_at(7)};
  auto const vals = [] {
    auto child1 = strings_column_wrapper{
      "año", "bit", "₹1", "aaa", "zit", "" /*NULL*/, "" /*NULL*/, "$1", "€1", "wut", "" /*NULL*/};
    auto child2 = fixed_width_column_wrapper<int32_t>{9, 8, 7, 6, 5, null, null, 2, 1, 0, null};
    return structs_column_wrapper{{child1, child2}, nulls_at({5, 6, 10})};
  }();

  auto const expect_keys = fixed_width_column_wrapper<int32_t>{{1, 2, 3, 4}, no_nulls()};
  auto const expect_vals = [] {
    auto child1 = strings_column_wrapper{"año", "zit", "₹1", "" /*NULL*/};
    auto child2 = fixed_width_column_wrapper<int32_t>{9, 5, 7, null};
    return structs_column_wrapper{{child1, child2}, null_at(3)};
  }();

  auto agg = cudf::make_max_aggregation<groupby_aggregation>();
  test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));
}

TEST_F(groupby_max_struct_test, values_with_null_child)
{
  constexpr int32_t null{0};
  {
    auto const keys = fixed_width_column_wrapper<int32_t>{1, 1};
    auto const vals = [] {
      auto child1 = fixed_width_column_wrapper<int32_t>{1, 1};
      auto child2 = fixed_width_column_wrapper<int32_t>{{-1, null}, null_at(1)};
      return structs_column_wrapper{child1, child2};
    }();

    auto const expect_keys = fixed_width_column_wrapper<int32_t>{1};
    auto const expect_vals = [] {
      auto child1 = fixed_width_column_wrapper<int32_t>{1};
      auto child2 = fixed_width_column_wrapper<int32_t>{-1};
      return structs_column_wrapper{child1, child2};
    }();

    auto agg = cudf::make_max_aggregation<groupby_aggregation>();
    test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));
  }

  {
    auto const keys = fixed_width_column_wrapper<int32_t>{1, 1};
    auto const vals = [] {
      auto child1 = fixed_width_column_wrapper<int32_t>{{-1, null}, null_at(1)};
      auto child2 = fixed_width_column_wrapper<int32_t>{{null, null}, nulls_at({0, 1})};
      return structs_column_wrapper{child1, child2};
    }();

    auto const expect_keys = fixed_width_column_wrapper<int32_t>{1};
    auto const expect_vals = [] {
      auto child1 = fixed_width_column_wrapper<int32_t>{-1};
      auto child2 = fixed_width_column_wrapper<int32_t>{{null}, null_at(0)};
      return structs_column_wrapper{child1, child2};
    }();

    auto agg = cudf::make_max_aggregation<groupby_aggregation>();
    test_single_agg(keys, vals, expect_keys, expect_vals, std::move(agg));
  }
}

struct groupby_max_float_test : public cudf::test::BaseFixture {
};

TEST_F(groupby_max_float_test, values_with_infinity)
{
  using T          = double;
  using int32s_col = fixed_width_column_wrapper<int32_t>;
  using floats_col = fixed_width_column_wrapper<T, int32_t>;

  auto constexpr inf = std::numeric_limits<T>::infinity();

  auto const keys = int32s_col{1, 2, 1, 2};
  auto const vals = floats_col{1., 1., inf, 2.};

  auto const expected_keys = int32s_col{1, 2};
  auto const expected_vals = floats_col{inf, 2.};

  // Related issue: https://github.com/rapidsai/cudf/issues/11352
  // The issue only occurs in sort-based aggregation.
  auto agg = cudf::make_max_aggregation<cudf::groupby_aggregation>();
  test_single_agg(
    keys, vals, expected_keys, expected_vals, std::move(agg), force_use_sort_impl::YES);
}

}  // namespace test
}  // namespace cudf
