#include <libdash.h>
#include <gtest/gtest.h>
#include "TestBase.h"
#include "MakePatternTest.h"

#include <dash/MakePattern.h>

using namespace dash;

TEST_F(MakePatternTest, DefaultTraits)
{
  size_t extent_x   = 20 * _dash_size;
  size_t extent_y   = 30 * _dash_size;
  auto sizespec     = dash::SizeSpec<2>(extent_x, extent_y);
  auto teamspec     = dash::TeamSpec<2>(_dash_size, 1);

  auto dflt_pattern = dash::make_pattern(
                        sizespec,
                        teamspec);
  // Test pattern type traits:
  ASSERT_TRUE_U(
    dash::pattern_layout_traits<
      decltype(dflt_pattern)
    >::type::local_strided);
}

TEST_F(MakePatternTest, VarArgTags)
{
  size_t extent_x   = 20 * _dash_size;
  size_t extent_y   = 30 * _dash_size;
  auto sizespec     = dash::SizeSpec<2>(extent_x, extent_y);
  auto teamspec     = dash::TeamSpec<2>(_dash_size, 1);

  // Tiled pattern with one tag in partitioning property category and two tags
  // in mapping property category:
  auto tile_pattern   = dash::make_pattern<
                          // Blocking constraints:
                          pattern_partitioning_properties<
                            // same number of elements in every block
                            pattern_partitioning_tag::balanced
                          >,
                          // Topology constraints:
                          pattern_mapping_properties<
                            // same amount of blocks for every process
                            pattern_mapping_tag::balanced,
                            // every process mapped in every row/column
                            pattern_mapping_tag::diagonal
                          >,
                          // Linearization constraints:
                          pattern_layout_properties<
                            // elements contiguous within block
                            pattern_layout_tag::local_phase
                          >
                        >(sizespec, teamspec);
  // Test pattern type traits:
  ASSERT_FALSE_U(
    dash::pattern_layout_traits <
      decltype(tile_pattern)
    >::type::local_strided);
  ASSERT_TRUE_U(
    dash::pattern_layout_traits<
      decltype(tile_pattern)
    >::type::local_phase);
  ASSERT_TRUE_U(
    dash::pattern_partitioning_traits <
      decltype(tile_pattern)
    >::type::balanced);
  ASSERT_TRUE_U(
    dash::pattern_mapping_traits <
      decltype(tile_pattern)
    >::type::diagonal);
  ASSERT_TRUE_U(
    dash::pattern_mapping_traits <
      decltype(tile_pattern)
    >::type::balanced);

  // Strided pattern with two tags in partitioning property category and one
  // tag in mapping property category:
  auto stride_pattern = dash::make_pattern<
                          // Blocking constraints:
                          pattern_partitioning_properties<
                            // same number of elements in every block
                            pattern_partitioning_tag::balanced,
                            // elements in block should fit into a cache line
                            pattern_partitioning_tag::cache_align
                          >,
                          // Topology constraints:
                          pattern_mapping_properties<
                            // same amount of blocks for every process
                            pattern_mapping_tag::balanced,
                            // Unit mapped to block differs from neighbors
                            pattern_mapping_tag::remote_neighbors
                          >,
                          // Linearization constraints:
                          pattern_layout_properties<
                            // elements contiguous within block
                            pattern_layout_tag::local_strided
                          >
                        >(sizespec, teamspec);
  // Test pattern type traits:
  ASSERT_TRUE_U(
    dash::pattern_layout_traits<
      decltype(stride_pattern)
    >::type::local_strided);
  ASSERT_FALSE_U(
    dash::pattern_layout_traits<
      decltype(stride_pattern)
    >::type::local_phase);
}
