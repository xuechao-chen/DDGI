#pragma once
#include <G3D/G3D.h>
#include "IrradianceField.h"

class App : public GApp
{
	shared_ptr<IrradianceField> m_pIrradianceField;
protected:
	void makeGUI();

public:
	App(const GApp::Settings& settings = GApp::Settings());

	virtual void onInit() override;
	virtual void onGraphics3D(RenderDevice* rd, Array<shared_ptr<Surface>>& surface3D) override;
	virtual void onAfterLoadScene(const Any& any, const String& sceneName) override;
};
