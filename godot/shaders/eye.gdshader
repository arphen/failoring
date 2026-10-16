// Eyeball (eyes.gd): sclera, iris with radial fibres, pupil and a wet cornea, all procedural from the model-space
// position of a unit sphere whose +z is the gaze. The catchlights come from the studio lights and the sky reflections.
shader_type spatial;
render_mode cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec3 iris_out : source_color = vec3(0.24, 0.13, 0.06);   // outer iris
uniform vec3 iris_in : source_color = vec3(0.45, 0.30, 0.13);    // collarette, round the pupil
uniform vec3 sclera : source_color = vec3(0.80, 0.77, 0.74);
uniform float iris_r = 0.61;      // iris radius over the eyeball radius (the limbus, where the cornea meets the eye)
uniform float pupil_r = 0.18;

varying vec3 v_loc;

float h2(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float n2(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(h2(i), h2(i + vec2(1.0, 0.0)), u.x), mix(h2(i + vec2(0.0, 1.0)), h2(i + vec2(1.0, 1.0)), u.x), u.y);
}

void vertex() {
	v_loc = VERTEX;
}

void fragment() {
	vec3 d = normalize(v_loc);
	float rr = d.z > 0.0 ? length(d.xy) : 1.0;     // distance from the gaze axis (sine of the angle)
	float ang = atan(d.y, d.x) / 6.2831853 + 0.5;
	// sclera: whiter at the front, warmer and a little pink towards the corners
	vec3 col = mix(sclera, sclera * vec3(1.0, 0.82, 0.78), smoothstep(0.55, 0.95, rr));
	float vein = n2(vec2(ang * 60.0, rr * 9.0)) * n2(vec2(ang * 23.0, rr * 30.0));
	col = mix(col, vec3(0.75, 0.35, 0.33), smoothstep(0.35, 0.6, vein) * smoothstep(0.62, 0.9, rr) * 0.35);
	// the iris sits behind the cornea: look through it (a ray refracted at the cornea's surface, index 1.376, to the iris
	// plane 0.5 radii behind the apex), so the iris shifts as the eye is seen from the side: depth, not a sticker
	vec2 ip = d.xy;
	if (d.z > 0.0 && rr < iris_r + 0.08) {
		vec3 vm = normalize((inverse(MODEL_MATRIX) * (INV_VIEW_MATRIX * vec4(VIEW, 0.0))).xyz);
		vec3 cn = normalize(v_loc - vec3(0.0, 0.0, 0.31));
		vec3 rf = refract(-vm, cn, 1.0 / 1.376);
		float tt = max(v_loc.z - 0.52, 0.0) / max(-rf.z, 0.25);
		ip = (v_loc + rf * tt).xy;
	}
	float rri = length(ip);
	float angi = atan(ip.y, ip.x) / 6.2831853 + 0.5;
	// iris: radial fibres, a lighter collarette, a dark limbal ring at the edge
	float fib = n2(vec2(angi * 120.0, rri * 6.0)) * 0.6 + n2(vec2(angi * 47.0, rri * 18.0)) * 0.4;
	vec3 ir = mix(iris_in, iris_out, smoothstep(pupil_r + 0.03, iris_r * 0.75, rri));
	ir *= 0.65 + 0.7 * fib;
	ir *= 1.0 - 0.75 * smoothstep(iris_r - 0.09, iris_r, rri);
	float in_iris = 1.0 - smoothstep(iris_r - 0.01, iris_r + 0.015, rr);          // the limbus itself stays where the cornea meets the eye
	col = mix(col, ir, in_iris);
	col = mix(col, vec3(0.008), (1.0 - smoothstep(pupil_r - 0.01, pupil_r + 0.01, rri)) * in_iris);
	// the upper lid shades the top of the eye, and the corners are in the socket's shadow
	float shade = 1.0 - 0.55 * smoothstep(0.0, 0.5, d.y) - 0.3 * smoothstep(0.55, 0.85, abs(d.x));
	ALBEDO = col * shade;
	ROUGHNESS = 0.06;
	SPECULAR = 0.5;
	AO = shade;
}

// Lamps as small crisp catchlights: a sharp GGX lobe on the wet cornea, without the light-size widening that the
// built-in model applies to soft lights (it turned a softbox into a blot over a third of the iris). The softboxes'
// shapes come from the sky reflection.
void light() {
	float ndl = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
	DIFFUSE_LIGHT += LIGHT_COLOR * ATTENUATION * ndl / 3.14159265;
	vec3 H = normalize(LIGHT + VIEW);
	float ndh = clamp(dot(NORMAL, H), 0.0, 1.0);
	float a2 = 0.0016;   // roughness 0.2 squared, squared: a few pixels wide at portrait distance
	float d = ndh * ndh * (a2 - 1.0) + 1.0;
	float D = a2 / (3.14159265 * d * d);
	float F = 0.03 + 0.97 * pow(1.0 - clamp(dot(LIGHT, H), 0.0, 1.0), 5.0);
	SPECULAR_LIGHT += LIGHT_COLOR * ATTENUATION * min(D * F * ndl * 0.25, 40.0);
}
