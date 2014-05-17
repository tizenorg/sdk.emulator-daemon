cmake_minimum_required(VERSION 2.6)
project(emuld CXX)

IF("$ENV{CFLAGS}" MATCHES "-DMOBILE")
	# to install pkgconfig setup file.
	SET(EXEC_PREFIX "\${prefix}")
	SET(INCLUDEDIR "\${prefix}/include")
	SET(VERSION 1.1)

	set(PROJECT_MAJOR_VERSION "1")
	set(PROJECT_MINOR_VERSION "1")
	set(PROJECT_RELEASE_VERSION "0")
	set(CMAKE_VERBOSE_MAKEFILE OFF)

	OPTION(USE_D_MOBILE "Use Mobile Def" ON)

	SET(SRCS
		src/emuld.cpp
		src/evdi.cpp
		src/emuld_proc.cpp
		src/client.cpp
		src/device.cpp
	)

	INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include)

	INCLUDE(FindPkgConfig)

	SET(PKG_MODULE
		vconf
		deviced
		dlog
		pmapi
	)

	pkg_check_modules(PKGS REQUIRED ${PKG_MODULE})

	ADD_DEFINITIONS("-DENABLE_DLOG_OUT")
	ADD_DEFINITIONS(-Wall -O3 -omit-frame-pointer)

	ADD_EXECUTABLE(${PROJECT_NAME} ${SRCS})

	TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${PKGS_LDFLAGS} -lpthread)

	INSTALL(TARGETS ${PROJECT_NAME} DESTINATION /usr/bin)
ELSE()
	INSTALL(FILES ${CMAKE_SOURCE_DIR}/wearable/${PROJECT_NAME} DESTINATION /usr/bin)
ENDIF()

