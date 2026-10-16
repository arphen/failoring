// The glass of the fitting mirror (mirror.gd): the panel's planar reflection (the scene seen from the reflected eye), a little
// tinted and dimmed as old silvered glass is, with a slight darkening towards the edges and a faint smudge of dust. Without the
// reflection (the panel faces away, or low quality) it is a dark polished glass.
shader_type spatial;
render_mode unshaded, cull_back, depth_draw_opaque;

uniform sampler2D reflection : filter_linear, repeat_disable;
uniform float live = 0.0;
uniform vec3 tint : source_color = vec3(0.86, 0.92, 0.9);
uniform float strength = 0.82;

float h21(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vn(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

void fragment() {
	vec2 uv = vec2(1.0 - UV.x, UV.y);        // a mirror image: the camera behind the glass sees it left to right
	vec3 col = vec3(0.012, 0.014, 0.014);
	if (live > 0.5) {
		col = texture(reflection, uv).rgb * tint * strength;
	}
	vec2 e = min(UV, 1.0 - UV);
	float edge = smoothstep(0.0, 0.05, min(e.x, e.y));
	float dust = 0.5 + 0.5 * vn(UV * vec2(9.0, 14.0)) * vn(UV * 40.0 + 3.0);
	col *= mix(0.55, 1.0, edge) * mix(0.9, 1.0, dust);
	ALBEDO = col;          // unshaded: only ALBEDO is written (EMISSION is ignored), tone mapping still applies
}
