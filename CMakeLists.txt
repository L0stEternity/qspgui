cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

if ("${APP_VERSION}" STREQUAL "")
	set(APP_VERSION "0.0.0" CACHE STRING "App version" FORCE)
endif()

string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" CMAKE_APP_VERSION "${APP_VERSION}")

project(qspgui VERSION ${CMAKE_APP_VERSION} HOMEPAGE_URL "https://qsp.org")

include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-char-subscripts -Wno-unused-variable")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -D_DEBUG -DDEBUG")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -D_DEBUG -DDEBUG")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Ofast -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast -DNDEBUG")
elseif ("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /D_DEBUG /DDEBUG")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_DEBUG /DDEBUG")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "AppleClang")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -D_DEBUG -DDEBUG")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -D_DEBUG -DDEBUG")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Ofast -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast -DNDEBUG")
else()
	message(FATAL_ERROR "Compiler is not supported")
endif()

FetchContent_Declare(qsp-custom
	GIT_REPOSITORY https://github.com/QSPFoundation/qsp.git
	GIT_TAG master
	#GIT_SHALLOW true # it works only with branch names and tags
	GIT_PROGRESS true
	SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/qsp-download"
	SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/qsp-src"
	BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/qsp-build"
)
FetchContent_MakeAvailable(qsp-custom)

# Optional modern renderer built on wxWebView (WebView2 / WebKitGTK / WKWebView).
# The classic wxHtmlWindow renderer is always built; this only adds an alternative.
option(QSPGUI_USE_WEBVIEW "Build the wxWebView-based renderer in addition to the classic one" OFF)

if (USE_INSTALLED_WX)
	if (QSPGUI_USE_WEBVIEW)
		find_package(wxWidgets REQUIRED base core adv aui html net webview)
	else()
		find_package(wxWidgets REQUIRED base core adv aui html net)
	endif()
	include(${wxWidgets_USE_FILE})
else()
	FetchContent_Declare(wxWidgets-custom
		GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
		GIT_TAG 5d63efc902e8b29c05ee492ff0c732a929f7b096
		#GIT_SHALLOW true # it works only with branch names and tags
		GIT_PROGRESS true
		SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-download"
		SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-src"
		BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-build"
		PATCH_COMMAND git checkout --recurse-submodules -- . && git apply --check --apply --ignore-whitespace "${CMAKE_CURRENT_SOURCE_DIR}/build_wx/wxPatch.diff"
	)
	# Disable wxWidgets features we don't need
	set(wxUSE_ACTIVEX OFF)
	set(wxUSE_LIBTIFF OFF)
	set(wxUSE_SECRETSTORE OFF)
	set(wxUSE_LIBSDL OFF)
	set(wxUSE_LIBMSPACK OFF)
	set(wxUSE_GSTREAMER OFF)
	set(wxUSE_OPENGL OFF)
	set(wxUSE_RICHTEXT OFF)
	set(wxUSE_RIBBON OFF)
	set(wxUSE_MEDIACTRL OFF)
	set(wxUSE_PROPGRID OFF)
	set(wxUSE_XRC OFF)
	set(wxUSE_DEBUGREPORT OFF)
	set(wxUSE_STC OFF)
	if (QSPGUI_USE_WEBVIEW)
		set(wxUSE_WEBVIEW ON)
		if (WIN32)
			# Edge/Chromium backend only; the legacy IE backend needs ActiveX, which we keep off
			set(wxUSE_WEBVIEW_EDGE ON)
			# Static loader: keeps the player a single self-contained exe,
			# matching how the rest of it is linked
			set(wxUSE_WEBVIEW_EDGE_STATIC ON)
			set(wxUSE_WEBVIEW_IE OFF)
		endif()
	else()
		set(wxUSE_WEBVIEW OFF)
	endif()

	set(wxBUILD_MONOLITHIC OFF)
	set(wxBUILD_SAMPLES OFF)
	set(wxBUILD_TESTS OFF)
	set(wxBUILD_DEMOS OFF)
	set(wxBUILD_BENCHMARKS OFF)
	set(wxBUILD_OPTIMISE ON)
	set(wxBUILD_STRIPPED_RELEASE ON)
	set(wxBUILD_SHARED OFF)

	if (MSVC)
		set(wxBUILD_USE_STATIC_RUNTIME ON)
	else()
		set(wxBUILD_USE_STATIC_RUNTIME OFF)
	endif()

	FetchContent_MakeAvailable(wxWidgets-custom)
