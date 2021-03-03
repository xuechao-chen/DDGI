#include "IrradianceField.h"

/** How much should the probes count when shading *themselves*? 1.0 preserves
	energy perfectly. Lower numbers compensate for small leaks/precision by avoiding
	recursive energy explosion. */
static const float recursiveEnergyPreservation = 0.85f;

const Array<const ImageFormat*> IrradianceField::s_irradianceFormats = {
	ImageFormat::RGB5A1(),
	ImageFormat::RGB8(),
	ImageFormat::RGB10A2(),
	ImageFormat::R11G11B10F(),
	ImageFormat::RGB16F(),
	ImageFormat::RGB32F() };

const Array<const ImageFormat*> IrradianceField::s_depthFormats = {
	ImageFormat::RGB8(),
	ImageFormat::RG16F(),
	ImageFormat::RG32F() };

IrradianceField::Specification::Specification() {}

Any IrradianceField::Specification::toAny() const
{
	Any a(Any::TABLE, "IrradianceField::Specification");
	a["probeDimensions"] = probeDimensions;
	a["probeCounts"] = probeCounts;
	a["irradianceOctResolution"] = irradianceOctResolution;
	a["depthOctResolution"] = depthOctResolution;
	a["irradianceDistanceBias"] = irradianceDistanceBias;
	a["irradianceVarianceBias"] = irradianceVarianceBias;
	a["irradianceChebyshevBias"] = irradianceChebyshevBias;
	a["normalBias"] = normalBias;
	a["hysteresis"] = hysteresis;
	a["depthSharpness"] = depthSharpness;
	a["irradianceRaysPerProbe"] = irradianceRaysPerProbe;
	a["glossyToMatte"] = glossyToMatte;
	a["singleBounce"] = singleBounce;
	a["irradianceFormatIndex"] = irradianceFormatIndex;
	a["depthFormatIndex"] = depthFormatIndex;
	a["showLights"] = singleBounce;
	a["encloseBounds"] = encloseBounds;
	return a;
}

IrradianceField::Specification::Specification(const Any& any)
{
	*this = IrradianceField::Specification();
	AnyTableReader reader("IrradianceField::Specification", any);
	reader.getIfPresent("probeDimensions", probeDimensions);
	reader.getIfPresent("probeCounts", probeCounts);
	reader.getIfPresent("irradianceOctResolution", irradianceOctResolution);
	reader.getIfPresent("depthOctResolution", depthOctResolution);
	reader.getIfPresent("irradianceDistanceBias", irradianceDistanceBias);
	reader.getIfPresent("irradianceVarianceBias", irradianceVarianceBias);
	reader.getIfPresent("irradianceChebyshevBias", irradianceChebyshevBias);
	reader.getIfPresent("normalBias", normalBias);
	reader.getIfPresent("hysteresis", hysteresis);
	reader.getIfPresent("depthSharpness", depthSharpness);
	reader.getIfPresent("irradianceRaysPerProbe", irradianceRaysPerProbe);
	reader.getIfPresent("glossyToMatte", glossyToMatte);
	reader.getIfPresent("singleBounce", singleBounce);
	reader.getIfPresent("irradianceFormatIndex", irradianceFormatIndex);
	reader.getIfPresent("depthFormatIndex", depthFormatIndex);
	reader.getIfPresent("showLights", showLights);
	reader.getIfPresent("encloseBounds", encloseBounds);
	reader.verifyDone();
}

