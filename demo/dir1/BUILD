

cc_library(
    name = "bar",
    srcs = [
      "bar.cc",
    ],
    hdrs = ["bar.h"],
)

cc_library(
    name = "foo",
    srcs = [
      "foo.cc",
    ],
    hdrs = ["foo.h"],
    deps = [ ":bar"],

    visibility = ["//visibility:public"],
)

cc_test(
   name = "foo-test",
   srcs = [ "foo_gtest.cc" ],
   deps = [
     ":foo", 
     "@com_google_googletest//:gtest_main" 
   ]
)

