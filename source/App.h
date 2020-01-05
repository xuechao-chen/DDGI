#pragma once
#include <G3D/G3D.h>

class App : public GApp
{
protected:
	void makeGUI();

public:
	App(const GApp::Settings& settings = GApp::Settings());

	virtual void onInit() override;
};
