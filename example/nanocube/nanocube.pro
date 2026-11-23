# Minimal QRhiWidget example drawing with nanovg_rhi, a spinning cube, see main.cpp.
# Uses the files of this repository only: src/ (nanovg, nanovg_rhi and its shaders),
# example/stb and example/fonts. Needs Qt 6.7+ (QRhiWidget) with Qt Shader Tools.

TARGET = nanocube
QT += core gui widgets
QT += gui-private # rhi/qrhi.h
CONFIG += c++17

# QRhi is semi-public since Qt 6.6 and still comes with gui-private: no "using private
# headers" messages; the build stays tied to the Qt version it was built against
CONFIG += no_private_qt_headers_warning

NVG_SRC = $$PWD/../../src

INCLUDEPATH += $$NVG_SRC $$PWD/../stb

HEADERS += \
	$$NVG_SRC/nanovg.h \
	$$NVG_SRC/nanovg_rhi.h

SOURCES += \
	main.cpp \
	nanovg_impl.c \
	$$NVG_SRC/nanovg.c

RESOURCES += nanocube.qrc # font of the hint text

# nanovg_rhi shaders, built by qsb (Qt Shader Tools); nanovg_rhi loads them from :/nanoplot/shaders
NVG_QSB = $$[QT_HOST_BINS]/qsb
exists($${NVG_QSB}.exe): NVG_QSB = $${NVG_QSB}.exe
!exists($$NVG_QSB): error("qsb not found in $$[QT_HOST_BINS], install Qt Shader Tools")

NVG_SHADERS = $$NVG_SRC/shaders/nvg.vert $$NVG_SRC/shaders/nvg.frag
nvgqsb.input = NVG_SHADERS
nvgqsb.output = ${QMAKE_FILE_BASE}${QMAKE_FILE_EXT}.qsb
nvgqsb.commands = $$shell_path($$NVG_QSB) --glsl 300es,330 --hlsl 50 --msl 12 -o ${QMAKE_FILE_OUT} ${QMAKE_FILE_NAME}
nvgqsb.name = QSB ${QMAKE_FILE_IN}
nvgqsb.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += nvgqsb

# the resource file over the generated shaders; rcc drops dependencies
# on files that do not exist yet at qmake time, so name them here
NVG_QRC = \
	"<RCC>" \
	"    <qresource prefix=\"/nanoplot/shaders\">" \
	"        <file>nvg.vert.qsb</file>" \
	"        <file>nvg.frag.qsb</file>" \
	"    </qresource>" \
	"</RCC>"
write_file($$OUT_PWD/nvgshaders.qrc, NVG_QRC)|error("cannot write $$OUT_PWD/nvgshaders.qrc")
RESOURCES += $$OUT_PWD/nvgshaders.qrc
rcc.depends += nvg.vert.qsb nvg.frag.qsb
