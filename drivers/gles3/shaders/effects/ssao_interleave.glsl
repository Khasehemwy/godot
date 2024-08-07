#[modes]

mode_smart = #define MODE_SMART
mode_half = #define MODE_HALF

#[vertex]
layout(location = 0) in vec2 vertex_attrib;

void main() {
	gl_Position = vec4(vertex_attrib, 1.0, 1.0);
}

#[fragment]

uniform sampler2DArray source_texture; // texunit:0

uniform float inv_sharpness;
uniform int size_modifier;
uniform vec2 pixel_size;

layout(location = 0) out vec4 frag_color;

vec4 unpack_edges(float p_packed_val) {
	uint packed_val = uint(p_packed_val * 255.5);
	vec4 edgesLRTB;
	edgesLRTB.x = float((packed_val >> uint(6)) & uint(0x03)) / 3.0;
	edgesLRTB.y = float((packed_val >> uint(4)) & uint(0x03)) / 3.0;
	edgesLRTB.z = float((packed_val >> uint(2)) & uint(0x03)) / 3.0;
	edgesLRTB.w = float((packed_val >> uint(0)) & uint(0x03)) / 3.0;

	return clamp(edgesLRTB + inv_sharpness, 0.0, 1.0);
}

void main() {
	ivec2 ssC = ivec2(gl_FragCoord.xy);
	if (any(greaterThanEqual(ssC, ivec2(1.0 / pixel_size)))) { //too large, do nothing
		return;
	}

	#ifdef MODE_SMART
	float ao;
	uvec2 pix_pos = uvec2(gl_FragCoord.xy);
	vec2 uv = (gl_FragCoord.xy + vec2(0.5)) * pixel_size;

	// calculate index in the four deinterleaved source array texture
	int mx = int(pix_pos.x % uint(2));
	int my = int(pix_pos.y % uint(2));
	int index_center = mx + my * 2; // center index
	int index_horizontal = (1 - mx) + my * 2; // neighboring, horizontal
	int index_vertical = mx + (1 - my) * 2; // neighboring, vertical
	int index_diagonal = (1 - mx) + (1 - my) * 2; // diagonal

	vec2 center_val = texelFetch(source_texture, ivec3(pix_pos / uvec2(size_modifier), index_center), 0).xy;

	ao = center_val.x;

	vec4 edgesLRTB = unpack_edges(center_val.y);

	// convert index shifts to sampling offsets
	float fmx = float(mx);
	float fmy = float(my);

	// in case of an edge, push sampling offsets away from the edge (towards pixel center)
	float fmxe = (edgesLRTB.y - edgesLRTB.x);
	float fmye = (edgesLRTB.w - edgesLRTB.z);

	// calculate final sampling offsets and sample using bilinear filter
	vec2 uv_horizontal = (gl_FragCoord.xy + vec2(0.5) + vec2(fmx + fmxe - 0.5, 0.5 - fmy)) * pixel_size;
	float ao_horizontal = textureLod(source_texture, vec3(uv_horizontal, index_horizontal), 0.0).x;
	vec2 uv_vertical = (gl_FragCoord.xy + vec2(0.5) + vec2(0.5 - fmx, fmy - 0.5 + fmye)) * pixel_size;
	float ao_vertical = textureLod(source_texture, vec3(uv_vertical, index_vertical), 0.0).x;
	vec2 uv_diagonal = (gl_FragCoord.xy + vec2(0.5) + vec2(fmx - 0.5 + fmxe, fmy - 0.5 + fmye)) * pixel_size;
	float ao_diagonal = textureLod(source_texture, vec3(uv_diagonal, index_diagonal), 0.0).x;

	// reduce weight for samples near edge - if the edge is on both sides, weight goes to 0
	vec4 blendWeights;
	blendWeights.x = 1.0;
	blendWeights.y = (edgesLRTB.x + edgesLRTB.y) * 0.5;
	blendWeights.z = (edgesLRTB.z + edgesLRTB.w) * 0.5;
	blendWeights.w = (blendWeights.y + blendWeights.z) * 0.5;

	// calculate weighted average
	float blendWeightsSum = dot(blendWeights, vec4(1.0, 1.0, 1.0, 1.0));
	ao = dot(vec4(ao, ao_horizontal, ao_vertical, ao_diagonal), blendWeights) / blendWeightsSum;

	frag_color = vec4(ao);
	#else // !MODE_SMART

	vec2 uv = (gl_FragCoord.xy + vec2(0.5)) * pixel_size;
	#ifdef MODE_HALF
	float a = textureLod(source_texture, vec3(uv, 0.0), 0.0).x;
	float d = textureLod(source_texture, vec3(uv, 3.0), 0.0).x;
	float avg = (a + d) * 0.5;

	#else
	float a = textureLod(source_texture, vec3(uv, 0.0), 0.0).x;
	float b = textureLod(source_texture, vec3(uv, 1.0), 0.0).x;
	float c = textureLod(source_texture, vec3(uv, 2.0), 0.0).x;
	float d = textureLod(source_texture, vec3(uv, 3.0), 0.0).x;
	float avg = (a + b + c + d) * 0.25;

	#endif
	frag_color = vec4(avg);
	#endif
}
