#include "drivers/gles3/storage/texture_storage.h"
// #include "servers/rendering/storage/utilities.h"
#ifdef GLES3_ENABLED

#include "SSAO.h"

using namespace GLES3;

SSAO *SSAO::singleton = nullptr;

void SSAO::_draw_screen_triangle() {
	glBindVertexArray(screen_triangle_array);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

SSAO::SSAO() {
	singleton = this;

	{
		ssao.gather_shader.initialize();
		ssao.gather_shader_version = ssao.gather_shader.version_create();
	}

	// {
	// 	ssao.importance_map_shader.initialize();
	// 	ssao.importance_map_shader_version = ssao.importance_map_shader.version_create();
	// }
	//
	{
		ssao.blur_shader.initialize();
		ssao.blur_shader_version = ssao.blur_shader.version_create();
	}

	{
		ssao.interleave_shader.initialize();
		ssao.interleave_shader_version = ssao.interleave_shader.version_create();
	}

	{
		// Screen Triangle.
		glGenBuffers(1, &screen_triangle);
		glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);

		const float qv[6] = {
			-1.0f,
			-1.0f,
			3.0f,
			-1.0f,
			-1.0f,
			3.0f,
		};

		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, qv, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind

		glGenVertexArrays(1, &screen_triangle_array);
		glBindVertexArray(screen_triangle_array);
		glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
		glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
		glEnableVertexAttribArray(RS::ARRAY_VERTEX);
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind
	}

	{
		const int sub_pass_count = 5;
		for (int pass = 0; pass < 4; pass++) {
			// String output="";
			for (int subPass = 0; subPass < sub_pass_count; subPass++) {
				int a = pass;

				int spmap[5]{ 0, 1, 4, 3, 2 };
				int b = spmap[subPass];

				float ca, sa;
				float angle0 = (float(a) + float(b) / float(sub_pass_count)) * Math_PI * 0.5f;

				ca = Math::cos(angle0);
				sa = Math::sin(angle0);

				float scale = 1.0f + (a - 1.5f + (b - (sub_pass_count - 1.0f) * 0.5f) / float(sub_pass_count)) * 0.07f;

				ssao.gather_constant.rotation_matrices[pass * 20 + subPass * 4 + 0] = scale * ca;
				ssao.gather_constant.rotation_matrices[pass * 20 + subPass * 4 + 1] = scale * -sa;
				ssao.gather_constant.rotation_matrices[pass * 20 + subPass * 4 + 2] = -scale * sa;
				ssao.gather_constant.rotation_matrices[pass * 20 + subPass * 4 + 3] = -scale * ca;

				// output+="vec4(";
				// for(int i=0;i<4;i++) {
				// 	output+=rtos(ssao.gather_constant.rotation_matrices[pass * 20 + subPass * 4 + i]);
				// 	if(i<3)output+=",";
				// }
				// output+="), ";
			}
			// print_line(output);
		}

		// glGenBuffers(1, &ssao.gather_constant.ubo);
		// glBindBufferBase(GL_UNIFORM_BUFFER, 1, ssao.gather_constant.ubo);
		// glBufferData(GL_UNIFORM_BUFFER, sizeof(ssao.gather_constant.rotation_matrices), &ssao.gather_constant.rotation_matrices, GL_STATIC_DRAW);

		// glBindBufferRange(GL_UNIFORM_BUFFER, 1, ssao.gather_constant.ubo, 0, sizeof(ssao.gather_constant.rotation_matrices));
		// ssao.gather_shader.version_bind_shader(ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		// GLuint blockIndex = glGetUniformBlockIndex(ssao.gather_shader.get_current_shader()->id, "Constants");
		// glUniformBlockBinding(ssao.gather_shader.get_current_shader()->id, blockIndex, 1);
		// GLint blockSize;
		// glGetActiveUniformBlockiv(ssao.gather_shader.get_current_shader()->id, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		// print_line(blockSize);

		// glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	ssao_set_quality(RS::EnvironmentSSAOQuality(int(GLOBAL_GET("rendering/environment/ssao/quality"))), GLOBAL_GET("rendering/environment/ssao/half_size"), GLOBAL_GET("rendering/environment/ssao/adaptive_target"), GLOBAL_GET("rendering/environment/ssao/blur_passes"), GLOBAL_GET("rendering/environment/ssao/fadeout_from"), GLOBAL_GET("rendering/environment/ssao/fadeout_to"));
}

SSAO::~SSAO() {
	glDeleteBuffers(1, &screen_triangle);
	// glDeleteBuffers(1, &ssao.gather_constant.ubo);
	glDeleteVertexArrays(1, &screen_triangle_array);

	ssao.gather_shader.version_free(ssao.gather_shader_version);
	ssao.blur_shader.version_free(ssao.blur_shader_version);
	ssao.interleave_shader.version_free(ssao.interleave_shader_version);
}

SSAO *SSAO::get_singleton() {
	return singleton;
}

void SSAO::generate_ssao(SSAORenderBufferProperty &p_ssao_buffers, Buffers &p_buffers, int p_view_index, const Projection &p_projection, const SSAOSettings &p_settings, GLuint
		p_depth_buffer) {
	GLboolean blend_enabled;
	glGetBooleanv(GL_BLEND, &blend_enabled);

	GLboolean depth_test_enabled;
	glGetBooleanv(GL_DEPTH_TEST, &depth_test_enabled);

	GLint framebuffer_origin;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_origin);

	//get viewport info
	GLint viewport_origin[4];
	glGetIntegerv(GL_VIEWPORT, viewport_origin);

	for (int i = 0; i < k_bound_texture_size; i++) {
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture[i]);
	}

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	/* SECOND PASS */
	//Gather
	{
		// RENDER_TIMESTAMP("SSAO Gather");

		ssao.gather_push_constant.screen_size[0] = p_settings.full_screen_size.x;
		ssao.gather_push_constant.screen_size[1] = p_settings.full_screen_size.y;

		ssao.gather_push_constant.half_screen_pixel_size[0] = 2.0 / p_settings.full_screen_size.x;
		ssao.gather_push_constant.half_screen_pixel_size[1] = 2.0 / p_settings.full_screen_size.y;
		if (k_is_frambuffer_same_size) {
			ssao.gather_push_constant.half_screen_pixel_size[0] /= 2.0;
			ssao.gather_push_constant.half_screen_pixel_size[1] /= 2.0;
		}

		if (ssao_half_size) {
			ssao.gather_push_constant.half_screen_pixel_size[0] *= 2.0;
			ssao.gather_push_constant.half_screen_pixel_size[1] *= 2.0;
		}
		ssao.gather_push_constant.half_screen_pixel_size_x025[0] = ssao.gather_push_constant.half_screen_pixel_size[0] * 0.75;
		ssao.gather_push_constant.half_screen_pixel_size_x025[1] = ssao.gather_push_constant.half_screen_pixel_size[1] * 0.75;
		float tan_half_fov_x = 1.0 / p_projection.columns[0][0];
		float tan_half_fov_y = 1.0 / p_projection.columns[1][1];
		ssao.gather_push_constant.NDC_to_view_mul[0] = tan_half_fov_x * 2.0;
		ssao.gather_push_constant.NDC_to_view_mul[1] = tan_half_fov_y * -2.0;
		ssao.gather_push_constant.NDC_to_view_add[0] = tan_half_fov_x * -1.0;
		ssao.gather_push_constant.NDC_to_view_add[1] = tan_half_fov_y;
		ssao.gather_push_constant.NDC_to_view_depth_AB[0] = p_projection.columns[2][2];
		ssao.gather_push_constant.NDC_to_view_depth_AB[1] = p_projection.columns[3][2];
		ssao.gather_push_constant.is_orthogonal = p_projection.is_orthogonal();

		ssao.gather_push_constant.radius = p_settings.radius;
		float radius_near_limit = (p_settings.radius * 1.2f);
		if (ssao_quality <= RS::ENV_SSAO_QUALITY_LOW) {
			radius_near_limit *= 1.50f;

			if (ssao_quality == RS::ENV_SSAO_QUALITY_VERY_LOW) {
				ssao.gather_push_constant.radius *= 0.8f;
			}
		}
		radius_near_limit /= tan_half_fov_y;
		ssao.gather_push_constant.intensity = p_settings.intensity;
		ssao.gather_push_constant.shadow_power = p_settings.power;
		ssao.gather_push_constant.shadow_clamp = 0.98;
		ssao.gather_push_constant.fade_out_mul = -1.0 / (ssao_fadeout_to - ssao_fadeout_from);
		ssao.gather_push_constant.fade_out_add = ssao_fadeout_from / (ssao_fadeout_to - ssao_fadeout_from) + 1.0;
		ssao.gather_push_constant.horizon_angle_threshold = p_settings.horizon;
		ssao.gather_push_constant.inv_radius_near_limit = 1.0f / radius_near_limit;
		ssao.gather_push_constant.neg_inv_radius = -1.0 / ssao.gather_push_constant.radius;

		ssao.gather_push_constant.load_counter_avg_div = 9.0 / float((p_ssao_buffers.half_buffer_width) * (p_ssao_buffers.half_buffer_height) * 255);
		ssao.gather_push_constant.adaptive_sample_limit = ssao_adaptive_target;

		ssao.gather_push_constant.detail_intensity = p_settings.detail;
		ssao.gather_push_constant.quality = MAX(0, ssao_quality - 1);
		ssao.gather_push_constant.size_multiplier = ssao_half_size ? 4 : (2 - k_is_frambuffer_same_size);

		bool success = ssao.gather_shader.version_bind_shader(ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		if (!success) {
			return;
		}

		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::SCREEN_SIZE, ssao.gather_push_constant.screen_size[0], ssao.gather_push_constant.screen_size[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::HALF_SCREEN_PIXEL_SIZE, ssao.gather_push_constant.half_screen_pixel_size[0], ssao.gather_push_constant.half_screen_pixel_size[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::HALF_SCREEN_PIXEL_SIZE_X025, ssao.gather_push_constant.half_screen_pixel_size_x025[0], ssao.gather_push_constant.half_screen_pixel_size_x025[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::NDC_TO_VIEW_MUL, ssao.gather_push_constant.NDC_to_view_mul[0], ssao.gather_push_constant.NDC_to_view_mul[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::NDC_TO_VIEW_ADD, ssao.gather_push_constant.NDC_to_view_add[0], ssao.gather_push_constant.NDC_to_view_add[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::NDC_TO_VIEW_DEPTH_AB, ssao.gather_push_constant.NDC_to_view_depth_AB[0], ssao.gather_push_constant.NDC_to_view_depth_AB[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::RADIUS, ssao.gather_push_constant.radius, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::INTENSITY, ssao.gather_push_constant.intensity, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::SHADOW_POWER, ssao.gather_push_constant.shadow_power, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::SHADOW_CLAMP, ssao.gather_push_constant.shadow_clamp, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::FADE_OUT_MUL, ssao.gather_push_constant.fade_out_mul, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::FADE_OUT_ADD, ssao.gather_push_constant.fade_out_add, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::HORIZON_ANGLE_THRESHOLD, ssao.gather_push_constant.horizon_angle_threshold, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::INV_RADIUS_NEAR_LIMIT, ssao.gather_push_constant.inv_radius_near_limit, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::IS_ORTHOGONAL, ssao.gather_push_constant.is_orthogonal, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::NEG_INV_RADIUS, ssao.gather_push_constant.neg_inv_radius, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::LOAD_COUNTER_AVG_DIV, ssao.gather_push_constant.load_counter_avg_div, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::ADAPTIVE_SAMPLE_LIMIT, ssao.gather_push_constant.adaptive_sample_limit, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::DETAIL_INTENSITY, ssao.gather_push_constant.detail_intensity, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::QUALITY, ssao.gather_push_constant.quality, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
		ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::SIZE_MULTIPLIER, ssao.gather_push_constant.size_multiplier, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, p_depth_buffer);
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, p_buffers.normal.color);

		// glBindBufferBase(GL_UNIFORM_BUFFER, 1, ssao.gather_constant.ubo);

		for (int i = 0; i < 4; i++) {
			ssao.gather_push_constant.pass_coord_offset[0] = i % 2;
			ssao.gather_push_constant.pass_coord_offset[1] = i / 2;
			ssao.gather_push_constant.pass_uv_offset[0] = ((i % 2) - 0.0) / p_settings.full_screen_size.x;
			ssao.gather_push_constant.pass_uv_offset[1] = ((i / 2) - 0.0) / p_settings.full_screen_size.y;
			ssao.gather_push_constant.pass = i;

			ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::PASS, ssao.gather_push_constant.pass, ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
			ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::PASS_COORD_OFFSET, ssao.gather_push_constant.pass_coord_offset[0], ssao.gather_push_constant.pass_coord_offset[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);
			ssao.gather_shader.version_set_uniform(SsaoShaderGLES3::PASS_UV_OFFSET, ssao.gather_push_constant.pass_uv_offset[0], ssao.gather_push_constant.pass_uv_offset[1], ssao.gather_shader_version, SsaoShaderGLES3::MODE_SSAO_BASE);

			glBindFramebuffer(GL_FRAMEBUFFER, p_buffers.deinterleaved.fbo);
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, p_buffers.deinterleaved.color, 0, p_view_index * 4 + i);
			glViewport(0, 0, p_buffers.deinterleaved.size.x, p_buffers.deinterleaved.size.y);

			_draw_screen_triangle();
		}
	}

	//	/* THIRD PASS */
	//	// Blur
	//
	{
		// RENDER_TIMESTAMP("SSAO Blur");
		ssao.blur_push_constant.edge_sharpness = 1.0 - p_settings.sharpness;
		ssao.blur_push_constant.half_screen_pixel_size[0] = 1.0 / p_ssao_buffers.buffer_width;
		ssao.blur_push_constant.half_screen_pixel_size[1] = 1.0 / p_ssao_buffers.buffer_height;

		int blur_passes = ssao_quality > RS::ENV_SSAO_QUALITY_VERY_LOW ? ssao_blur_passes : 1;

		for (int pass = 0; pass < blur_passes; pass++) {
			int blur_pipeline = SSAO_BLUR_PASS;
			if (ssao_quality > RS::ENV_SSAO_QUALITY_VERY_LOW) {
				if (pass < blur_passes - 2) {
					blur_pipeline = SSAO_BLUR_PASS_WIDE;
				} else {
					blur_pipeline = SSAO_BLUR_PASS_SMART;
				}
			}

			for (int i = 0; i < 4; i++) {
				if ((ssao_quality == RS::ENV_SSAO_QUALITY_VERY_LOW) && ((i == 1) || (i == 2))) {
					continue;
				}

				ssao.blur_push_constant.source_index = i;

				bool success = ssao.blur_shader.version_bind_shader(ssao.blur_shader_version, static_cast<SsaoBlurShaderGLES3::ShaderVariant>(blur_pipeline - SSAO_BLUR_PASS));
				if (!success) {
					return;
				}

				ssao.blur_shader.version_set_uniform(SsaoBlurShaderGLES3::EDGE_SHARPNESS, ssao.blur_push_constant.edge_sharpness, ssao.blur_shader_version, static_cast<SsaoBlurShaderGLES3::ShaderVariant>(blur_pipeline - SSAO_BLUR_PASS));
				ssao.blur_shader.version_set_uniform(SsaoBlurShaderGLES3::SOURCE_INDEX, ssao.blur_push_constant.source_index, ssao.blur_shader_version, static_cast<SsaoBlurShaderGLES3::ShaderVariant>(blur_pipeline - SSAO_BLUR_PASS));
				ssao.blur_shader.version_set_uniform(SsaoBlurShaderGLES3::HALF_SCREEN_PIXEL_SIZE, ssao.blur_push_constant.half_screen_pixel_size[0], ssao.blur_push_constant.half_screen_pixel_size[1], ssao.blur_shader_version, static_cast<SsaoBlurShaderGLES3::ShaderVariant>(blur_pipeline - SSAO_BLUR_PASS));

				if (pass % 2 == 0) {
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D_ARRAY, p_buffers.deinterleaved.color);

					glBindFramebuffer(GL_FRAMEBUFFER, p_buffers.deinterleaved_pong.fbo);
					glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, p_buffers.deinterleaved_pong.color, 0, p_view_index * 4 + i);
					glViewport(0, 0, p_buffers.deinterleaved_pong.size.x, p_buffers.deinterleaved_pong.size.y);
				} else {
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D_ARRAY, p_buffers.deinterleaved_pong.color);

					glBindFramebuffer(GL_FRAMEBUFFER, p_buffers.deinterleaved.fbo);
					glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, p_buffers.deinterleaved.color, 0, p_view_index * 4 + i);
					glViewport(0, 0, p_buffers.deinterleaved.size.x, p_buffers.deinterleaved.size.y);
				}

				_draw_screen_triangle();
			}
		}
	}

	/* FOURTH PASS */
	// Interleave buffers
	// back to full size
	{
		// RENDER_TIMESTAMP("SSAO Interleave");
		ssao.interleave_push_constant.inv_sharpness = 1.0 - p_settings.sharpness;
		ssao.interleave_push_constant.pixel_size[0] = 1.0 / p_settings.full_screen_size.x;
		ssao.interleave_push_constant.pixel_size[1] = 1.0 / p_settings.full_screen_size.y;
		ssao.interleave_push_constant.size_modifier = uint32_t(ssao_half_size ? 4 : (2 - k_is_frambuffer_same_size));

		int interleave_pipeline = SSAO_INTERLEAVE_HALF;
		if (ssao_quality == RS::ENV_SSAO_QUALITY_LOW) {
			interleave_pipeline = SSAO_INTERLEAVE;
		} else if (ssao_quality >= RS::ENV_SSAO_QUALITY_MEDIUM) {
			interleave_pipeline = SSAO_INTERLEAVE_SMART;
		}

		bool success = ssao.interleave_shader.version_bind_shader(ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));
		if (!success) {
			return;
		}

		ssao.interleave_shader.version_set_uniform(SsaoInterleaveShaderGLES3::INV_SHARPNESS, ssao.interleave_push_constant.inv_sharpness, ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));
		ssao.interleave_shader.version_set_uniform(SsaoInterleaveShaderGLES3::SIZE_MODIFIER, ssao.interleave_push_constant.size_modifier, ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));
		ssao.interleave_shader.version_set_uniform(SsaoInterleaveShaderGLES3::PIXEL_SIZE, ssao.interleave_push_constant.pixel_size[0], ssao.interleave_push_constant.pixel_size[1], ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));

		if (ssao_quality > RS::ENV_SSAO_QUALITY_VERY_LOW && ssao_blur_passes % 2 == 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, p_buffers.deinterleaved.color);
		} else {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, p_buffers.deinterleaved_pong.color);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, p_buffers.final.fbo);
		glViewport(0, 0, p_buffers.final.size.x, p_buffers.final.size.y);

		_draw_screen_triangle();
	}

	/* FOURTH PASS */
	// Interleave buffers
	// back to full size
	{
		// RENDER_TIMESTAMP("SSAO Interleave");
		ssao.interleave_push_constant.inv_sharpness = 1.0 - p_settings.sharpness;
		ssao.interleave_push_constant.pixel_size[0] = 1.0 / p_settings.full_screen_size.x;
		ssao.interleave_push_constant.pixel_size[1] = 1.0 / p_settings.full_screen_size.y;
		ssao.interleave_push_constant.size_modifier = uint32_t(ssao_half_size ? 4 : (2 - k_is_frambuffer_same_size));

		int interleave_pipeline = SSAO_INTERLEAVE_HALF;
		if (ssao_quality == RS::ENV_SSAO_QUALITY_LOW) {
			interleave_pipeline = SSAO_INTERLEAVE;
		} else if (ssao_quality >= RS::ENV_SSAO_QUALITY_MEDIUM) {
			interleave_pipeline = SSAO_INTERLEAVE_SMART;
		}

		bool success = ssao.interleave_shader.version_bind_shader(ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));
		if (!success) {
			return;
		}

		ssao.interleave_shader.version_set_uniform(SsaoInterleaveShaderGLES3::INV_SHARPNESS, ssao.interleave_push_constant.inv_sharpness, ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));
		ssao.interleave_shader.version_set_uniform(SsaoInterleaveShaderGLES3::SIZE_MODIFIER, ssao.interleave_push_constant.size_modifier, ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));
		ssao.interleave_shader.version_set_uniform(SsaoInterleaveShaderGLES3::PIXEL_SIZE, ssao.interleave_push_constant.pixel_size[0], ssao.interleave_push_constant.pixel_size[1], ssao.interleave_shader_version, static_cast<SsaoInterleaveShaderGLES3::ShaderVariant>(interleave_pipeline - SSAO_INTERLEAVE));

		if (ssao_quality > RS::ENV_SSAO_QUALITY_VERY_LOW && ssao_blur_passes % 2 == 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, p_buffers.deinterleaved.color);
		} else {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, p_buffers.deinterleaved_pong.color);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, p_buffers.final.fbo);
		glViewport(0, 0, p_buffers.final.size.x, p_buffers.final.size.y);

		_draw_screen_triangle();
	}

	///*Recovery states*/
	{
		if (blend_enabled) {
			glEnable(GL_BLEND);
		}
		if (depth_test_enabled) {
			glEnable(GL_DEPTH_TEST);
		}
		glDepthMask(GL_TRUE);

		glUseProgram(0);
		for (int i = 0; i < k_bound_texture_size; i++) {
			glActiveTexture(GL_TEXTURE0 + i);
			glBindTexture(GL_TEXTURE_2D, bound_texture[i]);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_origin);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
		glViewport(viewport_origin[0], viewport_origin[1], viewport_origin[2], viewport_origin[3]);
	}
}

void SSAO::ssao_set_quality(RenderingServer::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ssao_quality = p_quality;
	if (p_half_size) {
		WARN_PRINT("Half size SSAO is not supported in WebGL.");
	}
	// ssao_half_size = p_half_size;
	ssao_adaptive_target = p_adaptive_target;
	ssao_blur_passes = p_blur_passes;
	ssao_fadeout_from = p_fadeout_from;
	ssao_fadeout_to = p_fadeout_to;
}

RS::EnvironmentSSAOQuality SSAO::ssao_get_quality() const {
	return ssao_quality;
}

bool SSAO::is_ssao_half_size() const {
	return ssao_half_size;
}

#endif // GLES3_ENABLED
