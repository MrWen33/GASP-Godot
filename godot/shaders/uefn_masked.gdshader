// M_UEFN_Mannequin_Masked: BaseColor = sum over the five colour-ID masks of MF_CustomColorMask outputs (rounded RGB ->
// exclusive R/G/B/C/M/Y/W masks) times the region vector parameters; Normal = T_UEFN_Mannequin_N; default PBR inputs.
shader_type spatial;
render_mode cull_back;

uniform sampler2D mask_larm : hint_default_black, filter_linear_mipmap;
uniform sampler2D mask_lleg : hint_default_black, filter_linear_mipmap;
uniform sampler2D mask_rarm : hint_default_black, filter_linear_mipmap;
uniform sampler2D mask_rleg : hint_default_black, filter_linear_mipmap;
uniform sampler2D mask_torso : hint_default_black, filter_linear_mipmap;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap;
// per mask: colours for R, G, B, C, M, Y, W (linear)
uniform vec3 larm[7];
uniform vec3 lleg[7];
uniform vec3 rarm[7];
uniform vec3 rleg[7];
uniform vec3 torso[7];

vec3 decode(sampler2D m, vec2 uv, vec3 cols[7]) {
	vec3 id = round(clamp(texture(m, uv).rgb, 0.0, 1.0));
	float rg = id.r * id.g, rb = id.r * id.b, gb = id.g * id.b;
	float w = rg * id.b;
	float y = rg * (1.0 - w), mg = rb * (1.0 - w), c = gb * (1.0 - w);
	float any2 = max(gb, max(rb, rg));
	float r = id.r * (1.0 - any2), g = id.g * (1.0 - any2), b = id.b * (1.0 - any2);
	return cols[0] * r + cols[1] * g + cols[2] * b + cols[3] * c + cols[4] * mg + cols[5] * y + cols[6] * w;
}

void fragment() {
	vec3 col = decode(mask_larm, UV, larm) + decode(mask_lleg, UV, lleg) + decode(mask_rarm, UV, rarm)
		+ decode(mask_rleg, UV, rleg) + decode(mask_torso, UV, torso);
	ALBEDO = col;
	ROUGHNESS = 0.5;
	METALLIC = 0.0;
	SPECULAR = 0.5;
	vec3 n = texture(normal_tex, UV).rgb;
	n.g = 1.0 - n.g; // DirectX normal map
	NORMAL_MAP = n;
}
