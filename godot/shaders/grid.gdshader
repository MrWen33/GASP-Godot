shader_type spatial;
// Approximation of /Game/LevelPrototyping/Materials/M_Grid (world aligned, three grid levels).
// Sizes are in UE centimetres; world position is converted from Godot metres.

uniform vec4 surface_color : source_color = vec4(0.5, 0.5, 0.5, 1.0);
uniform vec4 grid_color : source_color = vec4(0.2, 0.2, 0.2, 1.0);
uniform vec3 grid_sizes = vec3(100.0, 200.0, 800.0);
uniform float base_specular = 0.05;

varying vec3 world_pos;
varying vec3 world_nrm;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_nrm = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

float grid_line(vec2 p, float size, float width) {
	vec2 g = abs(fract(p / size + 0.5) - 0.5) * size;
	vec2 fw = fwidth(p) * 0.75;
	vec2 l = 1.0 - smoothstep(vec2(width) - fw, vec2(width) + fw, g);
	return max(l.x, l.y);
}

float grid_plane(vec2 p) {
	float a = grid_line(p, grid_sizes.x, 0.6);
	float b = grid_line(p, grid_sizes.y, 1.0);
	float c = grid_line(p, grid_sizes.z, 2.0);
	return max(a * 0.55, max(b * 0.8, c));
}

void fragment() {
	vec3 p = world_pos * 100.0;
	vec3 n = abs(world_nrm);
	vec3 w = pow(n, vec3(8.0));
	w /= (w.x + w.y + w.z);
	float g = grid_plane(p.zy) * w.x + grid_plane(p.xz) * w.y + grid_plane(p.xy) * w.z;
	ALBEDO = mix(surface_color.rgb, grid_color.rgb, clamp(g, 0.0, 1.0));
	ROUGHNESS = 0.9;
	SPECULAR = base_specular * 4.0;
	METALLIC = 0.0;
}
