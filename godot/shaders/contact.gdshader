// Soft contact shadow under the mannequin's feet (look.gd contact_shadow). A dark radial falloff on a
// flat quad just above the floor. Unshaded, no depth write, no shadow of its own.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled;

uniform float strength = 0.6;

void fragment() {
	vec2 d = (UV - vec2(0.5)) * 2.0;
	float r = length(d);
	ALBEDO = vec3(0.0);
	ALPHA = (1.0 - smoothstep(0.3, 1.0, r)) * strength;
}
