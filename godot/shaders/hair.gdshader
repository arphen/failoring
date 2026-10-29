// Hair (hair.gd): the cap over the scalp and the locks, one material.
// COLOR: r per-lock shade (0..1), g 1 on locks / 0 on the cap, b layer (0 inner .. 1 outer), a the hairline mask (cap).
// UV: x across a lock (0..1), y along it (0 root .. 1 tip); on the cap, x round the head, y down from the crown.
// TANGENT: the strand direction, smooth along a lock, for the stretched highlight of hair (anisotropy along the strands).
shader_type spatial;
// opaque with cut-out edges (alpha scissor): blended, the studio's screen-space reflections made it read as glass
render_mode cull_disabled;

uniform vec3 hair_col : source_color = vec3(0.31, 0.24, 0.18);

float hash1(float x) { return fract(sin(x * 127.1) * 43758.5453); }
// 1D value noise across the strands: streaks that run the whole length (2D cells cut the locks into squares)
float noise1(float x) {
	float i = floor(x);
	float f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(hash1(i), hash1(i + 1.0), f);
}

void fragment() {
	bool lock = COLOR.g > 0.5;
	float seed = COLOR.r * 91.7;
	float x = lock ? UV.x : UV.x * 6.0;
	// strands: two scales of streaks across the lock, a slow drift along it
	float strands = 0.6 * noise1(x * 55.0 + seed) + 0.4 * noise1(x * 150.0 + seed * 2.3 + UV.y * 3.0);
	// colour: darker at the roots and on the inner layers (shaded by the outer ones), a little lighter at the tips, per
	// lock a little lighter or darker; the streaks vary it by about a quarter
	vec3 c = hair_col * mix(0.85, 1.12, COLOR.r) * mix(0.72, 1.1, strands);
	if (lock) {
		c *= mix(0.8, 1.0, smoothstep(0.0, 0.2, UV.y)) * mix(1.0, 1.12, smoothstep(0.6, 1.0, UV.y));
		c *= mix(0.72, 1.0, COLOR.b);
	} else {
		c *= 0.85;
	}
	ALBEDO = c;
	// a soft highlight: stronger, lit from behind, the strands read as metal
	ROUGHNESS = lock ? 0.74 : 0.8;
	SPECULAR = lock ? 0.06 : 0.04;
	ANISOTROPY = 0.85;
	ANISOTROPY_FLOW = vec2(0.0, 1.0);
	// coverage: each strand ends at its own length; the lock thins at its edges; the cap fades at the hairline
	float a;
	if (lock) {
		float len = 1.0 - 0.22 * noise1(UV.x * 47.0 + seed * 1.7);
		float tip = 1.0 - smoothstep(len - 0.04, len, UV.y);
		// ragged edges: the outline of a lock dissolves into strands (sharp, the locks read as ribbons)
		float edge = 1.0 - smoothstep(0.22, 0.5, abs(UV.x - 0.5) + 0.3 * (strands - 0.5));
		// the root fades in from the cap, strand by strand (it started as a hard horizontal edge)
		float root = smoothstep(0.0, 0.08 + 0.06 * strands, UV.y);
		a = tip * edge * root;
	} else {
		a = smoothstep(0.15, 0.6, COLOR.a + (strands - 0.5) * 0.4);
	}
	ALPHA = a;
	ALPHA_SCISSOR_THRESHOLD = 0.5;
}
