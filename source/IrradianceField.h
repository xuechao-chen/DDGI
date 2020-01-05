#pragma once
#include <G3D/G3D.h>

G3D_DECLARE_ENUM_CLASS(LightingMode, DIRECT_INDIRECT, DIRECT_ONLY, INDIRECT_ONLY);

class IrradianceField : public ReferenceCountedObject 
{
protected:
	friend class App; // This is here for exposing debugging parameters

	struct Specification 
	{
		AABox           probeDimensions = AABox(Point3(0.0f, 0.0f, 0.0f), Point3(1.0f, 1.0f, 1.0f));

		Vector3int32    probeCounts = Vector3int32(4, 2, 4);

		/** Side length of one face */
		int             irradianceOctResolution = 8;
		int             depthOctResolution = 16;

		/** Subtract a little distance = bias (pull sample point) to avoid
			texel artifacts (self-shadowing grids).  */
		float           irradianceDistanceBias = 0.0f;

		/** Add a little variance = smooth out bias / self-shadow.
			Larger values create smoother indirect shadows but als light leaks. */
		float           irradianceVarianceBias = 0.02f;

		/** Bias the to avoid light leaks with thin walls.
			Usually [0, 0.5]. 0.05 is a typical value at 32^2 resolution cube-map probes.
			AO will often cover these as well. Setting the value too LARGE can create
			light leaks in corners as well. */
		float           irradianceChebyshevBias = 0.07f;

		/** Slightly bump the location of the shadow test point away from the shadow casting surface.
			The shadow casting surface is the boundary for shadow, so the nearer an imprecise value is
			to it the more the light leaks.
		*/
		float           normalBias = 0.25f;

		/** Control the weight of new rays when updating each irradiance probe. A value close to 1 will
			very slowly change the probe textures, improving stability but reducing accuracy when objects
			move in the scene, while values closer to 0.9 or lower will rapidly react to scene changes
			but exhibit flickering.
		*/
		float           hysteresis = 0.98f;

		/** Exponent for depth testing. A high value will rapidly react to depth discontinuities, but risks
			exhibiting banding.
		*/
		float           depthSharpness = 50.0f;

		/** Number of rays emitted each frame for each probe in the scene */
		int             irradianceRaysPerProbe = 64;

		/** If true, add the glossy coefficient in to matte term for a single albedo. Eliminates low-probability,
			temporally insensitive caustic effects. */
		bool            glossyToMatte = true;

		bool            singleBounce = false;

		int             irradianceFormatIndex = 4;
		int             depthFormatIndex = 1;

		bool            showLights = false;
		bool            encloseBounds = false;

		Specification();

		Any toAny() const;

		Specification(const Any& any);
	};

	struct CubeMapProbe 
	{
		shared_ptr<Texture> radiance;
		shared_ptr<Texture> depth;
		shared_ptr<Texture> normals;
	};


	static const Array<const ImageFormat*>     s_irradianceFormats;
	static const Array<const ImageFormat*>     s_depthFormats;

	const int                           m_overheadViewDebugResolution = 512;

	Specification                       m_specification;

	/** Maximum distance that can be written to a probe. */
	float                               m_maxDistance = 4.0f;

	/** Should the probes enclose the scene boundary (true) or be enclosed by it (false)? */
	bool                                m_encloseScene = false;

	/**
		Low resolution irradiance probes, R11G11B10F radiance
		Cubemap array
	*/
	shared_ptr<Texture>                 m_irradianceProbes;

	/**
		Low resolution variance shadow-map style probes, RG32F()
		X channel is distance, Y channel is sum of squared distances
		Cubemap array
	*/
	shared_ptr<Texture>                 m_meanDistProbes;

	/** Framebuffers associated with each probe */
	shared_ptr<Framebuffer>             m_irradianceProbeFB;
	shared_ptr<Framebuffer>             m_meanDistProbeFB;

	Point3                              m_probeStartPosition;
	Vector3                             m_probeStep;

	String                              m_name;

	int                                 m_irradianceFormatIndex = 4;
	int                                 m_depthFormatIndex = 1;
	bool                                m_probeFormatChanged;

	/** Scene tree used for accelerated ray-tracing */
	shared_ptr<TriTree>                 m_sceneTriTree;

	/** Textures storing ray origins and directions for irradiance probe sampling,
		regenerated every frame and then split between all probes according to a given heuristic */
	shared_ptr<Texture>                 m_irradianceRayOrigins;
	shared_ptr<Texture>                 m_irradianceRayDirections;
	shared_ptr<Framebuffer>             m_irradianceRaysFB;

	shared_ptr<GBuffer>                 m_irradianceRaysGBuffer;
	shared_ptr<Framebuffer>             m_irradianceRaysShadedFB;

	shared_ptr<Scene>                   m_scene;

	LightingMode                        m_lightingMode = LightingMode::DIRECT_INDIRECT;