void IrradianceField::loadNewScene
   (const String& sceneName,
	const shared_ptr<Scene>& scene,
	Vector3int32 probeCountsOverride,
	float maxProbeDistance,
	int irradianceCubeResolutionOverride,
	int depthCubeResolutionOverride)
{
	const String& sceneFilename = Scene::sceneNameToFilename(sceneName);

	// Check if there is an options file for this scene
	const String& specName = FilePath::mangle(sceneName) + ".LightFieldModelSpecification.Any";
	const String& irradianceFieldSpecificationFilename = System::findDataFile(specName, false);
	debugPrintf("%s\n", specName.c_str());

	IrradianceField::Specification spec;
	bool specExists = FileSystem::exists(irradianceFieldSpecificationFilename);
	if (specExists) {
		spec = IrradianceField::Specification(Any::fromFile(irradianceFieldSpecificationFilename));
	}

	// Spec file didn't set probe dimensions, so compute them here.
	if (!specExists || spec.probeDimensions == AABox(Point3(0.0f, 0.0f, 0.0f), Point3(1.0f, 1.0f, 1.0f))) {
		// If a specification file does *not* exist, automatically generate a probe grid from the scene's total bounding box
		bool boxSet = false;
		AABox fullBox;

		// Iterate over all visible models in the scene to generate the final bounding box
		Array<shared_ptr<VisibleEntity>> entities;
		scene->getTypedEntityArray(entities);
		for (const shared_ptr<VisibleEntity>& entity : entities) {
			if (!entity->visible() || isNull(entity->model())) {
				continue;
			}

			AABox eBox;
			entity->getLastBounds(eBox);

			if (boxSet) {
				fullBox.merge(eBox);
			}
			else {
				boxSet = true;
				fullBox = eBox;
			}
		}

		Vector3 boxDims = fullBox.high() - fullBox.low();

		if (specExists) {
			m_encloseScene = m_encloseScene || spec.encloseBounds;
		}

		// In order to minimize the likelihood of probes being stuck in walls, reduce the dimensions somewhat
		// to be enclosed in the scene bounding box, or increase them to enclose it.
		boxDims.x *= m_encloseScene ? 1.1f : 0.9f;
		boxDims.y *= m_encloseScene ? 1.1f : 0.7f; // Reduce y more since we only have 2 probes in that direction
		boxDims.z *= m_encloseScene ? 1.1f : 0.9f;

		spec.probeDimensions = AABox(fullBox.center() - boxDims * 0.5f, fullBox.center() + boxDims * 0.5f);
	}

	if ((probeCountsOverride.x > 0) && (probeCountsOverride.y > 0) && (probeCountsOverride.z > 0)) {
		spec.probeCounts = probeCountsOverride;
	}
	else if (maxProbeDistance > 0.0f) {
		spec.probeCounts = Vector3int32(Vector3(spec.probeDimensions.high() - spec.probeDimensions.low()) / Vector3(maxProbeDistance, maxProbeDistance, maxProbeDistance));
		debugPrintf("Debug probe counts: %d, %d, %d\n", spec.probeCounts.x, spec.probeCounts.y, spec.probeCounts.z);
		for (int i = 0; i < 3; ++i) {
			spec.probeCounts[i] = ceilPow2(spec.probeCounts[i]);
		}
	}

	if (irradianceCubeResolutionOverride > 0) {
		spec.irradianceOctResolution = irradianceCubeResolutionOverride;
	}
	if (depthCubeResolutionOverride > 0) {
		spec.depthOctResolution = depthCubeResolutionOverride;
	}

	// Assume the probe counts are powers of two.
	int totalProbes = spec.probeCounts.x + spec.probeCounts.y + spec.probeCounts.z;
	// Do not go larger than 8k texture
	static const int MAX_TEXTURE_SIZE = 4096 * 4096;
	while ((totalProbes * spec.irradianceOctResolution * spec.irradianceOctResolution) > MAX_TEXTURE_SIZE
		|| (totalProbes * spec.depthOctResolution * spec.depthOctResolution) > MAX_TEXTURE_SIZE) {
		debugPrintf("Requested probe count is larger than max texture size of %d\n", MAX_TEXTURE_SIZE);
		// Heuristics. XZ resolution is probably more important than Y resolution,
		// unless Y resolution is relatively low...
		if (spec.probeCounts.y > 8) {
			spec.probeCounts.y /= 2;
		}
		else {
			spec.probeCounts.x /= 2; spec.probeCounts.z /= 2;
		}
		totalProbes = spec.probeCounts.x + spec.probeCounts.y + spec.probeCounts.z;
	}

	const Vector3 boundingBoxLengths(spec.probeDimensions.high() - spec.probeDimensions.low());
	// Slightly larger than the diagonal across the grid cell
	m_maxDistance = (boundingBoxLengths / spec.probeCounts).length() * 1.5f;

	init(spec);
	allocateIntermediateBuffers();
	m_probeFormatChanged = true;
	generateIrradianceProbes(RenderDevice::current);

	debugPrintf("Load complete.\n");
}

