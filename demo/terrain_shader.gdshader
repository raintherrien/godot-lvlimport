shader_type spatial;

uniform sampler2D BlendMap0 : source_color;
uniform sampler2D BlendMap1 : source_color;
uniform sampler2D BlendMap2 : source_color;
uniform sampler2D BlendMap3 : source_color;

uniform sampler2D BlendLayer0 : source_color;
uniform sampler2D BlendLayer1 : source_color;
uniform sampler2D BlendLayer2 : source_color;
uniform sampler2D BlendLayer3 : source_color;
uniform sampler2D BlendLayer4 : source_color;
uniform sampler2D BlendLayer5 : source_color;
uniform sampler2D BlendLayer6 : source_color;
uniform sampler2D BlendLayer7 : source_color;

uniform sampler2D BlendLayer8 : source_color;
uniform sampler2D BlendLayer9 : source_color;
uniform sampler2D BlendLayer10 : source_color;
uniform sampler2D BlendLayer11 : source_color;
uniform sampler2D BlendLayer12 : source_color;
uniform sampler2D BlendLayer13 : source_color;
uniform sampler2D BlendLayer14 : source_color;
uniform sampler2D BlendLayer15 : source_color;

void fragment() {
	vec4 b0 = texture(BlendMap0, UV2).rgba;
	vec4 b1 = texture(BlendMap1, UV2).rgba;
	vec4 b2 = texture(BlendMap2, UV2).rgba;
	vec4 b3 = texture(BlendMap3, UV2).rgba;

	vec3 l0 = texture(BlendLayer0, UV).rgb;
	vec3 l1 = texture(BlendLayer1, UV).rgb;
	vec3 l2 = texture(BlendLayer2, UV).rgb;
	vec3 l3 = texture(BlendLayer3, UV).rgb;
	vec3 l4 = texture(BlendLayer4, UV).rgb;
	vec3 l5 = texture(BlendLayer5, UV).rgb;
	vec3 l6 = texture(BlendLayer6, UV).rgb;
	vec3 l7 = texture(BlendLayer7, UV).rgb;
	vec3 l8 = texture(BlendLayer8, UV).rgb;
	vec3 l9 = texture(BlendLayer9, UV).rgb;
	vec3 l10 = texture(BlendLayer10, UV).rgb;
	vec3 l11 = texture(BlendLayer11, UV).rgb;
	vec3 l12 = texture(BlendLayer12, UV).rgb;
	vec3 l13 = texture(BlendLayer13, UV).rgb;
	vec3 l14 = texture(BlendLayer14, UV).rgb;
	vec3 l15 = texture(BlendLayer15, UV).rgb;

	ALBEDO = l0 * b0.r + l1 * b0.g + l2 * b0.b + l3 * b0.a +
	         l4 * b1.r + l5 * b1.g + l6 * b1.b + l7 * b1.a +
	         l8 * b2.r + l9 * b2.g + l10 * b2.b + l11 * b2.a +
	         l12 * b3.r + l13 * b3.g + l14 * b3.b + l15 * b3.a;

	SPECULAR = 0.0f;
	METALLIC = 0.0f;
}
