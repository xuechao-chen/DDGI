#include "App.h"

G3D_START_AT_MAIN();

int main(int argc, const char* argv[])
{
	initGLG3D(G3DSpecification());

	GApp::Settings settings(argc, argv);

	settings.window.caption = argv[0];

	settings.window.fullScreen = false;
	settings.window.width = 1400;
	settings.window.height = 1000;
	settings.window.resizable = !settings.window.fullScreen;
	settings.window.framed = !settings.window.fullScreen;
	settings.window.defaultIconFilename = "icon.png";

	settings.window.asynchronous = true;

	settings.hdrFramebuffer.colorGuardBandThickness = Vector2int16(0, 0);
	settings.hdrFramebuffer.depthGuardBandThickness = Vector2int16(0, 0);

	settings.renderer.deferredShading = true;
	settings.renderer.orderIndependentTransparency = true;

	settings.dataDir = FileSystem::currentDirectory();

	settings.screenCapture.includeAppRevision = false;
	settings.screenCapture.includeG3DRevision = false;
	settings.screenCapture.filenamePrefix = "_";

	return App(settings).run();
}


App::App(const GApp::Settings& settings) : GApp(settings)
{
}

void App::onInit()
{
	GApp::onInit();

	setFrameDuration(1.0f / 240.0f);

	String SceneName = "G3D Simple Cornell Box";
	loadScene(SceneName);
	
	makeGUI();
}

void App::onGraphics3D(RenderDevice * rd, Array<shared_ptr<Surface>>& surface3D)
{
	if (m_pIrradianceField) m_pIrradianceField->onGraphics3D(rd, surface3D);

	GApp::onGraphics3D(rd, surface3D);
}

void App::onAfterLoadScene(const Any & any, const String & sceneName)
{
	m_pIrradianceField = IrradianceField::create(sceneName, scene());
	m_pIrradianceField->onSceneChanged(scene());
}

void App::makeGUI()
{
	debugWindow->setVisible(true);
	developerWindow->videoRecordDialog->setEnabled(true);

	debugWindow->pack();
	debugWindow->setRect(Rect2D::xywh(0, 0, (float)window()->width(), debugWindow->rect().height()));
}
