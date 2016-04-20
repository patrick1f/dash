#include <libdash.h>
#include <gtest/gtest.h>

#include "TestBase.h"
#include "GlobDynamicMemTest.h"


TEST_F(GlobDynamicMemTest, UnbalancedRealloc)
{
  typedef int value_t;

  if (dash::size() < 2) {
    LOG_MESSAGE(
      "GlobDynamicMemTest.UnbalancedRealloc requires at least two units");
    return;
  }

  LOG_MESSAGE("initializing GlobDynamicMem<T>");

  size_t initial_local_capacity  = 10;
  size_t initial_global_capacity = dash::size() * initial_local_capacity;
  dash::GlobDynamicMem<value_t> gdmem(initial_local_capacity);

  LOG_MESSAGE("initial global capacity: %d, initial local capacity: %d",
              initial_global_capacity, initial_local_capacity);

  EXPECT_EQ_U(initial_local_capacity,  gdmem.local_size());
  EXPECT_EQ_U(initial_local_capacity,  gdmem.lend(dash::myid()) -
                                       gdmem.lbegin(dash::myid()));
  EXPECT_EQ_U(initial_global_capacity, gdmem.size());

  dash::barrier();

  // Total changes of local capacity:
  int unit_0_lsize_diff = 120;
  int unit_1_lsize_diff =   6;
  int unit_x_lsize_diff =   5;
  int gsize_diff        = unit_0_lsize_diff +
                          unit_1_lsize_diff +
                          (dash::size() - 2) * unit_x_lsize_diff;

  DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                 "begin local reallocation");
  // Extend local size, changes should be locally visible immediately:
  if (dash::myid() == 0) {
    gdmem.grow(unit_0_lsize_diff);
    EXPECT_EQ_U(initial_local_capacity + unit_0_lsize_diff,
                gdmem.local_size());
  }
  else if (dash::myid() == 1) {
    gdmem.grow(unit_1_lsize_diff);
    EXPECT_EQ_U(initial_local_capacity + unit_1_lsize_diff,
                gdmem.local_size());
  } else {
    gdmem.grow(unit_x_lsize_diff);
    EXPECT_EQ_U(initial_local_capacity + unit_x_lsize_diff,
                gdmem.local_size());
  }

  dash::barrier();
  LOG_MESSAGE("before commit: global size: %d, local size: %d",
               gdmem.size(), gdmem.local_size());

  gdmem.commit();

  LOG_MESSAGE("after commit: global size: %d, local size: %d",
               gdmem.size(), gdmem.local_size());

  // Global size should be updated after commit:
  EXPECT_EQ_U(initial_global_capacity + gsize_diff, gdmem.size());

  // Local sizes should be unchanged after commit:
  if (dash::myid() == 0) {
    EXPECT_EQ_U(initial_local_capacity + unit_0_lsize_diff,
                gdmem.local_size());
  }
  else if (dash::myid() == 1) {
    EXPECT_EQ_U(initial_local_capacity + unit_1_lsize_diff,
                gdmem.local_size());
  } else {
    EXPECT_EQ_U(initial_local_capacity + unit_x_lsize_diff,
                gdmem.local_size());
  }
  dash::barrier();
  DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                 "size checks after commit completed");

  // Initialize values in reallocated memory:
  auto lmem = gdmem.lbegin();
  auto lcap = gdmem.local_size();
  for (int li = 0; li < lcap; ++li) {
    auto value = 1000 * (dash::myid() + 1) + li;
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                   "setting local offset", li, "at unit", dash::myid(),
                   "value:", value);
    lmem[li] = value;
  }
  dash::barrier();
  DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                 "initialization of local values completed");

  if (dash::myid() == 0) {
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                   "testing basic iterator arithmetic");

    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc", "git_first");
    auto git_first  = gdmem.begin();
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc", "git_second");
    auto git_second = git_first + 1;
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc", "git_remote");
    auto git_remote = git_first + gdmem.local_size() + 1;
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc", "git_last");
    auto git_last   = git_first + gdmem.size() - 1;
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc", "git_end");
    auto git_end    = git_first + gdmem.size();
  }
  dash::barrier();
  DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                 "testing basic iterator arithmetic completed");

  // Test memory space of units separately:
  for (dart_unit_t unit = 0; unit < dash::size(); ++unit) {
    if (dash::myid() != unit) {
      auto unit_git_begin = gdmem.at(unit, 0);
      auto unit_git_end   = gdmem.at(unit, gdmem.local_size(unit));
      auto exp_l_capacity = initial_local_capacity;
      if (unit == 0) {
        exp_l_capacity += unit_0_lsize_diff;
      } else if (unit == 1) {
        exp_l_capacity += unit_1_lsize_diff;
      } else {
        exp_l_capacity += unit_x_lsize_diff;
      }
      DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                     "remote unit:",          unit,
                     "expected local size:",  exp_l_capacity,
                     "gdm.local_size(unit):", gdmem.local_size(unit),
                     "git_end - git_begin:",  unit_git_end - unit_git_begin);
      EXPECT_EQ_U(exp_l_capacity, gdmem.local_size(unit));
      EXPECT_EQ_U(exp_l_capacity, unit_git_end - unit_git_begin);
      int l_idx = 0;
      for(auto it = unit_git_begin; it != unit_git_end; ++it, ++l_idx) {
        DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                       "requesting element at",
                       "local offset", l_idx,
                       "from unit",    unit);
        auto gptr = it.dart_gptr();
        DASH_LOG_TRACE_VAR("GlobDynamicMemTest.UnbalancedRealloc", gptr);

        // request value via DART global pointer:
        value_t dart_gptr_value;
        dart_get_blocking(&dart_gptr_value, gptr, sizeof(value_t));
        DASH_LOG_TRACE_VAR("GlobDynamicMemTest.UnbalancedRealloc", dart_gptr_value);

        // request value via DASH global iterator:
        value_t git_value = *it;
        DASH_LOG_TRACE_VAR("GlobDynamicMemTest.UnbalancedRealloc", git_value);

        value_t expected = 1000 * (unit + 1) + l_idx;
        EXPECT_EQ_U(expected, dart_gptr_value);
        EXPECT_EQ_U(expected, git_value);
      }
    }
  }
  dash::barrier();

  DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                 "testing reverse iteration");

  // Test memory space of all units by iterating global index space:
  auto unit         = dash::size() - 1;
  auto local_offset = gdmem.local_size(unit) - 1;
  // Invert order to test reverse iterators:
  auto rgend        = gdmem.rend();
  EXPECT_EQ_U(gdmem.size(), gdmem.rend() - gdmem.rbegin());
  for (auto rgit = gdmem.rbegin(); rgit != rgend; ++rgit) {
    DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                   "requesting element at",
                   "local offset", local_offset,
                   "from unit",    unit);
    value_t expected   = 1000 * (unit + 1) + local_offset;
    value_t rgit_value = *rgit;
    DASH_LOG_TRACE_VAR("GlobDynamicMemTest.UnbalancedRealloc", rgit_value);
    value_t git_value  = *gdmem.at(unit, local_offset);
    DASH_LOG_TRACE_VAR("GlobDynamicMemTest.UnbalancedRealloc", git_value);

    EXPECT_EQ_U(expected, rgit_value);
    EXPECT_EQ_U(expected, git_value);
    if (local_offset == 0 && unit > 0) {
      --unit;
      local_offset = gdmem.local_size(unit);
    }
    --local_offset;
  }

  DASH_LOG_TRACE("GlobDynamicMemTest.UnbalancedRealloc",
                 "testing reverse iteration completed");
}

