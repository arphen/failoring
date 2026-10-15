// Fabric shading for the cloth display mesh (look.gd cloth_material sets the uniforms per fabric).
// Inputs from cloth_shade.gd: UV = pattern (u, v) in metres, UV2 = (strain, curvature) in [-1, 1],
// COLOR.rgb = garment albedo (linear), COLOR.a = contact and fold occlusion.
// Weave: a procedural height field in pattern space, turned into a normal perturbation with a
// cotangent frame, faded out where it is finer than a pixel. Sheen and backlight are custom light terms.
shader_type spatial;
render_mode cull_disabled, diffuse_lambert, specular_schlick_ggx;

uniform int weave_kind = 0;      // 0 plain, 1 twill, 2 satin, 3 knit, 4 nap, 5 smooth (leather)
uniform float freq = 300.0;      // weave cycles per metre of pattern
uniform float amp = 0.0001;      // weave height, metres
uniform float rough = 0.7;
uniform float spec = 0.2;
uniform float sheen = 0.3;
uniform float back = 0.15;
uniform float weave_on = 1.0;    // 0 turns the weave normals off (low quality)

varying vec2 v_sc;

const float TWO_PI = 6.2831853;

// Height field of the weave in cycle units q, with its derivative d/dq (vec3: h, dh/dq.x, dh/dq.y).
vec3 weave(vec2 q) {
	if (weave_kind == 0) {          // plain: over-under checker
		float a = sin(TWO_PI * q.x);
		float b = sin(TWO_PI * q.y);
		return vec3(a * b, TWO_PI * cos(TWO_PI * q.x) * b, TWO_PI * a * cos(TWO_PI * q.y));
	} else if (weave_kind == 1) {   // twill: diagonal ribs
		float s = sin(TWO_PI * (q.x + q.y));
		float d = TWO_PI * cos(TWO_PI * (q.x + q.y));
		return vec3(s, d, d);
	} else if (weave_kind == 2) {   // satin: long floats, very low relief
		return vec3(0.5 * sin(TWO_PI * q.x), 0.5 * TWO_PI * cos(TWO_PI * q.x), 0.0);
	} else if (weave_kind == 3) {   // knit: wales as triangle waves along y, stitches along x
		float t = 2.0 * abs(fract(q.y) - 0.5) - 1.0;
		float dt = 2.0 * sign(fract(q.y) - 0.5);
		float c = 0.6 + 0.4 * cos(TWO_PI * q.x);
		return vec3(t * c, t * -0.4 * TWO_PI * sin(TWO_PI * q.x), dt * c);
	} else if (weave_kind == 4) {   // nap: twill with a second, irregular component (wool)
		float s1 = sin(TWO_PI * (q.x + q.y));
		float s2 = sin(TWO_PI * (2.3 * q.x - 1.7 * q.y));
		float d1 = TWO_PI * cos(TWO_PI * (q.x + q.y));
		float d2 = TWO_PI * cos(TWO_PI * (2.3 * q.x - 1.7 * q.y));
		return vec3(0.6 * s1 + 0.4 * s2, 0.6 * d1 + 0.92 * d2, 0.6 * d1 - 0.68 * d2);
	}
	// smooth (leather grain)
	float g1 = sin(TWO_PI * 3.1 * q.x) * sin(TWO_PI * 2.7 * q.y);
	return vec3(g1, TWO_PI * 3.1 * cos(TWO_PI * 3.1 * q.x) * sin(TWO_PI * 2.7 * q.y), TWO_PI * 2.7 * sin(TWO_PI * 3.1 * q.x) * cos(TWO_PI * 2.7 * q.y));
}

void vertex() {
	v_sc = UV2;
}

void fragment() {
	// weave in cycle units, faded when a cycle is under about two pixels
	vec2 qf = UV * freq;
	float px = max(length(fwidth(qf)), 1e-6);
	float fade = clamp(1.0 - (px - 0.25) * 4.0, 0.0, 1.0) * weave_on;
	vec3 w = weave(qf);

	// cotangent frame from screen derivatives of view-space position and pattern uv
	vec3 N = NORMAL / max(length(NORMAL), 1e-6);
	vec3 dp1 = dFdx(VERTEX);
	vec3 dp2 = dFdy(VERTEX);
	vec2 duv1 = dFdx(UV);
	vec2 duv2 = dFdy(UV);
	vec3 dp2p = cross(dp2, N);
	vec3 dp1p = cross(N, dp1);
	vec3 T = dp2p * duv1.x + dp1p * duv2.x;
	vec3 B = dp2p * duv1.y + dp1p * duv2.y;
	float inv = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-14));
	T *= inv;
	B *= inv;
	// slope of the weave along u and v (dimensionless): amplitude times d/du = freq * d/dq
	float su = amp * freq * w.y * fade;
	float sv = amp * freq * w.z * fade;
	vec3 Np = N - su * T - sv * B;
	NORMAL = Np / max(length(Np), 1e-6);

	ALBEDO = COLOR.rgb;
	// contact and fold occlusion from the vertex alpha; stretched areas a little lighter
	AO = COLOR.a;               // ambient only: AO_LIGHT_AFFECT stays 0 so direct light is not darkened
	ALBEDO *= 1.0 + 0.10 * clamp(v_sc.x, 0.0, 1.0) + 0.04 * clamp(v_sc.y, 0.0, 1.0);
	ROUGHNESS = rough;
	SPECULAR = spec;
	if (!FRONT_FACING) {
		// the inside of a garment reads slightly darker than the outside
		ALBEDO *= 0.8;
	}
}

// Godot multiplies DIFFUSE_LIGHT by ALBEDO after this function, so the diffuse terms here are albedo-free.
// SPECULAR_LIGHT is not albedo-scaled. LIGHT_COLOR carries a factor pi (Godot's convention for physically based light
// functions): the terms are divided by pi so the cloth sits in the same light as the skin and the studio.
void light() {
	float ndl = dot(NORMAL, LIGHT);
	// wrapped diffuse: a soft terminator, as a woven surface scatters light
	float wrap = clamp((ndl + 0.3) / 1.3, 0.0, 1.0);
	// light from behind passes through the thin cloth
	float through = clamp(-ndl, 0.0, 1.0) * back;
	// sheen: fibre fuzz brightens grazing views, scattered like diffuse light
	float graze = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 3.0);
	float fuzz = sheen * graze * 0.5;
	DIFFUSE_LIGHT += LIGHT_COLOR * ATTENUATION * (wrap + through + fuzz) / PI;
	// broad specular lobe, width from the roughness
	vec3 HV = LIGHT + VIEW;
	vec3 H = HV / max(length(HV), 1e-6);
	float shine = mix(60.0, 6.0, rough);
	float lobe = pow(max(dot(NORMAL, H), 0.0), shine);
	// normalised Blinn-Phong ((n + 8) / 8 pi): a narrow lobe is brighter at its peak, as on a satin
	SPECULAR_LIGHT += LIGHT_COLOR * ATTENUATION * spec * lobe * max(ndl, 0.0) * (shine + 8.0) / (8.0 * PI);
}
