# Install script for directory: /opt/nordic/ncs/v3.2.4/zephyr

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/Users/erik/develop/zephyrsdk/zephyr-sdk-0.17.4/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/arch/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/lib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/boards/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/subsys/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/drivers/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/nrf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/hostap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/mcuboot/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/trusted-firmware-m/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/cjson/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/azure-sdk-for-c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/cirrus-logic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/openthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/memfault-firmware-sdk/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/canopennode/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/chre/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/lz4/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/zscilib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/cmsis/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/cmsis-dsp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/cmsis-nn/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/cmsis_6/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/fatfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/hal_nordic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/hal_st/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/hal_tdk/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/hal_wurthelektronik/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/liblc3/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/libmetal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/littlefs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/loramac-node/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/lvgl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/mipi-sys-t/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/nanopb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/nrf_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/open-amp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/percepio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/picolibc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/segger/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/uoscore-uedhoc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/zcbor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/nrfxlib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/nrf_hw_models/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/modules/connectedhomeip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/kernel/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/cmake/flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/cmake/usage/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/Ezurio/tmp/cs_at_command/build_bl54l15_dvk/cs_at_command/zephyr/cmake/reports/cmake_install.cmake")
endif()

