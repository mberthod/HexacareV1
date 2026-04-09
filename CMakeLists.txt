cmake_minimum_required(VERSION 3.16)

# Répertoire des composants personnalisés du projet
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

# Inclusion du système de build ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(LexacareV1)
