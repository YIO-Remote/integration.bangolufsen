TEMPLATE  = lib
CONFIG   += c++14 plugin
QT       += websockets core quick

# Plugin VERSION
GIT_HASH = "$$system(git log -1 --format="%H")"
GIT_BRANCH = "$$system(git rev-parse --abbrev-ref HEAD)"
GIT_VERSION = "$$system(git describe --match "v[0-9]*" --tags HEAD --always)"
BO_VERSION = $$replace(GIT_VERSION, v, "")
DEFINES += PLUGIN_VERSION=\\\"$$BO_VERSION\\\"

# build timestamp
win32 {
    # not the same format as on Unix systems, but good enough...
    BUILDDATE=$$system(date /t)
} else {
    BUILDDATE=$$system(date +"%Y-%m-%dT%H:%M:%S")
}
CONFIG(debug, debug|release) {
    DEBUG_BUILD = true
} else {
    DEBUG_BUILD = false
}

INTG_LIB_PATH = $$(YIO_SRC)
isEmpty(INTG_LIB_PATH) {
    INTG_LIB_PATH = $$clean_path($$PWD/../integrations.library)
    message("Environment variables YIO_SRC not defined! Using '$$INTG_LIB_PATH' for integrations.library project.")
} else {
    INTG_LIB_PATH = $$(YIO_SRC)/integrations.library
    message("YIO_SRC is set: using '$$INTG_LIB_PATH' for integrations.library project.")
}

! include($$INTG_LIB_PATH/qmake-destination-path.pri) {
    error( "Couldn't find the qmake-destination-path.pri file!" )
}

! include($$INTG_LIB_PATH/yio-plugin-lib.pri) {
    error( "Cannot find the yio-plugin-lib.pri file!" )
}

# verify integrations.library version
unix {
    INTG_LIB_VERSION = $$system(cat $$PWD/dependencies.cfg | awk '/^integrations.library:/$$system_quote("{print $2}")')
    INTG_GIT_VERSION = "$$system(cd $$INTG_LIB_PATH && git describe --match "v[0-9]*" --tags HEAD --always)"
    INTG_GIT_BRANCH  = "$$system(cd $$INTG_LIB_PATH && git rev-parse --abbrev-ref HEAD)"
    message("Required integrations.library version: $$INTG_LIB_VERSION Local version: $$INTG_GIT_VERSION ($$INTG_GIT_BRANCH)")
    # this is a simple check but qmake only provides limited tests and 'versionAtLeast' doesn't work with 'v' prefix.
    !contains(INTG_GIT_VERSION, $$re_escape($${INTG_LIB_VERSION}).*)) {
        !equals(INTG_GIT_BRANCH, $$INTG_LIB_VERSION) {
            error("Invalid integrations.library version: \"$$INTG_GIT_VERSION\". Please check out required version \"$$INTG_LIB_VERSION\"")
        }
    }
}

QMAKE_SUBSTITUTES += bangolufsen.json.in version.txt.in
# output path must be included for the output file from QMAKE_SUBSTITUTES
INCLUDEPATH += $$OUT_PWD
HEADERS  += src/bangolufsen.h
SOURCES  += src/bangolufsen.cpp
TARGET    = bangolufsen

# Configure destination path. DESTDIR is set in qmake-destination-path.pri
DESTDIR = $$DESTDIR/plugins
OBJECTS_DIR = $$PWD/build/$$DESTINATION_PATH/obj
MOC_DIR = $$PWD/build/$$DESTINATION_PATH/moc
RCC_DIR = $$PWD/build/$$DESTINATION_PATH/qrc
UI_DIR = $$PWD/build/$$DESTINATION_PATH/ui

# install
unix {
    target.path = /usr/lib
    INSTALLS += target
}

DISTFILES += \
    dependencies.cfg \
    bangolufsen.json.in \
    version.txt.in \
    README.md

# === start TRANSLATION section =======================================
lupdate_only{
SOURCES = src/bangolufsen.cpp
}

TRANSLATIONS = translations/bg_BG.ts \
               translations/ca_ES.ts \
               translations/cs_CZ.ts \
               translations/da_DK.ts \
               translations/de_DE.ts \
               translations/el_GR.ts \
               translations/en_US.ts \
               translations/es_ES.ts \
               translations/et_EE.ts \
               translations/fi_FI.ts \
               translations/fr_CA.ts \
               translations/fr_FR.ts \
               translations/ga_IE.ts \
               translations/hr_HR.ts \
               translations/hu_HU.ts \
               translations/is_IS.ts \
               translations/it_IT.ts \
               translations/lt_LT.ts \
               translations/lv_LV.ts \
               translations/mt_MT.ts \
               translations/nl_NL.ts \
               translations/no_NO.ts \
               translations/pl_PL.ts \
               translations/pt_BR.ts \
               translations/pt_PT.ts \
               translations/ro_RO.ts \
               translations/ru_BY.ts \
               translations/ru_MD.ts \
               translations/ru_RU.ts \
               translations/ru_UA.ts \
               translations/sk_SK.ts \
               translations/sl_SI.ts \
               translations/sv_SE.ts \
               translations/zh_CN.ts \
               translations/zh_TW.ts

#QMAKE_LUPDATE & _LRELEASE vars are set in qmake-destiation-path.pri
!isEmpty(QMAKE_LUPDATE):exists("$$QMAKE_LUPDATE") {
    message("Using Qt linguist tools: '$$QMAKE_LUPDATE', '$$QMAKE_LRELEASE'")
    command = $$QMAKE_LUPDATE bangolufsen.pro
    system($$command) | error("Failed to run: $$command")
    command = $$QMAKE_LRELEASE bangolufsen.pro
    system($$command) | error("Failed to run: $$command")
} else {
    warning("Qt linguist cmd line tools lupdate / lrelease not found: translations will NOT be compiled and build will most likely fail due to missing .qm files!")
}

RESOURCES += \
    translations.qrc

# Add setup schema to metadata
CFG_SCHEMA = "$$cat($$PWD/setup-schema.json)"
