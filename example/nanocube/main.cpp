// QRhiWidget example drawing with nanovg_rhi: a shaded cube that spins continuously.
// Drag with the left mouse button to turn it; on release it keeps spinning.
// Double click resets to start spin.
// Faces are filled as nanovg paths, so all drawing goes through nanovg_rhi.
//
// nanocube [render backend]  opengl, vulkan, d3d11, d3d12, metal; default: Qt's choice

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QQuaternion>
#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "nanovg.h"
#define NANOVG_RHI_IMPLEMENTATION // the backend is compiled here
#include "nanovg_rhi.h"

class NanoCube : public QRhiWidget {
public:
	NanoCube()
	{
		setMinimumSize(320, 240);
		setWindowTitle("nanocube");
		m_clock.start();
	}

	~NanoCube() override { releaseResources(); }

protected:
	void initialize(QRhiCommandBuffer*) override
	{
		// called again when the render target or the QRhi changes; only a new QRhi needs a new context
		if(m_vg != nullptr && m_vgRhi != rhi()) releaseResources();
		if(m_vg != nullptr) return;

		m_vg = nvgRhiCreate(rhi(), 0);
		m_vgRhi = rhi();
		if(m_vg == nullptr) {
			qWarning("nanovg_rhi not available on %s", rhi()->backendName());
			return;
		}
		qInfo("render backend %s", rhi()->backendName());
		m_font = loadFont(":/fonts/Roboto-Regular.ttf");
	}

	void render(QRhiCommandBuffer* cb) override
	{
		if(m_vg == nullptr) return;
		const QSize px = renderTarget()->pixelSize();
		const float clear[4] = { .08f, .09f, .11f, 1.f };
		nvgRhiSetTarget(m_vg, cb, renderTarget(), clear);
		nvgBeginFrame(m_vg, px.width(), px.height(), 1.f);
		paintCube(px.width(), px.height());
		nvgEndFrame(m_vg); // records the render pass
		nvgRhiSetTarget(m_vg, nullptr, nullptr, clear);

		update(); // keep spinning: the next frame, paced by the display
	}

	void releaseResources() override
	{
		if(m_vg != nullptr) nvgRhiDelete(m_vg);
		m_vg = nullptr;
		m_vgRhi = nullptr;
		m_font = -1;
	}

	void mousePressEvent(QMouseEvent* e) override
	{
		if(e->button() != Qt::LeftButton) return;
		m_dragging = true;
		m_last = e->position();
		m_dragClock.start();
	}

	void mouseMoveEvent(QMouseEvent* e) override
	{
		if(!m_dragging) return;
		const QPointF d = e->position() - m_last;
		m_last = e->position();
		const float angle = float(std::hypot(d.x(), d.y())) * .5f; // degrees per pixel dragged
		if(angle <= 0.f) return;

		// about the screen axes: a horizontal drag turns about y, a vertical one about x
		const QVector3D axis = QVector3D(float(d.y()), float(d.x()), 0.f).normalized();
		m_orientation = QQuaternion::fromAxisAndAngle(axis, angle) * m_orientation;

		const float sec = float(std::max<qint64>(1, m_dragClock.restart())) / 1000.f;
		m_spinAxis = axis;
		m_spinSpeed = std::min(angle / sec, 720.f); // kept on release
	}

	void mouseReleaseEvent(QMouseEvent* e) override
	{
		if(e->button() != Qt::LeftButton || !m_dragging) return;
		m_dragging = false;
		if(m_dragClock.elapsed() > 100) m_spinSpeed = 0.f; // held still before release: stop
		m_clock.restart(); // no jump by the time the cube was held
	}

	void mouseDoubleClickEvent(QMouseEvent* e) override
	{
		if(e->button() != Qt::LeftButton) return;
		m_orientation = QQuaternion();
		m_spinAxis = QVector3D(.3f, 1.f, .2f).normalized();
		m_spinSpeed = 45.f;
	}

private:
	static constexpr float EYE = 6.f; // distance of the cube center from the eye

	NVGcontext* m_vg = nullptr;
	QRhi* m_vgRhi = nullptr; ///< the QRhi m_vg was created with
	int m_font = -1;

	QQuaternion m_orientation;
	QVector3D m_spinAxis = QVector3D(.3f, 1.f, .2f).normalized();
	float m_spinSpeed = 45.f; // degrees per second
	QElapsedTimer m_clock; ///< since the last frame
	QElapsedTimer m_dragClock; ///< since the last mouse move of a drag
	QPointF m_last;
	bool m_dragging = false;

	/// a TrueType font from a Qt resource; nanovg frees the copy with free()
	int loadFont(const QString& name)
	{
		QFile f(name);
		if(!f.open(QFile::ReadOnly)) return -1;
		const QByteArray font = f.readAll();
		unsigned char* mem = static_cast<unsigned char*>(std::malloc(size_t(font.size())));
		if(mem == nullptr) return -1;
		std::memcpy(mem, font.constData(), size_t(font.size()));
		return nvgCreateFontMem(m_vg, "sans", mem, int(font.size()), 1);
	}

