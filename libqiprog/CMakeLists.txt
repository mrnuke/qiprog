##
##  qiprog - Reference implementation of the QiProg protocol
##
##  Copyright (C) 2013 Alexandru Gagniuc <mr.nuke.me@gmail.com>
##
## Permission is hereby granted, free of charge, to any person obtaining a copy
## of this software and associated documentation files (the "Software"), to deal
## in the Software without restriction, including without limitation the rights
## to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
## copies of the Software, and to permit persons to whom the Software is
## furnished to do so, subject to the following conditions:
##
## The above copyright notice and this permission notice shall be included in
## all copies or substantial portions of the Software.
##
## THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
## IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
## FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
## AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
## LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
## OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
## THE SOFTWARE.
##

cmake_minimum_required(VERSION 2.8)

project(libqiprog C)

include(FindPkgConfig)

#===============================================================================
#= Configurable options
#-------------------------------------------------------------------------------
option(DRIVER_USB_MASTER "Include USB support" ON)


#===============================================================================
#= Dependencies
#-------------------------------------------------------------------------------
if(DRIVER_USB_MASTER)
	find_package(PkgConfig)
	pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
endif()

#===============================================================================
#= Sources and build
#-------------------------------------------------------------------------------
add_definitions(-Wall -Wextra)

if(DRIVER_USB_MASTER)
	add_definitions(-DCONFIG_DRIVER_USB_MASTER=1)
	list(APPEND LIBQIPROG_SRCS src/usb_master.c)
	list(APPEND LIBQIPROG_INCLUDES ${LIBUSB_INCLUDE_DIRS})
	list(APPEND LIBQIPROG_LIBDIRS ${LIBUSB_LIBRARY_DIRS})
	list(APPEND LIBQIPROG_LINK_LIBS ${LIBUSB_LIBRARIES})
endif()


list(APPEND LIBQIPROG_INCLUDES include)

list(APPEND LIBQIPROG_SRCS
	src/core.c
	src/libqiprog.c
	src/util.c
)

include_directories(${LIBQIPROG_INCLUDES})
link_directories(${LIBQIPROG_LIBDIRS})

add_library(${PROJECT_NAME} STATIC
	${LIBQIPROG_SRCS})

target_link_libraries(${PROJECT_NAME} ${LIBQIPROG_LINK_LIBS})

# CMake, please give us libqiprog, not liblibqiprog
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME qiprog)
