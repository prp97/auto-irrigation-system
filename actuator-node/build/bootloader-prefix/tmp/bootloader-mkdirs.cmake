# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/paula/sed/esp-idf/components/bootloader/subproject"
  "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader"
  "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix"
  "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix/tmp"
  "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix/src/bootloader-stamp"
  "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix/src"
  "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/paula/sed/esp-workspace/auto-irrigation-system/actuator-node/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
