# unix specific packaging
# put anything here that applies to both linux and macos

# return here if building a macos .app
if(APPLE AND NOT SUNSHINE_BUILD_HOMEBREW)
    return()
endif()

# Installation destination dir
set(CPACK_SET_DESTDIR true)
if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "/usr/share/vibeshine")
endif()

# Native Linux lifecycle hooks require a regular, capability-free public inode.
# CMake's executable VERSION property otherwise installs a versioned binary plus
# a symlink, which those checks correctly reject. Application/package versions
# remain encoded in their existing metadata, independently of the file name.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT SUNSHINE_BUILD_APPIMAGE AND NOT SUNSHINE_BUILD_FLATPAK)
    set_property(TARGET sunshine PROPERTY VERSION)
endif()
install(TARGETS sunshine RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