endif()

configure_file(qspgui_config.h.cmakein qspgui_config.h @ONLY)

set(QSPGUI_SOURCES
	qspgui/animwin.cpp
	qspgui/app.cpp
	qspgui/callbacks_gui.cpp
	qspgui/comtools.cpp
	qspgui/devjson.cpp
	qspgui/devserver.cpp
	qspgui/frame.cpp
	qspgui/imgcanvas.cpp
	qspgui/initevent.cpp
	qspgui/inputbox.cpp
	qspgui/inputdlg.cpp
	qspgui/listbox.cpp
	qspgui/msgdlg.cpp
	qspgui/textbox.cpp
	qspgui/saveslots.cpp
	qspgui/toast.cpp
	qspgui/transhelper.cpp
	qspgui/updateappdialog.cpp
	qspgui/sound/sound_engine.c
)
if (QSPGUI_USE_WEBVIEW)
	list(APPEND QSPGUI_SOURCES
		qspgui/webpane.cpp
		qspgui/webtextbox.cpp
		qspgui/weblistbox.cpp
		qspgui/webimgcanvas.cpp)
endif()
if (WIN32)
	list(APPEND QSPGUI_SOURCES qspgui/rsc/res.rc)
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/misc/common/icons/logo.ico" "${CMAKE_BINARY_DIR}/misc/icons/logo.ico" COPYONLY)
	add_executable(qspgui WIN32 ${QSPGUI_SOURCES})
	target_compile_definitions(qspgui PRIVATE _CRT_SECURE_NO_WARNINGS)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT qspgui)
	if (MSVC)
		target_link_options(qspgui PRIVATE /STACK:4194304)
	else()
		target_link_options(qspgui PRIVATE -Wl,--stack,4194304)
	endif()
