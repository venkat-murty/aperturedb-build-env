#define BOOST_TEST_MODULE foobar
#include <boost/test/unit_test.hpp>

#include "dir2/foobar.h"

BOOST_AUTO_TEST_CASE(pass)
{
  BOOST_CHECK_NE(foobar(), 42 + 42);
}