shared_ptr<IrradianceField> IrradianceField::create
(const String& sceneName,
	const shared_ptr<Scene>& scene,
	Vector3int32 probeCountsOverride,
	float maxProbeDistance,
	int irradianceCubeResolutionOverride)
{
	const shared_ptr<IrradianceField>& irradianceField = createShared<IrradianceField>();
	irradianceField->loadNewScene(sceneName, scene, probeCountsOverride, maxProbeDistance, irradianceCubeResolutionOverride);
	return irradianceField;
}

IrradianceField::IrradianceField()
{
	m_sceneTriTree = TriTree::create(true);
}

void IrradianceField::setShaderArgs(UniformTable& args, const String& prefix) {
	alwaysAssertM(endsWith(prefix, "."), "Requires a struct prefix");

	Sampler bilinear = Sampler::video();
	m_irradianceProbes->setShaderArgs(args, prefix + "irradianceProbeGrid", bilinear);
	m_meanDistProbes->setShaderArgs(args, prefix + "meanMeanSquaredProbeGrid", bilinear);

	// Uniforms to convert oct to texel and back
	args.setUniform(prefix + "irradianceTextureWidth", m_irradianceProbes->width());
	args.setUniform(prefix + "irradianceTextureHeight", m_irradianceProbes->height());
	args.setUniform(prefix + "depthTextureWidth", m_meanDistProbes->width());
	args.setUniform(prefix + "depthTextureHeight", m_meanDistProbes->height());
	args.setUniform(prefix + "irradianceProbeSideLength", irradianceOctSideLength());
	args.setUniform(prefix + "depthProbeSideLength", depthOctSideLength());

	args.setUniform(prefix + "probeCounts", m_specification.probeCounts);
	args.setUniform(prefix + "probeStartPosition", m_probeStartPosition);
	args.setUniform(prefix + "probeStep", m_probeStep);

	args.setUniform(prefix + "irradianceDistanceBias", m_specification.irradianceDistanceBias);
	args.setUniform(prefix + "irradianceVarianceBias", m_specification.irradianceVarianceBias);
	args.setUniform(prefix + "irradianceChebyshevBias", m_specification.irradianceChebyshevBias);
	args.setUniform(prefix + "normalBias", m_specification.normalBias);

	args.setMacro("TRACE_MODE", "WORLD_SPACE_MARCH");
	args.setMacro("FILL_HOLES", "true");
	args.setMacro("LIGHTING_MODE", m_lightingMode);
}

void IrradianceField::init(const Specification& spec)
{
	m_name = "Irradiance Field";

	m_specification = spec;
	alwaysAssertM(G3D::isPow2(m_specification.probeCounts.x * m_specification.probeCounts.y * m_specification.probeCounts.z),
		"Probe count must be power of two");

	const Point3& lo = spec.probeDimensions.low();
	const Point3& hi = spec.probeDimensions.high();
	m_probeStep = (hi - lo) / (Vector3(m_specification.probeCounts) - Vector3(1, 1, 1)).max(Vector3(1, 1, 1));
	m_probeStartPosition = lo;
	m_oneBounce = spec.singleBounce;
	m_irradianceFormatIndex = spec.irradianceFormatIndex;
	m_depthFormatIndex = spec.depthFormatIndex;

	// Special case of 1-probe high surface
	for (int i = 0; i < 3; ++i)
	{
		if (m_specification.probeCounts[i] == 1)
		{
			m_probeStartPosition[i] = (hi[i] + lo[i]) / 2.0f;
		}
	}
}

