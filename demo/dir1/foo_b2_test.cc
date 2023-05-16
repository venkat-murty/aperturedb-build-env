#define BOOST_TEST_MODULE foo
#include <boost/test/unit_test.hpp>

#include "dir1/foo.h"

BOOST_AUTO_TEST_CASE(pass)
{
  BOOST_CHECK_EQUAL(foo(), 42);
}
