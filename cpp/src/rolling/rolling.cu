/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
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

#include <rolling/rolling_detail.hpp>
#include <cudf/rolling.hpp>
#include <cudf/types.hpp>
#include <cudf/column/column_view.hpp>
#include <cudf/column/column_device_view.cuh>
#include <cudf/column/column_factories.hpp>
#include <cudf/utilities/nvtx_utils.hpp>
#include <cudf/utilities/bit.hpp>
#include <cudf/detail/utilities/cuda.cuh>
#include <cudf/copying.hpp>

#include <rmm/device_scalar.hpp>

#include <memory>

namespace cudf {
namespace experimental {

namespace detail {

namespace { // anonymous

/**
 * @brief Computes the rolling window function
 *
 * @tparam ColumnType  Datatype of values pointed to by the pointers
 * @tparam agg_op  A functor that defines the aggregation operation
 * @tparam average Perform average across all valid elements in the window
 * @tparam block_size CUDA block size for the kernel
 * @tparam WindowIterator iterator type (inferred)
 * @param input Input column device view
 * @param output Output column device view
 * @param window_begin[in] Rolling window size iterator, accumulates from
 *                in_col[i-window+1] to in_col[i] inclusive
 * @param forward_window_begin[in] Rolling window size iterator in the forward
 *                direction, accumulates from in_col[i] to
 *                in_col[i+forward_window] inclusive
 * @param min_periods[in]  Minimum number of observations in window required to
 *                have a value, otherwise 0 is stored in the valid bit mask
 */
template <typename T, typename agg_op, bool average, int block_size, bool has_nulls,
          typename WindowIterator>
__launch_bounds__(block_size)
__global__
void gpu_rolling(column_device_view input,
                 mutable_column_device_view output,
                 size_type * __restrict__ output_valid_count,
                 WindowIterator window_begin,
                 WindowIterator forward_window_begin,
                 size_type min_periods)
{
  size_type i = blockIdx.x * block_size + threadIdx.x;
  size_type stride = block_size * gridDim.x;

  agg_op op;
  gdf_size_type warp_valid_count{0};

  auto active_threads = __ballot_sync(0xffffffff, i < input.size());
  while(i < input.size())
  {
    T val = agg_op::template identity<T>();
    // declare this as volatile to avoid some compiler optimizations that lead to incorrect results
    // for CUDA 10.0 and below (fixed in CUDA 10.1)
    volatile cudf::size_type count = 0;

    size_type window = window_begin[i];
    size_type forward_window = forward_window_begin[i];

    // compute bounds
    size_type start_index = max(0, i - window + 1);
    size_type end_index = min(input.size(), i + forward_window + 1);       // exclusive

    // aggregate
    // TODO: We should explore using shared memory to avoid redundant loads.
    //       This might require separating the kernel into a special version
    //       for dynamic and static sizes.
    for (size_type j = start_index; j < end_index; j++) {
      if (!has_nulls || input.is_valid(j)) {
        val = op(input.element<T>(j), val);
        count++;
      }
    }

    // check if we have enough input samples
    bool output_is_valid = (count >= min_periods);

    // set the mask
    cudf::bitmask_type result_mask{__ballot_sync(active_threads, output_is_valid)};

    // only one thread writes the mask
    if (0 == threadIdx.x % cudf::experimental::detail::warp_size) {
      output.set_mask_word(cudf::word_index(i), result_mask);
      warp_valid_count += __popc(result_mask);
    }

    // store the output value, one per thread
    if (output_is_valid)
      cudf::detail::store_output_functor<T, average>{}(output.element<T>(i), val, count);

    // process next element 
    i += stride;
    active_threads = __ballot_sync(active_threads, i < input.size());
  }

  // sum the valid counts across the whole block  
  gdf_size_type block_valid_count = 
    cudf::experimental::detail::single_lane_block_sum_reduce<block_size, 0>(warp_valid_count);
  
  if(threadIdx.x == 0) {
    atomicAdd(output_valid_count, block_valid_count);
  }
}

struct rolling_window_launcher
{
  template<typename T, typename agg_op, bool average, typename WindowIterator,
      typename std::enable_if_t<cudf::detail::is_supported<T, agg_op>(), std::nullptr_t> = nullptr>
  std::unique_ptr<column> dispatch_aggregation_type(column_view const& input,
                                                    WindowIterator window_begin,
                                                    WindowIterator forward_window_begin,
                                                    size_type min_periods,
                                                    rmm::mr::device_memory_resource *mr,
                                                    cudaStream_t stream)
  {
    if (input.size() == 0) return empty_like(input);

    cudf::nvtx::range_push("CUDF_ROLLING_WINDOW", cudf::nvtx::color::ORANGE);

    // output is always nullable
    std::unique_ptr<column> output =
        allocate_like(input, cudf::experimental::mask_allocation_policy::ALWAYS, mr);

    constexpr cudf::size_type block_size = 256;
    cudf::experimental::detail::grid_1d grid(input.size(), block_size);

    auto input_device_view = column_device_view::create(input);
    auto output_device_view = mutable_column_device_view::create(*output);

    rmm::device_scalar<size_type> device_valid_count{0, stream};

    if (input.has_nulls()) {
      gpu_rolling<T, agg_op, average, block_size, true><<<grid.num_blocks, block_size, 0, stream>>>
        (*input_device_view, *output_device_view, device_valid_count.data(),
         window_begin, forward_window_begin, min_periods);
    } else {
      gpu_rolling<T, agg_op, average, block_size, false><<<grid.num_blocks, block_size, 0, stream>>>
        (*input_device_view, *output_device_view, device_valid_count.data(),
         window_begin, forward_window_begin, min_periods);
    }

    output->set_null_count(output->size() - device_valid_count.value(stream));

    // check the stream for debugging
    CHECK_STREAM(stream);

    cudf::nvtx::range_pop();

    return output;
  }

