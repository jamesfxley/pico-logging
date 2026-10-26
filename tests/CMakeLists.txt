# The test tree mirrors src/ (CXX-13.2), so src/ is exactly the shippable
# surface: an install, footprint, lint or coverage rule over it needs no test
# exclusion. One directory per module, added after src/ so every
# pico_logging::<module> target the tests link already exists.

add_subdirectory(core)
add_subdirectory(logging)
add_subdirectory(testing)
add_subdirectory(varargs)

# The documents' examples, compiled from the documents (EN-03.5).
add_subdirectory(docs)