Point3int32 IrradianceField::probeIndexToGridIndex(int index) const
{
	const int xIndex = index % m_specification.probeCounts.x;
	const int yIndex = (index % (m_specification.probeCounts.x * m_specification.probeCounts.y)) / m_specification.probeCounts.x;
	const int zIndex = index / (m_specification.probeCounts.x * m_specification.probeCounts.y);
	return Point3int32(xIndex, yIndex, zIndex);
}

Point3 IrradianceField::probeIndexToPosition(int index) const
{
	const Point3int32 P = probeIndexToGridIndex(index);
	return m_probeStep * Vector3(P) + m_probeStartPosition;
}

void IrradianceField::onGraphics3D(RenderDevice* rd, const Array<shared_ptr<Surface>>& surfaceArray)
{
	if (m_sceneDirty && System::time() - lastSceneUpdateTime() > 0.1)
	{
		m_sceneTriTree->setContents(m_scene);
		m_sceneDirty = false;
	}

	generateIrradianceProbes(rd);
	generateIrradianceRays(rd, m_scene);
	sampleAndShadeIrradianceRays(rd, m_scene, surfaceArray);
	updateIrradianceProbes(rd, m_scene);
}

void IrradianceField::onSceneChanged(const shared_ptr<Scene>& scene)
{
	m_scene = scene;
	m_sceneDirty = true;
}

Color3 IrradianceField::probeCoordVisualizationColor(Point3int32 P)
{
	Color3 c(float(P.x & 1), float(P.y & 1), float(P.z & 1));
	// Make all probes the same brightness
	c /= max(c.r + c.g + c.b, 0.01f);
	return c * 0.6f + Color3(0.2f);
}

void IrradianceField::debugDraw() const
{
	const float radius = 0.075f;
	for (int i = 0; i < probeCount(); ++i)
	{
		Color3 color;
		const Point3& probeCenter = probeIndexToPosition(i);

		const Point3int32 P = probeIndexToGridIndex(i);
		color = probeCoordVisualizationColor(P);
		//color = Color3::fromASRGB(0xff007e);

		::debugDraw(std::make_shared<SphereShape>(probeCenter, radius), 0.0f, color * 0.8f, Color4::clear());
	}
}

void IrradianceField::allocateIntermediateBuffers()
{
	const ImageFormat* depthFormat = ImageFormat::DEPTH32();

	GBuffer::Specification gbufferRTSpec;

	gbufferRTSpec.encoding[GBuffer::Field::LAMBERTIAN].format = ImageFormat::RGBA32F();
	gbufferRTSpec.encoding[GBuffer::Field::GLOSSY].format = ImageFormat::RGBA32F();
	gbufferRTSpec.encoding[GBuffer::Field::EMISSIVE].format = ImageFormat::RGBA32F();
	gbufferRTSpec.encoding[GBuffer::Field::TRANSMISSIVE].format = ImageFormat::RGBA32F();
	gbufferRTSpec.encoding[GBuffer::Field::WS_POSITION].format = ImageFormat::RGBA32F();
	gbufferRTSpec.encoding[GBuffer::Field::WS_NORMAL] = Texture::Encoding(ImageFormat::RGBA32F(), FrameName::CAMERA, 1.0f, 0.0f);
	gbufferRTSpec.encoding[GBuffer::Field::DEPTH_AND_STENCIL].format = nullptr;
	gbufferRTSpec.encoding[GBuffer::Field::CS_NORMAL] = nullptr;
	gbufferRTSpec.encoding[GBuffer::Field::CS_POSITION] = nullptr;

	int rayDimX = probeCount();
	int rayDimY = m_specification.irradianceRaysPerProbe;

	m_irradianceRaysGBuffer = GBuffer::create(gbufferRTSpec, "IrradianceField::m_irradianceRaysGBuffer");
	m_irradianceRaysGBuffer->setSpecification(gbufferRTSpec);
	m_irradianceRaysGBuffer->resize(rayDimX, rayDimY);
}