  /**
   * @brief If we cannot perform aggregation on this type then throw an error
   */
  template<typename T, typename agg_op, bool average, typename WindowIterator,
     typename std::enable_if_t<!cudf::detail::is_supported<T, agg_op>(), std::nullptr_t> = nullptr>
  std::unique_ptr<column> dispatch_aggregation_type(column_view const& input,
                                                    WindowIterator window_begin,
                                                    WindowIterator forward_window_begin,
                                                    size_type min_periods,
                                                    rmm::mr::device_memory_resource *mr,
                                                    cudaStream_t stream)
  {
    CUDF_FAIL("Unsupported column type/operation combo. Only `min` and `max` are supported for "
              "non-arithmetic types for aggregations.");
  }

  /**
   * @brief Helper function for gdf_rolling. Deduces the type of the
   * aggregation column and type and calls another function to invoke the
   * rolling window kernel.
   */
  template <typename T, typename WindowIterator>
  std::unique_ptr<column> operator()(column_view const& input,
                                     WindowIterator window_begin,
                                     WindowIterator forward_window_begin,
                                     size_type min_periods,
                                     rolling_operator op,
                                     rmm::mr::device_memory_resource *mr,
                                     cudaStream_t stream)
  {
    switch (op) {
    case rolling_operator::SUM:
      return dispatch_aggregation_type<T, cudf::DeviceSum, false>(input, window_begin,
                                                                  forward_window_begin, min_periods,
                                                                  mr, stream);
    case rolling_operator::MIN:
      return dispatch_aggregation_type<T, cudf::DeviceMin, false>(input, window_begin,
                                                                  forward_window_begin, min_periods,
                                                                  mr, stream);
    case rolling_operator::MAX:
      return dispatch_aggregation_type<T, cudf::DeviceMax, false>(input, window_begin,
                                                                  forward_window_begin, min_periods,
                                                                  mr, stream);
    case rolling_operator::COUNT:
      return dispatch_aggregation_type<T, cudf::DeviceCount, false>(input, window_begin,
                                                                    forward_window_begin, min_periods,
                                                                    mr, stream);
    case rolling_operator::MEAN:
      return dispatch_aggregation_type<T, cudf::DeviceSum, true>(input, window_begin,
                                                                 forward_window_begin, min_periods,
                                                                 mr, stream);
    default:
      // TODO: need a nice way to convert enums to strings, same would be useful for groupby
      CUDF_FAIL("Rolling aggregation function not implemented");
    }
  }
};

} // namespace anonymous

// Applies a rolling window function to the values in a column.
template <typename WindowIterator>
std::unique_ptr<column> rolling_window(column_view const& input,
                                       WindowIterator window_begin,
                                       WindowIterator forward_window_begin,
                                       size_type min_periods,
                                       rolling_operator op,
                                       rmm::mr::device_memory_resource* mr,
                                       cudaStream_t stream = 0)
{
  return cudf::experimental::type_dispatcher(input.type(),
                                             rolling_window_launcher{},
                                             input, window_begin, forward_window_begin,
                                             min_periods, op, mr, stream);
}

// Applies a user-defined rolling window function to the values in a column.
template <typename WindowIterator>
std::unique_ptr<column> rolling_window(column_view const &input,
                                       WindowIterator window_begin,
                                       WindowIterator forward_window_begin,
                                       size_type min_periods,
                                       std::string const& user_defined_aggregator,
                                       rolling_operator agg_op,
                                       data_type output_type,
                                       rmm::mr::device_memory_resource* mr,
                                       cudaStream_t stream = 0)
{
  // TODO
  return cudf::make_numeric_column(data_type{INT32}, 0);
}

} // namespace detail

// Applies a fixed-size rolling window function to the values in a column.
std::unique_ptr<column> rolling_window(column_view const& input,
                                       size_type window,
                                       size_type forward_window,
                                       size_type min_periods,
                                       rolling_operator op,
                                       rmm::mr::device_memory_resource* mr)
{
  CUDF_EXPECTS((window >= 0) && (min_periods >= 0) && (forward_window >= 0),
               "Window size and min periods must be non-negative");

  auto window_begin = thrust::make_constant_iterator(window);
  auto forward_window_begin = thrust::make_constant_iterator(forward_window);

  return cudf::experimental::detail::rolling_window(input, window_begin, forward_window_begin,
                                                    min_periods, op, mr, 0);
}

// Applies a variable-size rolling window function to the values in a column.
std::unique_ptr<column> rolling_window(column_view const& input,
                                       column_view const& window,
                                       column_view const& forward_window,
                                       size_type min_periods,
                                       rolling_operator op,
                                       rmm::mr::device_memory_resource* mr)
{
  if (window.size() == 0 || forward_window.size() == 0) return empty_like(input);

  CUDF_EXPECTS(window.type().id() == INT32 && forward_window.type().id() == INT32,
               "window/forward_window must have INT32 type");

  CUDF_EXPECTS(window.size() == input.size() && forward_window.size() == input.size(),
               "window/forward_window size must match input size");

  return cudf::experimental::detail::rolling_window(input, window.begin<size_type>(),
                                                    forward_window.begin<size_type>(), min_periods,
                                                    op, mr, 0);
}

// Applies a fixed-size user-defined rolling window function to the values in a column.
std::unique_ptr<column> rolling_window(column_view const &input,
                                       size_type window,
                                       size_type forward_window,
                                       size_type min_periods,
                                       std::string const& user_defined_aggregator,
                                       rolling_operator op,
                                       data_type output_type,
                                       rmm::mr::device_memory_resource* mr)
{
  CUDF_EXPECTS((window >= 0) && (min_periods >= 0) && (forward_window >= 0),
               "Window size and min periods must be non-negative");

  auto window_begin = thrust::make_constant_iterator(window);
  auto forward_window_begin = thrust::make_constant_iterator(forward_window);

  return cudf::experimental::detail::rolling_window(input, window_begin, forward_window_begin,
                                                    min_periods, user_defined_aggregator, op,
                                                    output_type, mr, 0);
}

// Applies a variable-size user-defined rolling window function to the values in a column.
std::unique_ptr<column> rolling_window(column_view const &input,
                                       column_view const& window,
                                       column_view const& forward_window,
                                       size_type min_periods,
                                       std::string const& user_defined_aggregator,
                                       rolling_operator op,
                                       data_type output_type,
                                       rmm::mr::device_memory_resource* mr)
{
  if (window.size() == 0 || forward_window.size() == 0) return empty_like(input);

  CUDF_EXPECTS(window.type().id() == INT32 && forward_window.type().id() == INT32,
               "window/forward_window must have INT32 type");

  CUDF_EXPECTS(window.size() != input.size() && forward_window.size() != input.size(),
               "window/forward_window size must match input size");

  return cudf::experimental::detail::rolling_window(input, window.begin<size_type>(),
                                                    forward_window.begin<size_type>(), min_periods,
                                                    user_defined_aggregator, op, output_type, mr, 0);
}

} // namespace experimental 
} // namespace cudf
