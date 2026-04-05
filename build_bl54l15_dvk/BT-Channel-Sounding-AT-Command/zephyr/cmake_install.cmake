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
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/arch/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/lib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/boards/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/subsys/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/drivers/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/nrf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/hostap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/mcuboot/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/trusted-firmware-m/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/cjson/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/azure-sdk-for-c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/cirrus-logic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/openthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/memfault-firmware-sdk/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/canopennode/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/chre/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/lz4/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/zscilib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/cmsis/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/cmsis-dsp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/cmsis-nn/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/cmsis_6/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/fatfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/hal_nordic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/hal_st/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/hal_tdk/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/hal_wurthelektronik/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/liblc3/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/libmetal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/littlefs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/loramac-node/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/lvgl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/mipi-sys-t/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/nanopb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/nrf_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/open-amp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/percepio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/picolibc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/segger/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/uoscore-uedhoc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/zcbor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/nrfxlib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/nrf_hw_models/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/modules/connectedhomeip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/kernel/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/cmake/flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/cmake/usage/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/erik/develop/gitlocal/BT-Channel-Sounding-AT-Command/build_bl54l15_dvk/BT-Channel-Sounding-AT-Command/zephyr/cmake/reports/cmake_install.cmake")
endif()

