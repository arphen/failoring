// The tailor's tape of body mode (body_mode.gd): a yellow ribbon with centimetre ticks that unrolls along a measurement.
// UV.x is the length along the tape in metres from its start, UV.y 0..1 across it. `reveal` (m) is how much of it is
// unrolled (the rest is discarded); its end carries the metal tip. `read_at` is where the reading is (m along the
// tape), and `glow` lights the ticks round it up when the measurement is taken.
shader_type spatial;
render_mode cull_disabled;

uniform float reveal = 0.0;
uniform float read_at = 0.0;
uniform float glow = 0.0;
uniform vec3 tape_col : source_color = vec3(0.93, 0.78, 0.28);
uniform vec3 glow_col : source_color = vec3(1.0, 0.72, 0.25);

void fragment() {
	float s = UV.x;
	if (s > reveal) {
		discard;
	}
	vec3 c = tape_col;
	float across = UV.y;
	// ticks along one edge: every cm short, every 5 cm longer, every 10 cm across the tape and red
	float cm = s * 100.0;
	float f = fract(cm);
	float w = fwidth(cm) * 0.75 + 0.04;
	float tick = 1.0 - smoothstep(w * 0.5, w, min(f, 1.0 - f));
	float five = 1.0 - step(0.5, abs(fract(cm / 5.0 + 0.5) - 0.5) * 5.0);
	float ten = 1.0 - step(0.5, abs(fract(cm / 10.0 + 0.5) - 0.5) * 10.0);
	float reach = ten > 0.5 ? 0.82 : (five > 0.5 ? 0.5 : 0.3);
	float on = tick * step(across, reach);
	vec3 ink = ten > 0.5 ? vec3(0.62, 0.06, 0.04) : vec3(0.05, 0.04, 0.03);
	// a thin printed edge line
	float edge = 1.0 - smoothstep(0.03, 0.06, min(across, 1.0 - across));
	c = mix(c, ink, max(on, edge * 0.6));
	// the metal tip at the unrolled end
	float tip = smoothstep(reveal - 0.012, reveal - 0.010, s);
	c = mix(c, vec3(0.72, 0.72, 0.74), tip);
	// the reading: a band round it that lights up
	float near = exp(-pow((s - read_at) / 0.012, 2.0));
	ALBEDO = c;
	ROUGHNESS = mix(0.55, 0.25, tip);
	METALLIC = tip;
	EMISSION = glow_col * glow * (0.35 + 2.2 * near) * (0.4 + 0.6 * on) + c * 0.08;
}