TEST_F(GlobDynamicMemTest, LocalVisibility)
{
  typedef int value_t;

  if (dash::size() < 2) {
    LOG_MESSAGE(
      "GlobDynamicMemTest.LocalVisibility requires at least two units");
    return;
  }

  LOG_MESSAGE("initializing GlobDynamicMem<T>");

  size_t initial_local_capacity  = 10;
  size_t initial_global_capacity = dash::size() * initial_local_capacity;
  dash::GlobDynamicMem<value_t> gdmem(initial_local_capacity);

  LOG_MESSAGE("initial global capacity: %d, initial local capacity: %d",
              initial_global_capacity, initial_local_capacity);
  dash::barrier();

  // Total changes of local capacity:
  int unit_0_lsize_diff =  5;
  int unit_1_lsize_diff = -2;

  if (dash::myid() == 0) {
    // results in 2 buckets to attach, 0 to detach
    gdmem.grow(3);
    gdmem.shrink(2);
    gdmem.grow(5);
    gdmem.shrink(1);
  }
  if (dash::myid() == 1) {
    // results in 0 buckets to attach, 0 to detach
    gdmem.shrink(2);
    gdmem.grow(5);
    gdmem.shrink(2);
    gdmem.shrink(3);
  }

  dash::barrier();
  LOG_MESSAGE("global size: %d, local size: %d",
               gdmem.size(), gdmem.local_size());

  // Global memory space has not been updated yet, changes are only
  // visible locally.
  //
  // NOTE:
  // Local changes at units in same shared memory domain are visible
  // even when not committed yet.
  std::string my_host     = dash::util::Locality::Hostname(dash::myid());
  std::string unit_0_host = dash::util::Locality::Hostname(0);
  std::string unit_1_host = dash::util::Locality::Hostname(1);

  size_t expected_visible_size = initial_global_capacity;
  size_t expected_global_size  = initial_global_capacity;
  if (dash::myid() == 0) {
    expected_visible_size += unit_0_lsize_diff;
    if (my_host == unit_1_host) {
      LOG_MESSAGE("expected visible size: %d (locally) or %d (shmem)",
                  expected_visible_size,
                  expected_visible_size + unit_1_lsize_diff);
      // same shared memory domain as unit 1, changes at unit 1 might already
      // be visible to this unit:
      EXPECT_TRUE_U(
        gdmem.size() == expected_visible_size ||
        gdmem.size() == expected_visible_size + unit_1_lsize_diff);
    } else {
      EXPECT_EQ_U(expected_visible_size, gdmem.size());
    }
  }
  if (dash::myid() == 1) {
    expected_visible_size += unit_1_lsize_diff;
    if (my_host == unit_0_host) {
      LOG_MESSAGE("expected visible size: %d (locally) or %d (shmem)",
                  expected_visible_size,
                  expected_visible_size + unit_0_lsize_diff);
      // same shared memory domain as unit 0, changes at unit 0 might already
      // be visible to this unit:
      EXPECT_TRUE_U(
        gdmem.size() == expected_visible_size ||
        gdmem.size() == expected_visible_size + unit_0_lsize_diff);
    } else {
      EXPECT_EQ_U(expected_visible_size, gdmem.size());
    }
  }

  dash::barrier();
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "tests of visible memory size before commit passed");

  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "testing local capacities after grow/shrink");
  auto local_size = gdmem.lend() - gdmem.lbegin();
  EXPECT_EQ_U(local_size, gdmem.local_size());
  if (dash::myid() == 0) {
    EXPECT_EQ_U(local_size, initial_local_capacity + unit_0_lsize_diff);
  } else if (dash::myid() == 1) {
    EXPECT_EQ_U(local_size, initial_local_capacity + unit_1_lsize_diff);
  } else {
    EXPECT_EQ_U(local_size, initial_local_capacity);
  }

  // Initialize values in local memory:
  LOG_MESSAGE("initialize local values");
  auto lbegin = gdmem.lbegin();
  for (size_t li = 0; li < gdmem.local_size(); ++li) {
    value_t value = 100 * (dash::myid() + 1) + li;
    DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                   "local[", li, "] =", value);
    *(lbegin + li) = value;
  }

  dash::barrier();
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "tests of local capacities after grow/shrink passed");

  // Memory marked for deallocation is still accessible by other units:

  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "committing global memory");
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "local capacity before commit:",  gdmem.local_size());
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "global capacity before commit:", gdmem.size());
  // Collectively commit changes of local memory allocation to global
  // memory space:
  // register newly allocated local memory and remove local memory marked
  // for deallocation.
  gdmem.commit();
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility", "commit completed");

  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "local capacity after commit:",  gdmem.local_size());
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "global capacity after commit:", gdmem.size());

  // Changes are globally visible now:
  auto expected_global_capacity = initial_global_capacity +
                                  unit_0_lsize_diff + unit_1_lsize_diff;
  EXPECT_EQ_U(expected_global_capacity, gdmem.size());

  if (dash::myid() == 0) {
    DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility", "grow(30)");
    LOG_MESSAGE("grow(30)");
    gdmem.grow(30);
  }
  if (dash::myid() == 1) {
    DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility", "grow(30)");
    gdmem.grow(30);
  }
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "commit, balanced attach");
  gdmem.commit();
  // Capacity changes have been published globally:
  expected_global_capacity += (30 + 30);
  EXPECT_EQ(expected_global_capacity, gdmem.size());

  if (dash::myid() == 0) {
    // resizes attached bucket:
    DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility", "shrink(29)");
    gdmem.shrink(29);
  }
  if (dash::myid() == 1) {
    // marks bucket for detach:
    DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility", "shrink(30)");
    gdmem.shrink(30);
  }
  DASH_LOG_TRACE("GlobDynamicMemTest.LocalVisibility",
                 "commit, unbalanced detach");
  gdmem.commit();
  // Capacity changes have been published globally:
  expected_global_capacity -= (29 + 30);
  EXPECT_EQ(expected_global_capacity, gdmem.size());
}

