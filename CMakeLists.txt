cmake_minimum_required (VERSION 2.6 FATAL_ERROR)

set_property(GLOBAL PROPERTY USE_FOLDERS On)
project      (test_spreadsheet)
find_package (Qt5Widgets)

set(CMAKE_AUTORCC ON)

set  (project_SOURCES
    main.cpp
    spread_sheet.cpp
)

set  (INCLUDE_FILE
    spread_sheet.h
)

set  (QT_UI_HEADERS
      spread_sheet.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
set  (project_HEADERS ${INCLUDE_FILE} ${QT_UI_HEADERS})
set  (project_FORMS
      spread_sheet.ui
)

QT5_WRAP_CPP (project_HEADERS_MOC  ${QT_UI_HEADERS})
QT5_WRAP_UI  (project_FORMS_HEADERS ${project_FORMS})

ADD_DEFINITIONS (${QT_DEFINITIONS})
ADD_DEFINITIONS(-D _XKEYCHECK_H)

ADD_EXECUTABLE  (spread_sheet
                                ${project_SOURCES}
                                ${project_FORMS_HEADERS}
                                ${project_HEADERS_MOC}
                                )

#/SUBSYSTEM:WINDOWS and /ENTRY:mainCRTStartup need to config same time
Set_Target_Properties(spread_sheet PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

#set vs startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT spread_sheet)


IF (WIN32)
    TARGET_LINK_LIBRARIES (spread_sheet  ${QT_LIBRARIES} Qt5::Widgets)
ELSE (WIN32)
    TARGET_LINK_LIBRARIES (spread_sheet ${QT_LIBRARIES})
ENDIF (WIN32)
