#pragma once
#include <G3D/G3D.h>
#include "IrradianceField.h"

class CGIRenderer :public DefaultRenderer
{
	shared_ptr<IrradianceField> m_pIrradianceField;

	shared_ptr<Framebuffer>     m_pGIFramebuffer;
public:
	static shared_ptr<CGIRenderer> create()
	{
		return createShared<CGIRenderer>();
	}

	void setIrradianceField(shared_ptr<IrradianceField> vIrradianceField) { m_pIrradianceField = vIrradianceField; }

protected:
	CGIRenderer() {}

	virtual void renderDeferredShading
	(RenderDevice*                      rd,
		const Array<shared_ptr<Surface>>&   sortedVisibleSurfaceArray,
		const shared_ptr<GBuffer>&          gbuffer,
		const LightingEnvironment&          environment) override;
};