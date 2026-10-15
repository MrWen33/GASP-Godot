shader_type spatial;
// Simplified stand-in for the UE character master materials (M_Mannequin, M_Echo_*, Paragon ...):
// base color texture * tint, DirectX normal map, packed occlusion / roughness / metallic channels, optional opacity mask.
render_mode depth_draw_opaque, cull_back;

uniform vec4 tint : source_color = vec4(1.0);
uniform bool use_albedo = false;
uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool use_normal = false;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool use_packed = false;
uniform sampler2D packed_tex : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec4 occlusion_channel = vec4(0.0); // dot() selectors into packed_tex, zero = unused
uniform vec4 roughness_channel = vec4(0.0);
uniform vec4 metallic_channel = vec4(0.0);
uniform float roughness = 0.6;
uniform float metallic = 0.0;
uniform float specular = 0.5;
uniform bool use_mask = false;
uniform sampler2D mask_tex : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec4 mask_channel = vec4(1.0, 0.0, 0.0, 0.0);
uniform float alpha_clip = 0.333;

void fragment() {
	vec4 c = tint;
	if (use_albedo) {
		c *= texture(albedo_tex, UV);
	}
	ALBEDO = c.rgb;
	if (use_normal) {
		vec3 n = texture(normal_tex, UV).rgb;
		NORMAL_MAP = vec3(n.r, 1.0 - n.g, n.b); // UE normal maps are DirectX (green down)
	}
	float r = roughness;
	float m = metallic;
	if (use_packed) {
		vec4 p = texture(packed_tex, UV);
		if (dot(roughness_channel, vec4(1.0)) > 0.0) { r = dot(p, roughness_channel); }
		if (dot(metallic_channel, vec4(1.0)) > 0.0) { m = dot(p, metallic_channel); }
		if (dot(occlusion_channel, vec4(1.0)) > 0.0) { AO = dot(p, occlusion_channel); AO_LIGHT_AFFECT = 1.0; }
	}
	ROUGHNESS = r;
	METALLIC = m;
	SPECULAR = specular;
	if (use_mask) {
		ALPHA = dot(texture(mask_tex, UV), mask_channel);
		ALPHA_SCISSOR_THRESHOLD = alpha_clip;
	}
}
