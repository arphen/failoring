// Vignette (mode 0) and film grain (mode 1) over the 3D view: a black or white overlay whose alpha does the work
// (Godot 4.3's Environment has no vignette). Used by look.gd post().
shader_type canvas_item;
render_mode blend_mix;

uniform int mode = 0;
uniform float vignette = 0.38;     // darkening at the corners
uniform float grain = 0.045;       // grain amplitude

void fragment() {
	if (mode == 0) {
		vec2 q = (UV - 0.5) * vec2(1.0, 1.1);
		float d = length(q) * 1.25;
		float v = smoothstep(0.45, 1.05, d);
		COLOR = vec4(0.0, 0.0, 0.02, v * v * vignette + v * 0.04);
	} else {
		float n = fract(sin(dot(FRAGCOORD.xy + vec2(fract(TIME * 7.0) * 91.0, 0.0), vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
		COLOR = vec4(vec3(step(0.0, n)), abs(n) * grain * 2.0);
	}
}
