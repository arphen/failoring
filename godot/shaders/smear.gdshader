// Lipstick smeared on a mirror (studio_atelier.gd): waxy red swipes, a half-wiped heart, fingerprints. A quad laid over the
// glass; the alpha is the smear, the rest of the quad is clear.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec3 colour : source_color = vec3(0.62, 0.05, 0.1);
uniform float seed = 0.0;

float h11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

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

float seg(vec2 p, vec2 a, vec2 b, float w) {
	vec2 pa = p - a;
	vec2 ba = b - a;
	float t = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	return 1.0 - smoothstep(w * 0.6, w, length(pa - ba * t));
}

varying vec2 v_uv;

void vertex() {
	v_uv = UV;
}

void fragment() {
	vec2 p = (v_uv - 0.5) * vec2(1.0, 1.4);
	float m = 0.0;
	// a few swipes at slants, wider and streakier along their length
	for (int i = 0; i < 5; i++) {
		float fi = float(i) + seed;
		vec2 a = vec2(h11(fi * 3.1) - 0.5, h11(fi * 5.7) - 0.5) * 0.8;
		vec2 b = a + vec2(h11(fi * 7.3) - 0.5, h11(fi * 11.9) - 0.5) * 0.5;
		float streak = 0.55 + 0.45 * vn(vec2(dot(p - a, normalize(b - a)) * 40.0, fi * 9.0));
		m = max(m, seg(p, a, b, 0.012 + 0.012 * h11(fi * 13.0)) * streak);
	}
	// a heart, drawn with a wavering line and half wiped
	vec2 hp = (p - vec2(0.12, 0.22)) * 5.0;
	float hr = abs(hp.x) + hp.y - sqrt(max(abs(hp.x), 0.0)) * 1.1;
	float heart = 1.0 - smoothstep(0.0, 0.07, abs(length(vec2(abs(hp.x), hp.y - 0.6 * sqrt(max(abs(hp.x), 0.001)))) - 0.9));
	m = max(m, heart * 0.9 * smoothstep(-0.2, 0.2, vn(p * 12.0) - 0.4));
	// fingerprints: faint ridged ovals
	vec2 fp = (p - vec2(-0.2, -0.28)) * vec2(7.0, 5.0);
	float fprint = (1.0 - smoothstep(0.7, 1.0, length(fp))) * smoothstep(0.35, 0.65, 0.5 + 0.5 * sin(length(fp) * 26.0 + vn(fp * 3.0) * 3.0));
	m = max(m, fprint * 0.45);
	m *= 0.6 + 0.4 * vn(p * 60.0);
	ALBEDO = colour;
	ALPHA = clamp(m, 0.0, 0.92);
	ROUGHNESS = 0.32;
	SPECULAR = 0.7;
}