	void paintCube(float w, float h) // device pixels
	{
		const float dpr = float(devicePixelRatio());

		// spin by the time since the last frame, not while the mouse holds the cube
		const float dt = float(m_clock.restart()) / 1000.f;
		if(!m_dragging && m_spinSpeed > 0.f)
			m_orientation = QQuaternion::fromAxisAndAngle(m_spinAxis, m_spinSpeed * dt) * m_orientation;

		QMatrix4x4 mvp;
		mvp.perspective(35.f, w / h, .1f, 100.f);
		mvp.translate(0.f, 0.f, -EYE);
		mvp.rotate(m_orientation);

		static const QVector3D corners[8] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 },
			{ 1, 1, 1 }, { -1, 1, 1 } };
		QPointF p[8];
		for(int i = 0; i < 8; ++i) {
			const QVector3D ndc = (mvp * QVector4D(corners[i], 1.f)).toVector3DAffine();
			p[i] = QPointF((ndc.x() + 1.f) * .5f * w, (1.f - ndc.y()) * .5f * h);
		}

		struct Face {
			int c[4];
			QVector3D normal;
			NVGcolor color;
		};
		static const Face faces[6] = {
			{ { 4, 5, 6, 7 }, { 0, 0, 1 }, nvgRGBf(.90f, .30f, .25f) }, // front
			{ { 1, 0, 3, 2 }, { 0, 0, -1 }, nvgRGBf(.25f, .65f, .90f) }, // back
			{ { 5, 1, 2, 6 }, { 1, 0, 0 }, nvgRGBf(.30f, .80f, .40f) }, // right
			{ { 0, 4, 7, 3 }, { -1, 0, 0 }, nvgRGBf(.95f, .75f, .20f) }, // left
			{ { 7, 6, 2, 3 }, { 0, 1, 0 }, nvgRGBf(.70f, .40f, .90f) }, // top
			{ { 0, 1, 5, 4 }, { 0, -1, 0 }, nvgRGBf(.90f, .55f, .20f) }, // bottom
		};
		const QVector3D light = QVector3D(-.4f, .6f, 1.f).normalized(); // view space, from upper left

		// a cube is convex: the faces turned towards the eye never overlap, no sorting needed
		for(const Face& f : faces) {
			const QVector3D n = m_orientation.rotatedVector(f.normal);
			const QVector3D center = n + QVector3D(0.f, 0.f, -EYE); // face center in view space
			if(QVector3D::dotProduct(n, -center) <= 0.f) continue; // turned away

			const float k = .35f + .65f * std::max(0.f, QVector3D::dotProduct(n, light));
			nvgBeginPath(m_vg);
			nvgMoveTo(m_vg, p[f.c[0]].x(), p[f.c[0]].y());
			for(int i = 1; i < 4; ++i)
				nvgLineTo(m_vg, p[f.c[i]].x(), p[f.c[i]].y());
			nvgClosePath(m_vg);
			// nanovgXC colors are bytes (0..255), not floats as in nanovg
			nvgFillColor(m_vg, nvgRGBA((unsigned char)(f.color.r * k), (unsigned char)(f.color.g * k), (unsigned char)(f.color.b * k), 255));
			nvgFill(m_vg);
			nvgStrokeColor(m_vg, nvgRGBA(255, 255, 255, 90));
			nvgStrokeWidth(m_vg, 1.5f * dpr);
			nvgStroke(m_vg);
		}

		if(m_font >= 0) {
			nvgFontFaceId(m_vg, m_font);
			nvgFontSize(m_vg, 14.f * dpr);
			nvgFillColor(m_vg, nvgRGBA(160, 160, 160, 255));
			nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgText(m_vg, 10.f * dpr, 10.f * dpr, "drag to turn, double click to reset", nullptr);

			const QByteArray backend = QByteArray("render backend ") + rhi()->backendName();
			nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
			nvgText(m_vg, 10.f * dpr, h - 10.f * dpr, backend.constData(), nullptr);
		}
	}
};

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	NanoCube cube;
	if(argc > 1) {
		const QByteArray api = QByteArray(argv[1]).toLower();
		if(api == "opengl") cube.setApi(QRhiWidget::Api::OpenGL);
		else if(api == "vulkan") cube.setApi(QRhiWidget::Api::Vulkan);
		else if(api == "d3d11") cube.setApi(QRhiWidget::Api::Direct3D11);
		else if(api == "d3d12") cube.setApi(QRhiWidget::Api::Direct3D12);
		else if(api == "metal") cube.setApi(QRhiWidget::Api::Metal);
		else {
			fprintf(stderr, "unknown render backend '%s', expected one of: opengl, vulkan, d3d11, d3d12, metal\n", argv[1]);
			return EXIT_FAILURE;
		}
	}
	cube.resize(640, 480);
	cube.show();
	return app.exec();
}
