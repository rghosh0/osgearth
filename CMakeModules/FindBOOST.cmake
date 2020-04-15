# Locate BOOST.
# This module defines
# BOOST_INCLUDE_DIR, where to find the headers


UNSET(BOOST_INCLUDE_DIR CACHE)
FIND_PATH(BOOST_INCLUDE_DIR boost/geometry.hpp boost/any.hpp)

SET(BOOST_FOUND "NO")

IF(BOOST_INCLUDE_DIR)
  SET(BOOST_FOUND "YES")
ENDIF(BOOST_INCLUDE_DIR)



