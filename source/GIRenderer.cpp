#include "GIRenderer.h"

void CGIRenderer::renderDeferredShading(RenderDevice * rd, const Array<shared_ptr<Surface>>& sortedVisibleSurfaceArray, const shared_ptr<GBuffer>& gbuffer, const LightingEnvironment & environment)
{
	if (m_pIrradianceField)
	{
		if (isNull(m_pGIFramebuffer))
		{
			m_pGIFramebuffer = Framebuffer::create("CGIRenderer::m_pGIFramebuffer");
			m_pGIFramebuffer->set(Framebuffer::COLOR0, Texture::createEmpty("CGIRenderer::Indirect", gbuffer->width(), gbuffer->height(), ImageFormat::RGBA32F()));
		}
		m_pGIFramebuffer->resize(gbuffer->width(), gbuffer->height());

		// Compute GI
		rd->push2D(m_pGIFramebuffer); {
			Args args;
			gbuffer->setShaderArgsRead(args, "gbuffer_");
			args.setRect(rd->viewport());
			m_pIrradianceField->setShaderArgs(args, "irradianceFieldSurface.");
			args.setUniform("energyPreservation", 1.0f);

			LAUNCH_SHADER("shaders/GIRenderer_ComputeIndirect.pix", args);
		} rd->pop2D();
	}

	// Find the skybox
	shared_ptr<SkyboxSurface> skyboxSurface;
	for (const shared_ptr<Surface>& surface : sortedVisibleSurfaceArray)
	{
		skyboxSurface = dynamic_pointer_cast<SkyboxSurface>(surface);
		if (skyboxSurface) { break; }
	}

	rd->push2D(); {
		Args args;
		environment.setShaderArgs(args);
		gbuffer->setShaderArgsRead(args, "gbuffer_");
		args.setRect(rd->viewport());

		args.setUniform("matteIndirectBuffer", notNull(m_pGIFramebuffer) ? m_pGIFramebuffer->texture(0) : Texture::opaqueBlack(), Sampler::buffer());

		args.setMacro("OVERRIDE_SKYBOX", true);
		if (skyboxSurface) skyboxSurface->setShaderArgs(args, "skybox_");

		LAUNCH_SHADER("shaders/GIRenderer_DeferredShade.pix", args);
	} rd->pop2D();
}