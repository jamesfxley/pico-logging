# The test tree mirrors src/, so src/ is exactly the shippable surface: an
# install, footprint, lint or coverage rule over it needs no test exclusion.
# There is one directory per module, added after src/ so every
# pico_logging::<module> target the tests link already exists.

add_subdirectory(core)
add_subdirectory(logging)
add_subdirectory(testing)
add_subdirectory(varargs)

# The documents' examples, compiled from the documents so that an API change
# that breaks one fails this build.
add_subdirectory(docs)
