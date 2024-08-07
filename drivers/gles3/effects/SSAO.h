#ifndef SSAO_GLES3_H
#define SSAO_GLES3_H
#include "core/math/projection.h"
#include "core/math/vector2i.h"
#include "core/templates/rid.h"
#include "drivers/gles3/shaders/effects/ssao.glsl.gen.h"
#include "drivers/gles3/shaders/effects/ssao_blur.glsl.gen.h"
// #include "drivers/gles3/shaders/effects/ssao_importance_map.glsl.gen.h"
#include "drivers/gles3/shaders/effects/ssao_interleave.glsl.gen.h"
// #include "glad/gl.h"

#define RB_DEINTERLEAVED SNAME("deinterleaved")
#define RB_DEINTERLEAVED_PONG SNAME("deinterleaved_pong")
#define RB_IMPORTANCE_MAP SNAME("importance_map")
#define RB_IMPORTANCE_PONG SNAME("importance_pong")
#define RB_FINAL SNAME("final")
#define RB_NORMAL SNAME("normal")

#ifdef GLES3_ENABLED

// #include "drivers/gles3/storage/render_scene_buffers_gles3.h"
// #include "drivers/gles3/shaders/effects/glow.glsl.gen.h"

namespace GLES3 {
class SSAO {
private:
	static SSAO *singleton;

	enum SSAOMode {
		SSAO_GATHER,
		SSAO_GATHER_BASE,
		SSAO_GATHER_ADAPTIVE,
		SSAO_GENERATE_IMPORTANCE_MAP,
		SSAO_PROCESS_IMPORTANCE_MAPA,
		SSAO_PROCESS_IMPORTANCE_MAPB,
		SSAO_BLUR_PASS,
		SSAO_BLUR_PASS_SMART,
		SSAO_BLUR_PASS_WIDE,
		SSAO_INTERLEAVE,
		SSAO_INTERLEAVE_SMART,
		SSAO_INTERLEAVE_HALF,
		SSAO_MAX
	};

	struct SSAOGatherConstant {
		float rotation_matrices[80]; //5 vec4s * 4
		GLuint ubo;
	};

	struct SSAOGatherPushConstant {
		float screen_size[2];
		int pass;
		int quality;

		float half_screen_pixel_size[2];
		int size_multiplier;
		float detail_intensity;

		float NDC_to_view_mul[2];
		float NDC_to_view_add[2];

		float NDC_to_view_depth_AB[2];
		float half_screen_pixel_size_x025[2];

		float radius;
		float intensity;
		float shadow_power;
		float shadow_clamp;

		float fade_out_mul;
		float fade_out_add;
		float horizon_angle_threshold;
		float inv_radius_near_limit;

		uint32_t is_orthogonal;
		float neg_inv_radius;
		float load_counter_avg_div;
		float adaptive_sample_limit;

		float pass_coord_offset[2];
		float pass_uv_offset[2];
	};

	struct SSAOBlurPushConstant {
		float edge_sharpness;
		float source_index;
		float half_screen_pixel_size[2];
	};

	struct SSAOInterleavePushConstant {
		float inv_sharpness;
		int size_modifier;
		float pixel_size[2];
	};

	struct Shaders {
		SSAOGatherPushConstant gather_push_constant;
		SSAOGatherConstant gather_constant;
		SsaoShaderGLES3 gather_shader;
		RID gather_shader_version;

		SSAOBlurPushConstant blur_push_constant;
		SsaoBlurShaderGLES3 blur_shader;
		RID blur_shader_version;

		SSAOInterleavePushConstant interleave_push_constant;
		SsaoInterleaveShaderGLES3 interleave_shader;
		RID interleave_shader_version;

		// SsaoImportanceMapShaderGLES3 importance_map_shader;
		// RID importance_map_shader_version;
	} ssao;


	GLuint screen_triangle = 0;
	GLuint screen_triangle_array = 0;

	static const int k_bound_texture_size = 5;
	GLint bound_texture[k_bound_texture_size];

	RS::EnvironmentSSAOQuality ssao_quality = RS::ENV_SSAO_QUALITY_MEDIUM;
	bool ssao_half_size = false;
	float ssao_adaptive_target = 0.5;
	int ssao_blur_passes = 2;
	float ssao_fadeout_from = 50.0;
	float ssao_fadeout_to = 300.0;

	static const int k_is_frambuffer_same_size = 1;

	void _draw_screen_triangle();

public:
	struct SSAOBuffer {
		Size2i size;
		GLuint color = 0;
		GLuint fbo = 0;
	};

	struct Buffers {
		SSAOBuffer deinterleaved;
		SSAOBuffer deinterleaved_pong;
		SSAOBuffer importance_map;
		SSAOBuffer importance_pong;
		SSAOBuffer final;
		SSAOBuffer normal;
	};

	struct SSAORenderBufferProperty {
		bool half_size = false;
		int buffer_width;
		int buffer_height;
		int half_buffer_width;
		int half_buffer_height;
	} ssao_buffer_property;

	struct SSAOSettings {
		float radius = 1.0;
		float intensity = 2.0;
		float power = 1.5;
		float detail = 0.5;
		float horizon = 0.06;
		float sharpness = 0.98;

		Size2i full_screen_size;
	};

	SSAO();
	~SSAO();

	static SSAO *get_singleton();
	void generate_ssao(SSAORenderBufferProperty &p_ssao_buffers, Buffers &p_buffers, int p_view_index, const Projection &p_projection, const SSAOSettings &p_settings, GLuint
			p_depth_buffer);

	void ssao_set_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to);
	RS::EnvironmentSSAOQuality ssao_get_quality() const;
	bool is_ssao_half_size() const;
};
} //namespace GLES3

#endif // GLES3_ENABLED

#endif // SSAO_GLES3_H