void IrradianceField::renderIndirectIllumination
   (RenderDevice*                          rd,
	const shared_ptr<GBuffer>&             gbuffer,
	const LightingEnvironment&             environment)
{
	m_giFramebuffer->resize(gbuffer->width(), gbuffer->height());

	// Compute GI
	rd->push2D(m_giFramebuffer); {
		rd->setGuardBandClip2D(gbuffer->colorGuardBandThickness());
		// Don't shade the skybox on this pass because it will be forward rendered
		rd->setDepthTest(RenderDevice::DEPTH_GREATER);
		Args args;
		gbuffer->setShaderArgsRead(args, "gbuffer_");
		args.setRect(rd->viewport());
		setShaderArgs(args, "irradianceFieldSurface.");
		m_irradianceRayOrigins->setShaderArgs(args, "gbuffer_WS_RAY_ORIGIN_", Sampler::buffer());
		args.setUniform("energyPreservation", recursiveEnergyPreservation);
		args.setMacro("RT_GBUFFER", 1);

		LAUNCH_SHADER("shaders/GIRenderer_ComputeIndirect.pix", args);
	} rd->pop2D();
}

void IrradianceField::generateIrradianceRays(RenderDevice* rd, const shared_ptr<Scene>& scene)
{
	BEGIN_PROFILER_EVENT("generateIrradianceRays");

	rd->push2D(m_irradianceRaysFB); {
		Args args;

		args.setMacro("RAYS_PER_PROBE", m_specification.irradianceRaysPerProbe);
		args.setRect(rd->viewport());
		
		setShaderArgs(args, "irradianceFieldSurface.");
		args.setUniform("randomOrientation", Matrix3::fromAxisAngle(Vector3::random(), Random::common().uniform(0.f, 2 * pif())));

		LAUNCH_SHADER("shaders/IrradianceField_GenerateRandomRays.pix", args);

	} rd->pop2D();

	END_PROFILER_EVENT();
}

