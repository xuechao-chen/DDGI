#pragma once
#include <G3D/G3D.h>

class CGIRenderer :public DefaultRenderer
{
public:
	static shared_ptr<CGIRenderer> create()
	{
		return createShared<CGIRenderer>();
	}

protected:
	CGIRenderer() {}

	virtual void renderDeferredShading
	(RenderDevice*                      rd,
		const Array<shared_ptr<Surface>>&   sortedVisibleSurfaceArray,
		const shared_ptr<GBuffer>&          gbuffer,
		const LightingEnvironment&          environment) override;

	virtual void renderIndirectIllumination
	(RenderDevice*                       rd,
		const Array<shared_ptr<Surface> >&  sortedVisibleSurfaceArray,
		const shared_ptr<GBuffer>&          gbuffer,
		const LightingEnvironment&          environment) override;
};