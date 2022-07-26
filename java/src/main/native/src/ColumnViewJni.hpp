/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION.
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

#include <cudf/column/column.hpp>
#include <cudf/lists/lists_column_view.hpp>
#include <cudf/utilities/default_stream.hpp>
#include <rmm/cuda_stream_view.hpp>

namespace cudf::jni {

/**
 * @brief Creates a deep copy of the exemplar column, with its validity set to the equivalent
 * of the boolean `validity` column's value.
 *
 * The bool_column must have the same number of rows as the exemplar column.
 * The result column will have the same number of rows as the exemplar.
 * For all indices `i` where the boolean column is `true`, the result column will have a valid value
 * at index i. For all other values (i.e. `false` or `null`), the result column will have nulls.
 *
 * @param exemplar The column to be deep copied.
 * @param bool_column bool column whose value is to be used as the validity.
 * @return Deep copy of the exemplar, with the replaced validity.
 */
std::unique_ptr<cudf::column>
new_column_with_boolean_column_as_validity(cudf::column_view const &exemplar,
                                           cudf::column_view const &bool_column);

/**
 * @brief Generates list offsets with lengths of each list.
 *
 * For example,
 * Given a list column: [[1,2,3], [4,5], [6], [], [7,8]]
 * The list lengths of it: [3, 2, 1, 0, 2]
 * The list offsets of it: [0, 3, 5, 6, 6, 8]
 *
 * @param list_length The column represents list lengths.
 * @return The column represents list offsets.
 */
std::unique_ptr<cudf::column>
generate_list_offsets(cudf::column_view const &list_length,
                      rmm::cuda_stream_view stream = cudf::default_stream_value);

/**
 * @brief Perform a special treatment for the results of `cudf::lists::list_overlap` to produce the
 *        results that match with Spark's `arrays_overlap`.
 *
 * The function `arrays_overlap` of Apache Spark has a special behavior that needs to be addressed.
 * In particular, the result of checking overlap between two lists will be a null element instead of
 * a `false` value (as output by `cudf::lists::list_overlap`) if:
 *  - Both of the the input lists have no non-null common element, and
 *  - They are both non-empty, and
 *  - Either of them contains null elements.
 *
 * This function performs post-processing on the results of `cudf::lists::list_overlap`, adding
 * special treatment to produce an output column that matches with the behavior described above.
 *
 * @param lhs The input lists column for one side.
 * @param rhs The input lists column for the other side.
 * @param overlap_result The result column generated by `cudf::lists::list_overlap`.
 */
void post_process_list_overlap(cudf::column_view const &lhs, cudf::column_view const &rhs,
                               std::unique_ptr<cudf::column> const &overlap_result,
                               rmm::cuda_stream_view stream = cudf::default_stream_value);

/**
 * @brief Generates lists column by copying elements that are distinct by key from each input list
 * row to the corresponding output row.
 *
 * The input lists column must be given such that each list element is a struct of <key, value>
 * pair. With such input, a list containing distinct by key elements are defined such that the keys
 * of all elements in the list are distinct (i.e., any two keys are always compared unequal).
 *
 * There will not be any validity check for the input. The caller is responsible to make sure that
 * the input lists column has the right structure.
 *
 * @return A new list columns in which the elements in each list are distinct by key.
 */
std::unique_ptr<cudf::column> lists_distinct_by_key(cudf::lists_column_view const &input,
                                                    rmm::cuda_stream_view stream);

} // namespace cudf::jni