void IrradianceField::sampleAndShadeArbitraryRays
   (RenderDevice*                       rd,
	const Array<shared_ptr<Surface>>&   surfaceArray,
	const shared_ptr<Framebuffer>&      targetFramebuffer,
	const LightingEnvironment&          environment,
	const shared_ptr<Texture>&          rayOrigins,
	const shared_ptr<Texture>&          rayDirections,
	const bool                          useProbeIndirect,
	const bool                          glossyToMatte,
	const shared_ptr<GBuffer>&          gbuffer,
	const TriTree::IntersectRayOptions  traceOptions)
{
	BEGIN_PROFILER_EVENT("sampleAndShadeArbitraryRays");
	//m_sceneTriTree->intersectRays(rayOrigins, rayDirections, gbuffer, traceOptions);

	int Width = rayOrigins->width();
	int Height = rayOrigins->height();
	shared_ptr<GLPixelTransferBuffer> RTOutBuffers[5];
	for (int i = 0; i < 5; ++i)
	{
		switch (i) {
		case 2:
		case 3:
			RTOutBuffers[i] = GLPixelTransferBuffer::create(Width, Height, ImageFormat::RGBA8());// , nullptr, 1, GL_STREAM_DRAW);
			break;
		default:
			RTOutBuffers[i] = GLPixelTransferBuffer::create(Width, Height, ImageFormat::RGBA32F());// , nullptr, 1, GL_STREAM_DRAW);
		}
	}

	m_sceneTriTree->intersectRays(rayOrigins->toPixelTransferBuffer(), rayDirections->toPixelTransferBuffer(), RTOutBuffers);

	gbuffer->texture(GBuffer::Field::WS_POSITION)->update(RTOutBuffers[0]);
	gbuffer->texture(GBuffer::Field::WS_NORMAL)->update(  RTOutBuffers[1]);
	gbuffer->texture(GBuffer::Field::LAMBERTIAN)->update( RTOutBuffers[2]);
	gbuffer->texture(GBuffer::Field::GLOSSY)->update(     RTOutBuffers[3]);
	gbuffer->texture(GBuffer::Field::EMISSIVE)->update(   RTOutBuffers[4]);

	renderIndirectIllumination(rd, gbuffer, environment);

	// Find the skybox
	shared_ptr<SkyboxSurface> skyboxSurface;
	for (const shared_ptr<Surface>& surface : surfaceArray) 
	{
		skyboxSurface = dynamic_pointer_cast<SkyboxSurface>(surface);
		if (skyboxSurface) { break; }
	}

	//////////////////////////////////////////////////////////////////////////////////
	// Perform deferred shading on the GBuffer
	rd->push2D(targetFramebuffer); {
		// Disable screen-space effects. Note that this is a COPY we're making in order to mutate it
		LightingEnvironment e = environment;
		e.ambientOcclusionSettings.enabled = false;

		Args args;
		e.setShaderArgs(args);
		gbuffer->setShaderArgsRead(args, "gbuffer_");
		args.setRect(rd->viewport());

		args.setMacro("GLOSSY_TO_MATTE", glossyToMatte);
		args.setUniform("matteIndirectBuffer", useProbeIndirect ? m_giFramebuffer->texture(0) : Texture::opaqueBlack(), Sampler::buffer());
		args.setMacro("LIGHTING_MODE", LightingMode::DIRECT_INDIRECT);

		args.setMacro("OVERRIDE_SKYBOX", true);
		if (skyboxSurface) skyboxSurface->setShaderArgs(args, "skybox_");

		// When rendering the probes, we don't have ray traced glossy reflections at the probe's primary ray hits,
		// so use the environment map (won't matter, because we usually kill all glossy reflection for irradiance
		// probes anyway since it is so viewer dependent).
		args.setMacro("USE_GLOSSY_INDIRECT_BUFFER", false);
		rayOrigins->setShaderArgs(args, "gbuffer_WS_RAY_ORIGIN_", Sampler::buffer());
		rayDirections->setShaderArgs(args, "gbuffer_WS_RAY_DIRECTION_", Sampler::buffer());

		LAUNCH_SHADER("shaders/GIRenderer_DeferredShade.pix", args);
	} rd->pop2D();

	END_PROFILER_EVENT();
}

void IrradianceField::sampleAndShadeIrradianceRays(RenderDevice* rd, const shared_ptr<Scene>& scene, const Array<shared_ptr<Surface>>& surfaceArray)
{
	BEGIN_PROFILER_EVENT("sampleIrradianceRays");

	m_irradianceRaysGBuffer->prepare(rd, 0.0f, 0.0f, Vector2int16(0, 0), Vector2int16(0, 0));

	// Don't cull backfaces...if a probe looks through a back face (e.g., single-sided ceiling), it will get incorrect results
	sampleAndShadeArbitraryRays
	    (rd,
		surfaceArray,
		m_irradianceRaysShadedFB,
		scene->lightingEnvironment(),
		m_irradianceRayOrigins,
		m_irradianceRayDirections,
		!m_oneBounce,
		m_specification.glossyToMatte,
		m_irradianceRaysGBuffer,
		TriTree::DO_NOT_CULL_BACKFACES);

	END_PROFILER_EVENT();
}

void IrradianceField::updateIrradianceProbes(RenderDevice* rd, const shared_ptr<Scene>& scene)
{
	BEGIN_PROFILER_EVENT("updateIrradianceProbes");

	static const bool IRRADIANCE = true, DEPTH = false;

	updateIrradianceProbe(rd, IRRADIANCE);
	updateIrradianceProbe(rd, DEPTH);

	m_firstFrame = false;

	END_PROFILER_EVENT();
}

