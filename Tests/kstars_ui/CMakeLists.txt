
SET(KSTARS_UI_TESTS_SRC kstars_ui_tests.cpp)

ECM_ADD_APP_ICON(KSTARS_UI_TESTS_SRC ICONS
        ../../kstars/icons/16-apps-kstars.png
        ../../kstars/icons/32-apps-kstars.png
        ../../kstars/icons/48-apps-kstars.png
        ../../kstars/icons/64-apps-kstars.png
        ../../kstars/icons/128-apps-kstars.png
        )

QT5_ADD_RESOURCES(KSTARS_UI_TESTS_SRC ../../kstars/data/kstars.qrc)

ADD_EXECUTABLE(kstars_ui_tests ${KSTARS_UI_TESTS_SRC})
TARGET_LINK_LIBRARIES(kstars_ui_tests ${TEST_LIBRARIES})

IF (INDI_FOUND)
    INCLUDE_DIRECTORIES(${INDI_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(kstars_ui_tests ${INDI_CLIENT_LIBRARIES} ${NOVA_LIBRARIES} z)
ENDIF ()

ADD_TEST(NAME KStarsUiTests COMMAND ${CMAKE_CURRENT_BINARY_DIR}/kstars)