elseif (APPLE)
	add_executable(qspgui MACOSX_BUNDLE ${QSPGUI_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/misc/macos/icon.icns")
	set_target_properties(qspgui PROPERTIES
		MACOSX_BUNDLE_BUNDLE_NAME "QSP Classic"
		MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}"
		MACOSX_BUNDLE_COPYRIGHT "(c) QSP Foundation"
		MACOSX_BUNDLE_GUI_IDENTIFIER "org.qsp.qspgui"
		MACOSX_BUNDLE_ICON_FILE "icon.icns"
		MACOSX_BUNDLE_INFO_STRING "QSP Classic"
		MACOSX_BUNDLE_LONG_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
		RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/misc/macos/icon.icns")
elseif (UNIX)
	add_executable(qspgui ${QSPGUI_SOURCES})
else()
	message(FATAL_ERROR "Platform is not supported")
endif()

target_compile_definitions(qspgui PRIVATE _UNICODE)
target_link_libraries(qspgui PRIVATE qsp wxbase wxcore wxadv wxaui wxnet wxhtml)
if (QSPGUI_USE_WEBVIEW)
	target_compile_definitions(qspgui PRIVATE QSPGUI_USE_WEBVIEW)
	if (NOT USE_INSTALLED_WX)
		target_link_libraries(qspgui PRIVATE wxwebview)
	endif()
	if (WIN32)
		# The Edge backend can serve the game folder over a virtual host, which
		# keeps relative media paths contained and gives <video> a real origin.
		# It needs the WebView2 SDK headers wxWidgets downloads for itself;
		# without them we fall back to a plain file:// base URL.
		# wxWidgets downloads the SDK itself and caches the location here, but
		# only adds the include path to its own target privately.
		find_path(QSPGUI_WEBVIEW2_INCLUDE_DIR
			NAMES WebView2.h
			HINTS "${WEBVIEW2_PACKAGE_DIR}/build/native/include"
			NO_DEFAULT_PATH)
		if (QSPGUI_WEBVIEW2_INCLUDE_DIR)
			message(STATUS "WebView2 SDK headers: ${QSPGUI_WEBVIEW2_INCLUDE_DIR}")
			target_include_directories(qspgui PRIVATE "${QSPGUI_WEBVIEW2_INCLUDE_DIR}")
			target_compile_definitions(qspgui PRIVATE QSPGUI_HAVE_WEBVIEW2_SDK)
		else()
			message(WARNING "WebView2 SDK headers not found; falling back to file:// base URLs")
		endif()
	endif()
endif()

# Translations
#
# The .mo catalogues are compiled from create_lang/*.po as part of the build, so
# a release cannot ship strings that are older than the ones in the tree - which
# is what happens when the generated catalogues are committed and someone edits
# a .po without remembering to re-run the converter by hand.
#
# gettext is not installed on a stock Windows box, so build_packages/msgfmt.py
# stands in for it. A real msgfmt is preferred when there is one, since it also
# validates the catalogue.

find_program(QSPGUI_MSGFMT NAMES msgfmt)
find_package(Python3 QUIET COMPONENTS Interpreter)

if (QSPGUI_MSGFMT)
	set(QSPGUI_MSGFMT_COMMAND "${QSPGUI_MSGFMT}")
	message(STATUS "Compiling translations with ${QSPGUI_MSGFMT}")
elseif (Python3_Interpreter_FOUND)
	set(QSPGUI_MSGFMT_COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_packages/msgfmt.py")
	message(STATUS "Compiling translations with ${Python3_EXECUTABLE} build_packages/msgfmt.py")
else()
	message(WARNING "Neither msgfmt nor Python found; shipping the prebuilt catalogues in misc/common/langs, which may be out of date")
endif()

# Where the catalogues live for both the build tree and the install tree.
set(QSPGUI_LANGS_DIR "${CMAKE_BINARY_DIR}/langs")

if (QSPGUI_MSGFMT_COMMAND)
	# CONFIGURE_DEPENDS so that adding a language does not silently need a manual
	# reconfigure before it appears in the build.
	file(GLOB QSPGUI_PO_FILES CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/create_lang/qspgui_*.po")
	set(QSPGUI_MO_FILES "")
	foreach (po IN LISTS QSPGUI_PO_FILES)
		# create_lang/qspgui_ru.po -> langs/ru/qspgui_ru.mo, the layout
		# QSPTranslationHelper looks for at runtime.
		get_filename_component(po_name "${po}" NAME_WE)
		string(REGEX REPLACE "^qspgui_" "" lang "${po_name}")
		set(mo "${QSPGUI_LANGS_DIR}/${lang}/${po_name}.mo")
		add_custom_command(OUTPUT "${mo}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${QSPGUI_LANGS_DIR}/${lang}"
			COMMAND ${QSPGUI_MSGFMT_COMMAND} "${po}" -o "${mo}"
			DEPENDS "${po}" "${CMAKE_CURRENT_SOURCE_DIR}/build_packages/msgfmt.py"
			COMMENT "Compiling translation ${lang}"
			VERBATIM)
		list(APPEND QSPGUI_MO_FILES "${mo}")
	endforeach()

	add_custom_target(qspgui_translations ALL DEPENDS ${QSPGUI_MO_FILES})
	add_dependencies(qspgui qspgui_translations)
else()
	# No converter available: fall back to whatever is committed.
	add_custom_target(qspgui_translations ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/misc/common/langs" "${QSPGUI_LANGS_DIR}"
		COMMENT "Copying prebuilt translations"
		VERBATIM)
	add_dependencies(qspgui qspgui_translations)
endif()

# A runnable build tree
#
# qsp.dll already lands next to the exe, but the data files did not, so running
# the freshly built player picked up neither translations nor the soundfont and
# every run needed a separate install step to be usable. Stage them beside the
# exe instead - this is the same content the install rules place in DATADIR.

if (APPLE)
	set(QSPGUI_RUNTIME_DATA_DIR "$<TARGET_BUNDLE_CONTENT_DIR:qspgui>/Resources")
else()
	set(QSPGUI_RUNTIME_DATA_DIR "$<TARGET_FILE_DIR:qspgui>")
endif()

add_custom_command(TARGET qspgui POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${QSPGUI_LANGS_DIR}" "${QSPGUI_RUNTIME_DATA_DIR}/langs"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/misc/common/sound" "${QSPGUI_RUNTIME_DATA_DIR}/sound"
	COMMENT "Staging data files next to the player"
	VERBATIM)

# Tests
#
# The pieces with no GUI in them: the QSP code generator that turns outside
# input into something the engine will run, the development API's hand-written
# JSON, the containment checks that keep a game in its folder, whole-file I/O
# and the save slots. Each is either a parser or a boundary check, which is
# exactly the code that should not be verified by running the player and
# looking at it.
#
# ctest --test-dir <build> --output-on-failure

option(QSPGUI_BUILD_TESTS "Build the unit tests" OFF)

if (QSPGUI_BUILD_TESTS)
	enable_testing()

	add_executable(qspgui_tests
		tests/testing.cpp
		tests/test_devjson.cpp
		tests/test_fileio.cpp
		tests/test_paths.cpp
		tests/test_qspcode.cpp
		tests/test_saveslots.cpp
		qspgui/comtools.cpp
		qspgui/devjson.cpp
		qspgui/saveslots.cpp
	)
	target_compile_definitions(qspgui_tests PRIVATE _UNICODE)
	if (WIN32)
		target_compile_definitions(qspgui_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
	# wxcore for wxColour, which comtools uses; qsp because the same file
	# serialises a session. No GUI is created - the runner is a console app
	# that brings wxWidgets up through wxInitializer.
	target_link_libraries(qspgui_tests PRIVATE qsp wxbase wxcore)

	add_test(NAME qspgui_tests COMMAND qspgui_tests)
endif()

# Installation

if (WIN32)
	set(BUNDLE_NAME qspgui.exe)
	set(BINDIR ${CMAKE_INSTALL_BINDIR})
	set(LIBDIR ${CMAKE_INSTALL_LIBDIR})
	set(RLIBDIR ${CMAKE_INSTALL_BINDIR})
	set(HEADERDIR ${CMAKE_INSTALL_INCLUDEDIR})
	set(DATADIR ${CMAKE_INSTALL_BINDIR})
	set(INSTALL_RPATH "")
elseif (APPLE)
	set(BUNDLE_NAME qspgui.app)
	set(BINDIR ${BUNDLE_NAME}/Contents/MacOS)
	set(LIBDIR ${BUNDLE_NAME}/Contents/Frameworks)
	set(RLIBDIR ${BUNDLE_NAME}/Contents/Frameworks)
	set(HEADERDIR ${BUNDLE_NAME}/Contents/Frameworks)
	set(DATADIR ${BUNDLE_NAME}/Contents/Resources)
	set(INSTALL_RPATH "@loader_path;@loader_path/../Frameworks")
elseif (UNIX)
	set(BUNDLE_NAME qspgui)
	set(BINDIR ${CMAKE_INSTALL_BINDIR})
	set(LIBDIR ${CMAKE_INSTALL_LIBDIR})
	set(RLIBDIR ${CMAKE_INSTALL_LIBDIR})
	set(HEADERDIR ${CMAKE_INSTALL_INCLUDEDIR})
	set(DATADIR ${CMAKE_INSTALL_DATADIR}/qspgui)
	set(INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
else()
	message(FATAL_ERROR "Platform is not supported")
endif()

set_target_properties(qspgui qsp PROPERTIES INSTALL_RPATH "${INSTALL_RPATH}")
install(TARGETS qspgui qsp
		COMPONENT Main
		RUNTIME DESTINATION ${BINDIR}
		LIBRARY DESTINATION ${RLIBDIR}
		ARCHIVE DESTINATION ${LIBDIR}
		PRIVATE_HEADER DESTINATION ${HEADERDIR}
		PUBLIC_HEADER DESTINATION ${HEADERDIR}
		BUNDLE DESTINATION .)

install(IMPORTED_RUNTIME_ARTIFACTS qspgui
		COMPONENT Main
		RUNTIME DESTINATION ${BINDIR}
		LIBRARY DESTINATION ${RLIBDIR}
		BUNDLE DESTINATION .)

if (NOT USE_INSTALLED_WX)
	set(WX_INSTALL_TARGETS wxbase wxcore wxadv wxaui wxhtml wxnet)
	if (QSPGUI_USE_WEBVIEW)
		list(APPEND WX_INSTALL_TARGETS wxwebview)
	endif()
	set_target_properties(${WX_INSTALL_TARGETS} PROPERTIES INSTALL_RPATH "${INSTALL_RPATH}")
	install(TARGETS ${WX_INSTALL_TARGETS}
			COMPONENT Libs
			RUNTIME DESTINATION ${BINDIR}
			LIBRARY DESTINATION ${RLIBDIR}
			ARCHIVE DESTINATION ${LIBDIR}
			PRIVATE_HEADER DESTINATION ${HEADERDIR}
			PUBLIC_HEADER DESTINATION ${HEADERDIR})
endif()

if (APPLE)
	set(DEP_DIRS "${CMAKE_INSTALL_PREFIX}/${RLIBDIR}")
	install(CODE
			"include(BundleUtilities)
			fixup_bundle(\"${CMAKE_INSTALL_PREFIX}/${BUNDLE_NAME}\" \"\" \"${DEP_DIRS}\")"
			COMPONENT Main)
elseif (UNIX)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/misc/linux/qspgui.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" COMPONENT Main)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/misc/common/icons/qsp.svg" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps" COMPONENT Main)
endif()

install(DIRECTORY "${QSPGUI_LANGS_DIR}" DESTINATION "${DATADIR}" COMPONENT Main)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/misc/common/sound" DESTINATION "${DATADIR}" COMPONENT Main)

# Packaging

if (WIN32)
	set(CPACK_PACKAGE_NAME "QSP Classic")
else()
	set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "QSP Classic player")
set(CPACK_PACKAGE_VENDOR "QSP Foundation")
set(CPACK_PACKAGE_CONTACT "byte@qsp.org")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
	OWNER_READ OWNER_WRITE OWNER_EXECUTE
	GROUP_READ GROUP_EXECUTE
	WORLD_READ WORLD_EXECUTE
)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_PACKAGE_EXECUTABLES qspgui "QSP Classic")
set(CPACK_CREATE_DESKTOP_LINKS qspgui)
set(CPACK_NSIS_INSTALLED_ICON_NAME "${BINDIR}\\qspgui.exe")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)

set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
     WriteRegStr HKCR '.qsp' '' 'QSPClassic'
     WriteRegStr HKCR '.gam' '' 'QSPClassic'
     WriteRegStr HKCR 'QSPClassic' '' 'QSP Game'
     WriteRegStr HKCR 'QSPClassic\\shell' '' 'open'
     WriteRegStr HKCR 'QSPClassic\\DefaultIcon' \\
                      '' '$INSTDIR\\${BINDIR}\\qspgui.exe,0'
     WriteRegStr HKCR 'QSPClassic\\shell\\open\\command' \\
                      '' '$INSTDIR\\${BINDIR}\\qspgui.exe \"%1\"'
     System::Call \\
       'Shell32::SHChangeNotify(i 0x8000000, i 0, i 0, i 0)'
     ")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
     DeleteRegKey HKCR '.qsp'
     DeleteRegKey HKCR '.gam'
     DeleteRegKey HKCR 'QSPClassic'
     ")

set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL Main)

include(CPack)

cpack_add_component(Main DISPLAY_NAME "QSP Player" REQUIRED)