void IrradianceField::updateIrradianceProbe(RenderDevice* rd, bool irradiance)
{
	rd->push2D(irradiance ? m_irradianceProbeFB : m_meanDistProbeFB); {

		rd->setBlendFunc(RenderDevice::BLEND_SRC_ALPHA, RenderDevice::BLEND_ONE_MINUS_SRC_ALPHA);
		// Set the depth test to discard the border pixels
		rd->setDepthTest(RenderDevice::DepthTest::DEPTH_GREATER);
		Args args;

		args.setMacro("RAYS_PER_PROBE", m_specification.irradianceRaysPerProbe);
		args.setUniform("hysteresis", m_firstFrame ? 0.0f : m_specification.hysteresis);
		args.setUniform("depthSharpness", m_specification.depthSharpness);
		// Uniforms to compute texel to direction and back in oct format
		args.setUniform("fullTextureWidth", irradiance ? m_irradianceProbeFB->width() : m_meanDistProbeFB->width());
		args.setUniform("fullTextureHeight", irradiance ? m_irradianceProbeFB->height() : m_meanDistProbeFB->height());
		args.setUniform("probeSideLength", irradiance ? irradianceOctSideLength() : depthOctSideLength());
		args.setUniform("maxDistance", m_maxDistance);
		setShaderArgs(args, "irradianceFieldSurface.");
		args.setRect(rd->viewport());

		m_irradianceRaysGBuffer->texture(GBuffer::Field::WS_POSITION)->setShaderArgs(args, "rayHitLocations.", Sampler::buffer());
		m_irradianceRaysGBuffer->texture(GBuffer::Field::WS_NORMAL)->setShaderArgs(args, "rayHitNormals.", Sampler::buffer());

		m_irradianceRayOrigins->setShaderArgs(args, "rayOrigins.", Sampler::buffer());
		m_irradianceRayDirections->setShaderArgs(args, "rayDirections.", Sampler::buffer());
		m_irradianceRaysShadedFB->texture(0)->setShaderArgs(args, "rayHitRadiance.", Sampler::buffer());

		// Set skybox args to read on miss
		dynamic_pointer_cast<Skybox>(m_scene->entity("skybox"))->keyframeArray()[0]->setShaderArgs(args, "skybox_", Sampler::defaults());

		args.setMacro("OUTPUT_IRRADIANCE", irradiance);
		LAUNCH_SHADER("shaders/IrradianceField_UpdateIrradianceProbe.pix", args);
	} rd->pop2D();

	//rd->push2D(irradiance ? m_irradianceProbeFB : m_meanDistProbeFB); {
	//
	//	//rd->setBlendFunc(RenderDevice::BLEND_SRC_ALPHA, RenderDevice::BLEND_ONE_MINUS_SRC_ALPHA);
	//	rd->setDepthTest(RenderDevice::DEPTH_LEQUAL);
	//	Args args;
	//	args.setUniform("fullTextureWidth", irradiance ? m_irradianceProbeFB->width() : m_meanDistProbeFB->width());
	//	args.setUniform("fullTextureHeight", irradiance ? m_irradianceProbeFB->height() : m_meanDistProbeFB->height());
	//
	//	args.setUniform("probeSideLength", irradiance ? irradianceOctSideLength() : depthOctSideLength());
	//	args.setUniform("probeTexture", irradiance ? m_irradianceProbes : m_meanDistProbes, Sampler::buffer());
	//
	//	args.setRect(rd->viewport());
	//	LAUNCH_SHADER("shaders/IrradianceField_CopyProbeEdges.pix", args);
	//} rd->pop2D();
}

