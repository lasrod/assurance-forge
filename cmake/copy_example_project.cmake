# copy_example_project.cmake -- copy the example project beside the built
# executable, leaving out its .af/ folder.
#
# .af/ is the per-checkout audit history and draft workspace the app writes
# when a project is opened. The examples submodule ignores it, so a developer
# who ever opened the example has one; copied along, it made the shipped example
# open with "Audit log divergence detected".
#
# Inputs: AF_EXAMPLE_SOURCE, AF_EXAMPLE_DESTINATION.

foreach(_input AF_EXAMPLE_SOURCE AF_EXAMPLE_DESTINATION)
    if(NOT DEFINED ${_input})
        message(FATAL_ERROR "${_input} is not set")
    endif()
endforeach()

# Replaced, not merged, so a .af/ an earlier build copied does not survive.
file(REMOVE_RECURSE "${AF_EXAMPLE_DESTINATION}")
get_filename_component(_parent "${AF_EXAMPLE_DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${_parent}")
file(COPY "${AF_EXAMPLE_SOURCE}/" DESTINATION "${AF_EXAMPLE_DESTINATION}" PATTERN ".af" EXCLUDE)
