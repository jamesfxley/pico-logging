# AR-1's cheap half: no printf-family call on the shipped log path, held by the
# rule in src/.clang-tidy that the lint gate runs over src/. This is the rule's
# negative control: it runs that configuration over a fixture calling snprintf
# and passes only when the call is refused with the rule's reason.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME pico_logging_printf_rule_refuses_snprintf
    COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/printf_rule_control.py
            ${PROJECT_SOURCE_DIR})