void IrradianceField::generateIrradianceProbes(RenderDevice* rd)
{
	const int irradianceSide = irradianceOctSideLength();
	const int depthSide = depthOctSideLength();

	const int rayDimX = m_specification.irradianceRaysPerProbe;
	const int rayDimY = probeCount();

	// Allocate or reallocate the ray tracing buffers if the probe requirements change
	if (isNull(m_irradianceRayOrigins) ||
		m_irradianceRayOrigins->width() != rayDimX ||
		m_irradianceRayOrigins->height() != rayDimY)
	{
		m_irradianceRayOrigins = Texture::createEmpty("IrradianceField::m_irradianceRayOrigins", rayDimX, rayDimY, ImageFormat::RGBA32F());
		m_irradianceRayDirections = Texture::createEmpty("IrradianceField::m_irradianceRayDirections", rayDimX, rayDimY, ImageFormat::RGBA32F());
		m_irradianceRaysFB = Framebuffer::create(m_irradianceRayOrigins, m_irradianceRayDirections);
		m_irradianceRaysShadedFB = Framebuffer::create(Texture::createEmpty("IrradianceField::m_irradianceRaysShadedFB", rayDimX, rayDimY, ImageFormat::RGB32F()));
		m_giFramebuffer = Framebuffer::create(Texture::createEmpty("IrradianceField::matte indirect", rayDimX, rayDimY, ImageFormat::RGBA32F()));
	}

	static int oldIrradianceSide = 0;
	static int oldDepthSide = 0;

	// Allocate irradiance/depth probes if this is the first call or the probe resolution changes (mostly for debugging; in normal use,
	// this is only invoked once anyway)
	if (isNull(m_irradianceProbes) ||
		irradianceSide != oldIrradianceSide ||
		depthSide != oldDepthSide ||
		m_irradianceProbes->format() != s_irradianceFormats[m_irradianceFormatIndex] ||
		m_meanDistProbes->format() != s_depthFormats[m_depthFormatIndex] ||
		m_probeFormatChanged)
	{
		m_probeFormatChanged = false;

		// 1-pixel of padding surrounding each probe, 1-pixel padding surrounding entire texture for alignment.
		const int irradianceWidth = (irradianceSide + 2) * m_specification.probeCounts.x * m_specification.probeCounts.y + 2;
		const int irradianceHeight = (irradianceSide + 2) * m_specification.probeCounts.z + 2;

		const int depthWidth = (depthSide + 2) * m_specification.probeCounts.x * m_specification.probeCounts.y + 2;
		const int depthHeight = (depthSide + 2) * m_specification.probeCounts.z + 2;

		m_irradianceProbes = Texture::createEmpty("IrradianceField::m_irradianceProbes", irradianceWidth, irradianceHeight, s_irradianceFormats[m_irradianceFormatIndex], Texture::DIM_2D, false, 1);
		m_meanDistProbes = Texture::createEmpty("IrradianceField::m_meanDistProbes", depthWidth, depthHeight, s_depthFormats[m_depthFormatIndex], Texture::DIM_2D, false, 1);

		m_irradianceProbeFB = Framebuffer::create(m_irradianceProbes);
		m_meanDistProbeFB = Framebuffer::create(m_meanDistProbes);

		m_irradianceProbeFB->set(Framebuffer::DEPTH, Texture::createEmpty("irradianceStencil", m_irradianceProbeFB->width(), m_irradianceProbeFB->height(), ImageFormat::DEPTH32()));
		m_meanDistProbeFB->set(Framebuffer::DEPTH, Texture::createEmpty("depthStencil", m_meanDistProbeFB->width(), m_meanDistProbeFB->height(), ImageFormat::DEPTH32()));

		// Write 1 outside probe octahedron
		for (int i = 0; i < 2; ++i)
		{
			rd->push2D(i == 0 ? m_irradianceProbeFB : m_meanDistProbeFB); {

				rd->setColorClearValue(Color4(0, 0, 0, 0));
				rd->setDepthWrite(true);
				rd->clear();
				Args args;

				args.setUniform("probeSideLength", (i == 0) ? irradianceSide : depthSide);
				args.setRect(rd->viewport());
				LAUNCH_SHADER("shaders/IrradianceField_WriteOnesToProbeBorders.pix", args);

			}; rd->pop2D();
		}
	}
	oldIrradianceSide = irradianceSide;
	oldDepthSide = depthSide;
}