	bool                                m_sceneDirty = true;

	shared_ptr<Framebuffer>             m_giFramebuffer;

	Point3 probeIndexToPosition(int index) const;

	Point3int32 probeIndexToGridIndex(int index) const;

	void init(const Specification& spec);

	IrradianceField();

	/** allocates all of the framebuffers/gbuffers/textures
		needed for re-generating the irradiancefield. */
	void allocateIntermediateBuffers();

	/** Generate rays for irradiance probe updates. */
	void generateIrradianceRays(RenderDevice* rd, const shared_ptr<Scene>& scene);

	/** Sample rays for irradiance probe updates, returning shaded hit points. */
	void sampleAndShadeIrradianceRays(RenderDevice* rd, const shared_ptr<Scene>& scene, const Array<shared_ptr<Surface>>& surfaceArray);

	/** Update irradiance probes at runtime using newly sampled rays. */
	void updateIrradianceProbes(RenderDevice* rd, const shared_ptr<Scene>& scene);

	/** Update a single irradiance probe at runtime using newly sampled rays. */
	void updateIrradianceProbe(RenderDevice* rd, bool irradiance);

	void renderIndirectIllumination
	(RenderDevice*							   rd,
	 const shared_ptr<GBuffer>&                gbuffer,
	 const LightingEnvironment&                environment);

public:

	void sampleAndShadeArbitraryRays
	(RenderDevice*								rd,
	 const Array<shared_ptr<Surface>>&          surfaceArray,
	 const shared_ptr<Framebuffer>&             targetFramebuffer,
	 const LightingEnvironment&                 environment,
	 const shared_ptr<Texture>&                 rayOrigins,
	 const shared_ptr<Texture>&                 rayDirections,
	 const bool                                 useProbeIndirect,
	 const bool                                 glossyToMatte,
	 const shared_ptr<GBuffer>&                 gbuffer,
	 const TriTree::IntersectRayOptions         traceOptions);

	// Return maxProbeDistance so we can set it in the shader. Note that we may also use this value on the way in
	// to *set* the maxProbeDistance, or at set the initial distance before converting to powers of two.
	void loadNewScene
	(const String&            sceneName, 
	 const shared_ptr<Scene>& scene,
	 Vector3int32             probeCountsOverride, 
	 float                    maxProbeDistance, 
	 int                      irradianceCubeResolutionOverride = -1, 
	 int                      depthCubeResolutionOverride      = -1);
	/** If true, set hysteresis to zero and force all probes to re-render.
		Used for when parameters change */
	bool                        m_firstFrame = true;
	bool                        m_oneBounce = false;

	void generateIrradianceProbes(RenderDevice* rd);

	void setShaderArgs(UniformTable& args, const String& prefix);

	bool encloseScene() {
		return m_encloseScene;
	}

	void setEncloseScene(bool b) {
		m_encloseScene = b;
	}

	float gRaysPerFrame() {
		return float(m_irradianceRayOrigins->width() * m_irradianceRayDirections->height()) / 1000000000.0f;
	}

	static const ImageFormat* distanceFormat() {
		return ImageFormat::R16F();
	}

	const int irradianceOctSideLength() {
		return m_specification.irradianceOctResolution;
	}
	const int depthOctSideLength() {
		return m_specification.depthOctResolution;
	}

	void setIrradianceOctSideLength(int sideLengthSize, RenderDevice* rd) {
		m_specification.irradianceOctResolution = sideLengthSize;
		generateIrradianceProbes(rd);
	}

	void setDepthOctSideLength(int sideLengthSize, RenderDevice* rd) {
		m_specification.depthOctResolution = sideLengthSize;
		generateIrradianceProbes(rd);
	}

	const ImageFormat* irradianceFormat() {
		return s_irradianceFormats[m_irradianceFormatIndex];
	}

	static const Texture::Encoding& normalEncoding() {
		static Texture::Encoding enc(ImageFormat::RG8(), FrameName::WORLD, 2.0f, -1.0f);
		return enc;
	}

	RealTime lastSceneUpdateTime() {
		return m_sceneTriTree->lastBuildTime();
	}

	int probeCount() const {
		return m_specification.probeCounts.x * m_specification.probeCounts.y * m_specification.probeCounts.z;
	}

	const Vector3int32& probeCounts() const {
		return m_specification.probeCounts;
	}

	static shared_ptr<IrradianceField> create
	(const String&            sceneFilename, 
	 const shared_ptr<Scene>& scene,
	 Vector3int32             probeCountsOverride              = Vector3int32(-1, -1, -1), 
     float                    maxProbeDistance                 = -1.0f, 
     int                      irradianceCubeResolutionOverride = -1);

	/** The surfaceArray is only used to find the skybox */
	virtual void onGraphics3D(RenderDevice* rd, const Array<shared_ptr<Surface>>& surfaceArray);

	virtual void onSceneChanged(const shared_ptr<Scene>& scene);
};