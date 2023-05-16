# b2

```
b2 xfoo
b2 release xfoo
b2 debug xfoo
b2 coverage=on (generate coverage data)
b2 address-sanitizer=on (generate code with sanitizer or)
b2 foo_test (run the test)
```

# bazel

```
bazel build :all (build all targets)
bazel build :foo (build target foo)
bazel test :foo-test (run the foo-test)
bazel test :all (run all the tests)
bazel build //...:all (build everthing in the project)
```
