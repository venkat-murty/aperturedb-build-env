


cc_library(
    name = "foobar",
    srcs = [
      "foobar.cc",
    ],
    hdrs = ["foobar.h"],
    deps = [
     "//dir1:foo"
     ]
)

cc_test(
   name = "foobar-test",
   srcs = [ "foobar_gtest.cc" ],
   deps = [
     ":foobar", 
     "@com_google_googletest//:gtest_main" 
   ]
)

