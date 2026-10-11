# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# CPack post-build hook: adds the build date and git commit to each Debian package.
#
# CPack has no variable for a custom control field, so this script (run by
# CPACK_POST_BUILD_SCRIPTS) unpacks every built .deb, appends XB-Git-Commit and
# XB-Build-Date to its control file and repacks it.  The fields go beside the
# version, never into it (policies/packaging-and-release-model.md,
# "Identifying a build").  Only CPACK_-prefixed variables reach this script.

if(NOT CPACK_TG_GIT_COMMIT OR NOT CPACK_TG_BUILD_DATE)
    message(FATAL_ERROR
        "build metadata is missing: CPACK_TG_GIT_COMMIT and "
        "CPACK_TG_BUILD_DATE must be set in CMakeLists.txt")
endif()

find_program(DPKG_DEB_EXECUTABLE dpkg-deb)
if(NOT DPKG_DEB_EXECUTABLE)
    message(FATAL_ERROR "dpkg-deb not found: the packages would carry no build metadata")
endif()

foreach(_pkg IN LISTS CPACK_PACKAGE_FILES)
    if(NOT _pkg MATCHES "\\.deb$")
        continue()
    endif()

    get_filename_component(_name "${_pkg}" NAME)
    set(_work "${CPACK_TOPLEVEL_DIRECTORY}/build-metadata/${_name}")
    file(REMOVE_RECURSE "${_work}")
    file(MAKE_DIRECTORY "${_work}")

    execute_process(COMMAND "${DPKG_DEB_EXECUTABLE}" -R "${_pkg}" "${_work}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "could not unpack ${_pkg}")
    endif()

    file(READ "${_work}/DEBIAN/control" _control)
    string(STRIP "${_control}" _control)
    file(WRITE "${_work}/DEBIAN/control"
         "${_control}\nXB-Git-Commit: ${CPACK_TG_GIT_COMMIT}\nXB-Build-Date: ${CPACK_TG_BUILD_DATE}\n")

    execute_process(COMMAND "${DPKG_DEB_EXECUTABLE}" --root-owner-group
                            -b "${_work}" "${_pkg}"
                    OUTPUT_QUIET
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "could not repack ${_pkg} after adding metadata")
    endif()

    file(REMOVE_RECURSE "${_work}")
    message(STATUS "build metadata added to ${_name}")
endforeach()
