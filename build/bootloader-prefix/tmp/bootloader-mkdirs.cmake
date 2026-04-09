# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/mathieu/esp/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/home/mathieu/esp/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader"
  "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix"
  "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix/tmp"
  "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix/src/bootloader-stamp"
  "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix/src"
  "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/media/mathieu/Nouveau nom/Hexacore/CLIENTS/LEXARO/PROJETS/LEXACARE/202508_Projet_054_Lexaro_TempCam/05_FIRMWARE/LexacareV1/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