TEST_F(GlobDynamicMemTest, RemoteAccess)
{
  typedef int value_t;

  if (dash::size() < 3) {
    LOG_MESSAGE(
      "GlobDynamicMemTest.RemoteAccess requires at least three units");
    return;
  }

  size_t initial_local_capacity  = 10;
  size_t initial_global_capacity = dash::size() * initial_local_capacity;
  dash::GlobDynamicMem<value_t> gdmem(initial_local_capacity);

  int unit_0_lsize_diff =  5;
  int unit_1_lsize_diff = -2;

  if (dash::myid() == 0) {
    gdmem.resize(initial_global_capacity + unit_0_lsize_diff);
  }
  if (dash::myid() == 1) {
    gdmem.resize(initial_global_capacity + unit_1_lsize_diff);
  }

  dash::barrier();

  if (dash::myid() > 1) {
    auto unit_0_local_size = gdmem.lend(0) - gdmem.lbegin(0);
    auto unit_1_local_size = gdmem.lend(1) - gdmem.lbegin(1);
    if (dash::myid() == 0) {
      EXPECT_EQ_U(unit_0_local_size, initial_local_capacity + 5);
      EXPECT_EQ_U(unit_0_local_size, gdmem.local_size());
    } else {
      EXPECT_EQ_U(unit_0_local_size, initial_local_capacity);
    }
    if (dash::myid() == 1) {
      EXPECT_EQ_U(unit_1_local_size, initial_local_capacity - 2);
      EXPECT_EQ_U(unit_1_local_size, gdmem.local_size());
    } else {
      EXPECT_EQ_U(unit_0_local_size, initial_local_capacity);
      EXPECT_EQ_U(unit_1_local_size, initial_local_capacity);
    }
  }

  if (dash::myid() != 1) {
    LOG_MESSAGE("requesting last local element from unit 1");
    auto unit_1_git_last = gdmem.at(1, initial_local_capacity-1);
    value_t actual;
    dash::get_value(&actual, unit_1_git_last);
    value_t expected = 100 * 2 + initial_local_capacity - 1;
    EXPECT_EQ_U(expected, actual);
  }
}
