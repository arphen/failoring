// Skin for the mannequin (look.gd body_material; attributes from skin.gd). Look only.
//
// Inputs: CUSTOM0 = (rest x, y, z, cavity); COLOR = (redness, thin, areola, nails); UV = MakeHuman's texture
// coordinates (region masks, photo skins). The rest position is the
// MakeHuman base mesh before morphs and posing, so everything drawn from it (pores, mottling, lips, brows, hair) stays
// on the same patch of skin while the body walks and on every preset.
//
// What makes it read as skin rather than plastic, in order of effect:
//   1. light scattering under the surface: a red-shifted soft terminator (per-channel wrapped diffuse) and light
//      coming through thin parts (ears, fingers) seen against a light; Forward+ also blurs it (SSS_STRENGTH, high)
//   2. a broken highlight: pores and fine bumps (procedural, in rest space, faded below a pixel), two specular lobes
//      (an oily sharp one and a broad one) and a roughness that varies across the face and body
//   3. colour that is not uniform: mottling, redder knees, elbows, knuckles, ears, nose and cheeks, darker creases
// Face features are drawn from rest-space landmarks (skin.gd), no texture.
shader_type spatial;
render_mode cull_back, depth_draw_opaque, diffuse_burley, specular_schlick_ggx;

uniform vec3 tone : source_color = vec3(0.84, 0.66, 0.56);       // skin albedo
uniform vec3 under : source_color = vec3(0.86, 0.38, 0.30);      // blood under the skin (redness tint)
uniform vec3 lip_tint : source_color = vec3(0.70, 0.36, 0.36);   // lips (multiplied with the tone)
uniform vec3 brow_col : source_color = vec3(0.10, 0.07, 0.05);
uniform vec3 hair_col : source_color = vec3(0.09, 0.065, 0.05);
uniform float hair = 1.0;          // scalp hair: 0 bald, 1 short crop
uniform float brows = 1.0;
uniform float moles = 1.0;         // 0 none
uniform float melanin = 0.5;       // 0 very light .. 1 very dark: how much the redness shows
uniform float wrap = 0.0;          // soft terminator (0: the shadow map decides where the light ends)
uniform vec3 glow_col = vec3(0.06, 0.014, 0.006); // reddening on the lit side of the terminator, where light enters at a
                                                 // grazing angle and leaves after a longer path
uniform float detail = 1.0;        // pores and bumps (0 smooth)
uniform float sss = 0.0;           // Forward+ screen-space scattering (look.gd sets it on high)
uniform float specular_amount = 0.27;  // F0 about 0.026 (skin's 1.4 refractive index)
uniform float shrink = 0.0;        // m: the skin is drawn this far inside the collision surface (look.gd body_material)

// The idle layer (idle.gd, body_loader.gd): the body skinned once more with small bone motions, a breath and a blink.
uniform sampler2D idle_xf : filter_nearest, hint_default_black;   // 24 x 3 RGBA32F: row r of each bone's 3 x 4 matrix
uniform float idle_on = 0.0;
uniform float breath = 0.0;        // 0 .. 1 (times the depth): the ribcage and belly swell along their horizontal normals
uniform vec2 breath_amp = vec2(0.0045, 0.0065);   // m: ribcage, belly
uniform float blink = 0.0;         // 0 open .. 1 closed: the lids swing down round the eyeball
uniform mat3 lid_basis = mat3(1.0);   // rest axes -> drawn axes (head rotation and scale), set by eyes.gd
// the lids at rest (a face's expression, face.json "lids"): x the upper lid lowered, y the lower lid raised, in radians of
// the same swing; a blink closes from there
uniform vec2 lid_rest = vec2(0.0);

uniform int debug_view = 0;        // 1 cavity, 2 redness, 3 thin, 4 areola and nails, 5 face masks, 6 region texture

// MakeHuman UV space: MPFB2's region masks (R lips, G nails, B eyelids, A inside of the mouth; assets/skin/regions.png)
// and, when one is installed, a photographic MakeHuman skin (assets/skin/<name>.png, tools/fetch_makehuman_skins.sh)
uniform sampler2D regions : hint_default_black, filter_linear_mipmap;
uniform float use_regions = 0.0;
uniform sampler2D photo : source_color, hint_default_white, filter_linear_mipmap_anisotropic;
uniform float use_photo = 0.0;
uniform vec3 photo_tint = vec3(1.0);   // multiplies the photo (a light tint towards the chosen tone)
// a face likeness (face.gd): the wearer's face skin in the same UV layout, alpha = where it covers; face_gain matches its
// exposure to the body's tone. Where it covers, the drawn brows and scalp hair give way to the photo's own.
uniform sampler2D face_tex : source_color, hint_default_transparent, filter_linear_mipmap_anisotropic;
uniform float use_face = 0.0;
uniform vec3 face_gain = vec3(1.0);

uniform vec3 mouth = vec3(0.0, 0.6058, 0.1375);
uniform float mouth_w = 0.0225;
uniform vec3 nose_tip = vec3(0.0, 0.637, 0.1518);
uniform vec3 eye_c = vec3(0.0284, 0.6677, 0.1098);   // left eye centre (the right one is mirrored)
uniform vec3 scalp_c = vec3(0.0, 0.672, 0.030);

varying vec3 v_rest;
varying float v_cav;
varying vec4 v_mask;
varying float v_thin;
varying float v_oil;

const float PI_ = 3.14159265;

float hash3(vec3 p) {
	p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// value noise in [-1, 1] with its gradient (xyz)
vec4 vnoise(vec3 x) {
	vec3 i = floor(x);
	vec3 w = fract(x);
	vec3 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);
	vec3 du = 30.0 * w * w * (w * (w - 2.0) + 1.0);
	float a = hash3(i);
	float b = hash3(i + vec3(1.0, 0.0, 0.0));
	float c = hash3(i + vec3(0.0, 1.0, 0.0));
	float d = hash3(i + vec3(1.0, 1.0, 0.0));
	float e = hash3(i + vec3(0.0, 0.0, 1.0));
	float f = hash3(i + vec3(1.0, 0.0, 1.0));
	float g = hash3(i + vec3(0.0, 1.0, 1.0));
	float h = hash3(i + vec3(1.0, 1.0, 1.0));
	float k1 = b - a;
	float k2 = c - a;
	float k3 = e - a;
	float k4 = a - b - c + d;
	float k5 = a - c - e + g;
	float k6 = a - b - e + f;
	float k7 = -a + b + c - d + e - f - g + h;
	float v = a + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z;
	vec3 dv = du * vec3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
			k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
			k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
	return vec4(2.0 * v - 1.0, 2.0 * dv);
}

float fade_for(float px, float freq) {
	// px: metres per pixel; a feature finer than about two pixels is faded out (it would only shimmer)
	return 1.0 - smoothstep(0.22, 0.5, px * freq);
}

// the lids: rotate the vertices of an eyelid about the eye's centre, in rest space (the displacement is carried to the drawn
// pose by lid_basis). The upper lid margin drops about 38 degrees to meet the lower one, which rises a little; the skin
// above and below follows less and less.
vec3 lid_shift(vec3 rest) {
	vec3 ec = vec3(sign(rest.x) * eye_c.x, eye_c.y, eye_c.z);
	vec3 d = rest - ec;
	if (abs(d.x) > 0.02 || abs(d.y) > 0.03 || d.z < -0.004 || d.z > 0.03) {
		return vec3(0.0);
	}
	float taper = 1.0 - smoothstep(0.008, 0.0145, abs(d.x));
	float across = clamp(abs(d.x) / 0.0132, 0.0, 1.0);
	float shape = sqrt(max(1.0 - across * across, 0.0));
	float up_m = 0.0058 * shape;
	float lo_m = -0.0035 * shape;
	float wu = d.y > 0.0 ? (1.0 - smoothstep(0.0, 0.011, max(d.y - up_m, 0.0))) * taper : 0.0;
	float wl = d.y <= 0.0 ? (1.0 - smoothstep(0.0, 0.008, max(lo_m - d.y, 0.0))) * taper : 0.0;
	float a = blink * (0.66 * wu - 0.14 * wl) + (1.0 - blink) * (lid_rest.x * wu - lid_rest.y * wl);
	float ca = cos(a);
	float sa = sin(a);
	vec3 r = vec3(d.x, d.y * ca - d.z * sa, d.y * sa + d.z * ca);
	return lid_basis * (r - d);
}

void vertex() {
	vec3 p = VERTEX;
	vec3 n = NORMAL;
	if (breath > 0.001) {
		vec3 nh = vec3(n.x, 0.0, n.z);
		float nl = length(nh);
		nh = nl > 1e-4 ? nh / nl : vec3(0.0);
		p += nh * breath * (CUSTOM3.x * breath_amp.x + CUSTOM3.y * breath_amp.y);
	}
	if (blink > 0.001 || lid_rest != vec2(0.0)) {
		p += lid_shift(CUSTOM0.xyz);
	}
	if (idle_on > 0.5) {
		vec3 pp = vec3(0.0);
		vec3 nn = vec3(0.0);
		for (int k = 0; k < 4; k++) {
			float wk = CUSTOM2[k];
			if (wk > 0.0) {
				int bi = int(CUSTOM1[k] + 0.5);
				vec4 r0 = texelFetch(idle_xf, ivec2(bi, 0), 0);
				vec4 r1 = texelFetch(idle_xf, ivec2(bi, 1), 0);
				vec4 r2 = texelFetch(idle_xf, ivec2(bi, 2), 0);
				pp += wk * vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);
				nn += wk * vec3(dot(r0.xyz, n), dot(r1.xyz, n), dot(r2.xyz, n));
			}
		}
		p = pp;
		n = normalize(nn);
	}
	VERTEX = p;
	NORMAL = n;
	VERTEX -= NORMAL * shrink;
	v_rest = CUSTOM0.xyz;
	v_cav = CUSTOM0.w;
	v_mask = COLOR;
	v_thin = COLOR.g;
}

// the mouth's outline seen from the front (the lips and the opening between them), at any depth: 1 inside, 0 outside
float mouth_outline(vec3 p, float grow) {
	vec3 m = p - mouth;
	float u = abs(m.x) / mouth_w;
	float uu = clamp(u, 0.0, 1.0);
	float top = 0.0125 * pow(max(1.0 - uu * uu, 0.0), 0.75) + grow;
	float bot = -0.0118 * pow(max(1.0 - uu * uu, 0.0), 0.6) - grow;
	float e = 0.0009;
	float in_y = smoothstep(bot - e, bot + e, m.y) * (1.0 - smoothstep(top - e, top + e, m.y));
	return in_y * (1.0 - smoothstep(1.0, 1.15, u));
}

// lips: 1 on the lips, 0 off them; m = rest position relative to the mouth centre
float lips_mask(vec3 p) {
	vec3 m = p - mouth;
	float u = abs(m.x) / mouth_w;
	if (u > 1.25 || p.z < mouth.z - 0.012 || abs(m.y) > 0.02) {
		return 0.0;
	}
	float uu = clamp(u, 0.0, 1.0);
	float top = 0.0125 * pow(max(1.0 - uu * uu, 0.0), 0.75) - 0.0016 * exp(-pow(uu / 0.16, 2.0));  // cupid's bow
	float bot = -0.0118 * pow(max(1.0 - uu * uu, 0.0), 0.6);
	float e = 0.0009;
	float in_y = smoothstep(bot - e, bot + e, m.y) * (1.0 - smoothstep(top - e, top + e, m.y));
	return in_y * (1.0 - smoothstep(0.96, 1.04, u));
}

// eyebrow strength (0..1) and the direction along the brow, from the rest position
float brow_mask(vec3 p, out float along, out float across) {
	float ax = abs(p.x);
	along = (ax - 0.008) / 0.049;            // 0 at the inner end, 1 at the tail
	float t = clamp(along, 0.0, 1.0);
	// the brow line: rises from the inner end, arches at about 60 %, drops to the tail
	float yc = eye_c.y + 0.0135 + 0.0085 * sin(t * 2.6) - 0.0065 * t * t;
	float half_w = mix(0.0031, 0.0011, t * t);   // full inner end, thin tail
	across = (p.y - yc) / half_w;
	float inside = (1.0 - smoothstep(0.7, 1.15, abs(across)));
	float ends = smoothstep(-0.04, 0.05, along) * (1.0 - smoothstep(0.92, 1.05, along));
	float front = smoothstep(eye_c.z - 0.006, eye_c.z + 0.008, p.z);
	return inside * ends * front * step(0.55, p.y);
}

// scalp hair: 1 above the hairline, 0 on the face, ears and neck
float hair_mask(vec3 p, out vec3 dir) {
	vec3 d = p - scalp_c;
	float r = length(d);
	dir = d / max(r, 1e-5);
	float az = atan(dir.x, dir.z);                 // 0 at the front, +-pi at the back
	float el = asin(clamp(dir.y, -1.0, 1.0));      // elevation
	// the hairline's elevation as a function of azimuth: high on the forehead, receding at the temples, low behind
	float c1 = cos(az);
	float c2 = cos(2.0 * az);
	float line = radians(-3.5 + 40.0 * c1 - 5.5 * c2) - radians(6.0) * exp(-pow((abs(az) - 0.7) / 0.22, 2.0));
	float soft = radians(3.0) + radians(2.0) * max(c1, 0.0);
	float m = smoothstep(line - soft, line + soft, el);
	// keep the ears and the skin around them clear
	vec3 ear = vec3(sign(p.x) * 0.068, 0.633, 0.018);
	vec3 de = (p - ear) / vec3(0.03, 0.04, 0.034);
	m *= smoothstep(0.85, 1.15, length(de));
	// only the head, above the nape
	m *= smoothstep(0.585, 0.60, p.y);
	return m;
}

void fragment() {
	vec3 rp = v_rest;
	float px = max(length(fwidth(rp)), 1e-7);
	float cav = v_cav;
	float crease = smoothstep(0.12, 0.6, cav);   // only real creases (ear folds, eyelids, nostrils, between fingers)
	float face = step(0.53, rp.y) * (1.0 - step(0.115, abs(rp.x)));

	// ---------------- height field (metres) and its rest-space gradient
	vec4 n_pore = vnoise(rp * 1450.0);
	vec4 n_pore2 = vnoise(rp * 2300.0 + vec3(5.1, 1.7, 9.3));
	vec4 n_fine = vnoise(rp * 520.0 + vec3(17.3, 3.1, 7.7));
	vec4 n_mid = vnoise(rp * 75.0 + vec3(2.2, 8.4, 1.3));
	vec4 n_low = vnoise(rp * 16.0 + vec3(9.1, 4.2, 6.6));
	float f_pore = fade_for(px, 1450.0) * detail;
	float f_pore2 = fade_for(px, 2300.0) * detail;
	float f_fine = fade_for(px, 520.0) * detail;
	float f_mid = fade_for(px, 75.0) * detail;
	// pores are pits where the noise is high: h = -a (n - t)^2 above t
	float t = 0.35;
	float pv = max(n_pore.x - t, 0.0);
	float pv2 = max(n_pore2.x - t, 0.0);
	float face_pores = 1.0 + 0.4 * face;   // deeper pores on the face
	vec3 grad = vec3(0.0);
	float a_pore = 3.6e-5 * face_pores * f_pore;
	grad += -a_pore * 2.0 * pv / 0.42 * n_pore.yzw * 1450.0;
	float a_pore2 = 1.6e-5 * f_pore2;
	grad += -a_pore2 * 2.0 * pv2 / 0.42 * n_pore2.yzw * 2300.0;
	grad += 1.2e-5 * f_fine * n_fine.yzw * 520.0;
	// soft unevenness of the flesh, strongest on the thighs, seat and hips (rest y from the knee to the waist)
	float fleshy = 1.0 + 0.6 * smoothstep(-0.42, -0.25, rp.y) * (1.0 - smoothstep(0.0, 0.1, rp.y));
	grad += 2.0e-4 * fleshy * f_mid * n_mid.yzw * 75.0;
	grad += 6.0e-4 * detail * n_low.yzw * 16.0;
	// lips: fine vertical lines
	vec4 reg = texture(regions, UV) * use_regions;
	// the painted lip mask follows the real lip outline; the drawn one is the fallback without the texture
	float lip = use_regions > 0.5 ? smoothstep(0.15, 0.6, reg.r) : lips_mask(rp);
	if (lip > 0.0) {
		vec4 n_lip = vnoise(vec3(rp.x * 1800.0, rp.y * 180.0, rp.z * 400.0));
		grad += lip * 4.5e-5 * fade_for(px, 1800.0) * vec3(n_lip.y * 1800.0, n_lip.z * 180.0, n_lip.w * 400.0);
	}

	// surface-gradient bump: the rest-space gradient is carried to the screen by the derivatives of the rest position
	vec3 N = normalize(NORMAL);
	vec3 dpx = dFdx(VERTEX);
	vec3 dpy = dFdy(VERTEX);
	float dhx = dot(grad, dFdx(rp));
	float dhy = dot(grad, dFdy(rp));
	vec3 r1 = cross(dpy, N);
	vec3 r2 = cross(N, dpx);
	float det = dot(dpx, r1);
	// Mikkelsen's form is normalize(|det| N - grad); dividing by |det| keeps it scale free
	vec3 sg = sign(det) * (dhx * r1 + dhy * r2) / max(abs(det), 1e-12);
	NORMAL = normalize(N - sg);

	// ---------------- colour
	vec3 alb = tone;
	vec3 photo_c = texture(photo, UV).rgb * photo_tint;
	vec4 face_c = texture(face_tex, UV);
	float face_a = face_c.a * use_face;
	// mottling: large soft patches that lean yellow or red, and a finer unevenness
	float mot = n_low.x * 0.6 + vnoise(rp * 41.0 + 3.3).x * 0.4;
	alb *= 1.0 + 0.045 * mot * vec3(1.0, 0.75, 0.6);
	alb = mix(alb, alb * vec3(1.03, 0.97, 0.88), 0.35 * clamp(vnoise(rp * 6.0 + 1.7).x, 0.0, 1.0));
	// redness: mask from the vertices (knees, elbows, hands, feet, ears) plus the face (cheeks, nose, lids)
	float red = v_mask.r;
	if (face > 0.5) {
		vec3 q = rp;
		vec3 cheek = vec3(sign(q.x) * 0.040, 0.627, 0.118);
		red = max(red, 0.42 * exp(-dot((q - cheek) / vec3(0.018, 0.014, 0.03), (q - cheek) / vec3(0.018, 0.014, 0.03))));
		red = max(red, 0.5 * exp(-pow(distance(q, nose_tip) / 0.011, 2.0)));
		vec3 ec = vec3(sign(q.x) * eye_c.x, eye_c.y, eye_c.z);
		float de = distance(q, ec);
		red = max(red, 0.65 * (1.0 - smoothstep(0.0115, 0.0135, de)) * step(ec.z + 0.002, q.z));  // lid margins
		red = max(red, 0.25 * exp(-pow((de - 0.016) / 0.004, 2.0)) * step(ec.z, q.z));           // around the eyes
	}
	red = clamp(red + 0.2 * crease, 0.0, 1.0);
	vec3 flushed = alb * mix(vec3(1.0), under / max(max(under.r, under.g), under.b), 0.55);
	alb = mix(alb, flushed, red * mix(0.75, 0.3, melanin));
	// a few small moles and freckles: rare peaks of a fine noise, only where a coarse noise allows them (they cluster)
	float mole = smoothstep(0.84, 0.92, vnoise(rp * 240.0 + vec3(11.0, 3.0, 7.0)).x)
			* smoothstep(0.25, 0.65, vnoise(rp * 7.0 + vec3(5.0, 1.0, 2.0)).x) * fade_for(px, 240.0) * moles * (1.0 - 0.6 * face);
	alb = mix(alb, alb * vec3(0.58, 0.45, 0.40), mole * 0.85);
	// areola
	alb = mix(alb, alb * vec3(0.80, 0.58, 0.53), v_mask.b * 0.85);
	// creases a little darker
	alb *= 1.0 - 0.12 * crease;
	// lips, and the inside of the slightly parted mouth (well behind the lips' front)
	float lmu = abs(rp.x - mouth.x) / mouth_w;
	float ly = clamp(abs(rp.y - mouth.y) / 0.009, 0.0, 1.0);
	vec3 lip_c = tone * lip_tint * 1.15;
	lip_c *= mix(0.68, 1.12, smoothstep(0.0, 0.75, ly));                  // darker at the lip line, lighter in the middle
	lip_c *= mix(0.78, 1.0, 1.0 - smoothstep(0.55, 1.0, lmu));            // and at the corners
	lip_c = mix(lip_c, lip_c * vec3(1.05, 0.9, 0.95), 0.4 * clamp(vnoise(rp * 300.0).x * 0.5 + 0.5, 0.0, 1.0));   // a little unevenness
	alb = mix(alb, lip_c, lip * 0.88 * (1.0 - use_face));   // a face likeness has the photo's own lips
	// how far behind the lips' front a point lies: the lips curl in towards the mouth line and darken (wet inner lip),
	// and the mouth behind them is dark
	float mu = abs(rp.x - mouth.x) / mouth_w;
	float depth = (0.1445 - 0.0125 * mu * mu) - rp.z;
	float near_line = step(mu, 1.05) * (1.0 - smoothstep(0.004, 0.008, abs(rp.y - mouth.y))) * step(mouth.z - 0.03, rp.z);
	// the mouth line: the lips meet at the bottom of a groove 5 to 8 mm behind their fronts. The groove is deep red and
	// shut off from the light (its ambient occlusion also takes the direct light), so the lips read as in contact, with a
	// soft dark line between them, and never as a slot or a pale band
	// (a face likeness closes the mouth: its inner lips are drawn up to the mouth line, and they still carry the
	// cavity's and the teeth's rest positions; there the groove is only the soft line where closed lips meet)
	float closed = 1.0 - use_face;
	float groove = near_line * smoothstep(0.0012, 0.0042, depth) * (1.0 - 0.7 * use_face);
	alb = mix(alb, alb * vec3(0.34, 0.13, 0.13), groove);
	// the inside of the mouth is a real cavity behind the lips: moist pink-red at the lips, deeper red further in, with a
	// gradient of occlusion, never a black hole
	float in_depth = (mouth.z + 0.0035) - rp.z;
	float inside_m = mouth_outline(rp, 0.0015) * smoothstep(0.0012, 0.0045, in_depth) * closed;
	vec3 cav_col = tone * mix(vec3(0.62, 0.26, 0.24), vec3(0.16, 0.05, 0.05), smoothstep(0.004, 0.025, in_depth));
	alb = mix(alb, cav_col, inside_m);
	// the upper teeth, painted on the far wall of the cavity: ivory crowns with soft gaps, a curved lower edge, dim (they sit
	// in the mouth's shadow), dimmer still towards the corners and up at the gums. Only seen through the opening between the
	// lips, and only a hint: they are not geometry.
	{
		vec2 tp = vec2(rp.x - mouth.x, rp.y - mouth.y);
		float arch = 1.0 - smoothstep(0.006, 0.0115, abs(tp.x));
		float edge = -0.0021 + 0.0014 * pow(clamp(abs(tp.x) / 0.0125, 0.0, 1.0), 2.0);
		float above = smoothstep(edge - 0.0006, edge + 0.0006, tp.y) * (1.0 - smoothstep(0.0045, 0.008, tp.y));
		float cw = 0.0024;
		float cx = fract(tp.x / cw + 0.5) - 0.5;
		float crown = 1.0 - smoothstep(0.38, 0.5, abs(cx));
		vec3 ivory = vec3(0.70, 0.62, 0.50) * (0.8 + 0.2 * cos(cx * 3.0)) * mix(1.0, 0.45, smoothstep(0.0, 0.006, tp.y));
		float teeth = arch * above * mix(0.7, 1.0, crown) * smoothstep(0.014, 0.024, in_depth) * inside_m * 0.8;
		alb = mix(alb, ivory * 0.62, teeth);
		inside_m *= 1.0 - 0.5 * teeth;
	}
	groove = max(groove, inside_m * smoothstep(0.002, 0.014, in_depth));
	// nails: pale pink, glossy
	float nail = use_regions > 0.5 ? smoothstep(0.2, 0.7, reg.g) : v_mask.a;
	// eyelids: a little pinker and darker, as the skin there is thin
	alb = mix(alb, alb * vec3(0.92, 0.78, 0.78), smoothstep(0.1, 0.8, reg.b) * 0.6);
	alb = mix(alb, alb * vec3(0.45, 0.2, 0.2), smoothstep(0.2, 0.8, reg.a) * 0.85 * closed);   // the mouth behind the parted lips: shadowed red, not a void
	// lid margins: a dark lash line along the upper margin (strands), a moist pink line along the lower one
	float lash = 0.0;
	float wet_lid = 0.0;
	if (face > 0.5) {
		vec3 dl = rp - vec3(sign(rp.x) * eye_c.x, eye_c.y, eye_c.z);
		float rd = length(dl);
		float band = smoothstep(0.0108, 0.0114, rd) * (1.0 - smoothstep(0.0123, 0.0134, rd)) * step(0.002, dl.z);
		float upper = smoothstep(0.0, 0.003, dl.y);
		float strands = 0.55 + 0.45 * vnoise(vec3(rp.x * 1100.0, rp.y * 300.0, rp.z * 300.0)).x;
		lash = band * upper * strands;
		wet_lid = band * (1.0 - upper);
		alb = mix(alb, vec3(0.025, 0.017, 0.015), clamp(lash * 0.9, 0.0, 1.0));
		alb = mix(alb, alb * vec3(1.1, 0.8, 0.8), wet_lid * 0.6);
	}
	// a photographic skin replaces the drawn colour (it carries its own lips, mottling and redness); creases stay
	alb = mix(alb, photo_c * (1.0 - 0.12 * crease), use_photo);
	alb = mix(alb, face_c.rgb * face_gain * (1.0 - 0.12 * crease), face_a);
	alb = mix(alb, mix(tone, vec3(0.92, 0.78, 0.76), 0.6), nail * 0.8);

	// brows and scalp hair, drawn over the skin
	float hair_m = 0.0;
	float brow_m = 0.0;
	vec3 hdir = vec3(0.0, 1.0, 0.0);
	if (face > 0.5) {
		float along;
		float across;
		brow_m = brow_mask(rp, along, across) * brows;
		if (brow_m > 0.0) {
			// hairs run along the brow (slanting up at the inner end): fine across them, long along them
			float slant = mix(1.2, 0.15, clamp(along, 0.0, 1.0));
			vec4 s = vnoise(vec3(abs(rp.x) * 170.0, (rp.y - slant * abs(rp.x)) * 1500.0, rp.z * 200.0));
			float f = fade_for(px, 1500.0);
			// sparser towards the edges: the outline is made of hairs, not a painted edge
			float core = 1.0 - smoothstep(0.2, 1.0, abs(across));
			brow_m *= clamp(mix(0.75, 0.25 + 1.1 * s.x + 0.35 * core, f), 0.0, 1.0);
		}
		hair_m = hair_mask(rp, hdir) * hair;
		if (hair_m > 0.0) {
			// cropped hair: dense, with a fine grain, thinning out over the last centimetre at the hairline so the
			// scalp shows through there
			vec4 g = vnoise(rp * 2800.0);
			vec4 g2 = vnoise(rp * 600.0 + 4.0);
			float grain = 0.72 + 0.28 * g.x * fade_for(px, 2800.0);
			float thin_out = smoothstep(0.0, 1.0, hair_m) * (0.92 + 0.08 * g2.x);
			hair_m = clamp(thin_out * grain * 1.08, 0.0, 1.0);
		}
	}
	alb = mix(alb, brow_col, clamp(brow_m, 0.0, 1.0) * 0.92 * (1.0 - face_a));
	alb = mix(alb, hair_col, clamp(hair_m, 0.0, 1.0) * (1.0 - face_a));
	ALBEDO = clamp(alb, 0.0, 1.0);
	if (debug_view == 1) {
		ALBEDO = vec3(crease, 0.15, max(-cav, 0.0));
	} else if (debug_view == 2) {
		ALBEDO = vec3(red);
	} else if (debug_view == 3) {
		ALBEDO = vec3(v_thin);
	} else if (debug_view == 4) {
		ALBEDO = vec3(v_mask.b, v_mask.a, 0.0);
	} else if (debug_view == 5) {
		ALBEDO = vec3(lip, brow_m, hair_m);
	} else if (debug_view == 6) {
		ALBEDO = reg.rgb + vec3(reg.a);
	} else if (debug_view == 7) {
		ALBEDO = vec3(groove, near_line, clamp(depth * 100.0, 0.0, 1.0));
	} else if (debug_view == 8) {
		ALBEDO = vec3(clamp((rp.z - 0.13) * 50.0, 0.0, 1.0), clamp((rp.y - mouth.y) * 50.0 + 0.5, 0.0, 1.0), clamp(abs(rp.x) * 20.0, 0.0, 1.0));
	}

	// ---------------- gloss
	float rough = 0.6 + 0.05 * n_mid.x;
	if (face > 0.5) {
		// T-zone: forehead and nose a little oilier
		float tz = exp(-pow(rp.x / 0.022, 2.0)) * smoothstep(0.615, 0.64, rp.y) * (1.0 - smoothstep(0.71, 0.73, rp.y));
		rough -= 0.10 * tz;
	}
	rough = mix(rough, 0.2, lip);                  // wet lips
	rough = mix(rough, 0.16, wet_lid);
	rough = mix(rough, 0.25, nail);
	rough = mix(rough, 0.72, clamp(hair_m + brow_m, 0.0, 1.0));
	rough += 0.10 * crease;
	ROUGHNESS = clamp(rough, 0.05, 1.0);
	v_oil = mix(1.0, 0.35, clamp(hair_m + brow_m, 0.0, 1.0)) * (1.0 - 0.6 * crease);
	v_oil = mix(v_oil, 1.9, clamp(lip + wet_lid, 0.0, 1.0));        // the sharp lobe is what makes lips and lid margins look wet
	SPECULAR = specular_amount * (1.0 - 0.7 * crease) * (1.0 - groove) * (1.0 + 0.5 * lip);
	AO = (1.0 - 0.5 * crease) * (1.0 - 0.9 * groove);
	AO_LIGHT_AFFECT = groove * 0.9;
	// the faint fuzz of skin: a soft sheen at grazing angles, less where the hair is drawn
	RIM = 0.18 * (1.0 - clamp(hair_m + brow_m, 0.0, 1.0)) * (1.0 - groove);
	RIM_TINT = 0.55;
	SSS_STRENGTH = sss * (1.0 - clamp(hair_m, 0.0, 1.0));
	SSS_TRANSMITTANCE_COLOR = vec4(under, 1.0);
	SSS_TRANSMITTANCE_DEPTH = 0.004 + 0.012 * v_thin;
	SSS_TRANSMITTANCE_BOOST = 0.0;
}

float d_ggx(float ndh, float r) {
	float a = r * r;
	float a2 = a * a;
	float d = ndh * ndh * (a2 - 1.0) + 1.0;
	return a2 / (PI_ * d * d);
}

float v_smith(float ndl, float ndv, float r) {
	float a = r * r;
	float gv = ndl * sqrt(ndv * ndv * (1.0 - a) + a);
	float gl = ndv * sqrt(ndl * ndl * (1.0 - a) + a);
	return 0.5 / max(gv + gl, 1e-5);
}

// LIGHT_COLOR carries a factor pi (Godot's convention), so diffuse is divided by pi. Godot multiplies DIFFUSE_LIGHT by
// ALBEDO afterwards; SPECULAR_LIGHT is used as is.
void light() {
	float ndl = dot(NORMAL, LIGHT);
	// scattering: a slightly soft terminator, warmer on its lit side. The red is kept on the lit side: past the
	// terminator the shadow map is unreliable (normal bias), and a red wrap there showed as orange lines along the
	// limbs where a second lamp lifted the shadow.
	float soft = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
	vec3 diff = vec3(soft) + glow_col * exp(-pow((ndl - 0.14) / 0.14, 2.0));
	// thin parts lit from behind glow red
	float back_t = v_thin * clamp(-ndl, 0.0, 1.0) * pow(clamp(dot(VIEW, -LIGHT) * 0.5 + 0.5, 0.0, 1.0), 2.0);
	vec3 through = back_t * vec3(0.9, 0.28, 0.16) * 0.8;
	DIFFUSE_LIGHT += LIGHT_COLOR * ATTENUATION * (diff + through) / PI_;

	// two GGX lobes (d'Eon and Luebke's skin model: a broad and a sharp one), Schlick Fresnel at F0 0.028
	float nl = clamp(ndl, 0.0, 1.0);
	if (nl > 0.0) {
		vec3 H = normalize(LIGHT + VIEW);
		float ndh = clamp(dot(NORMAL, H), 0.0, 1.0);
		float ndv = clamp(dot(NORMAL, VIEW), 1e-4, 1.0);
		float ldh = clamp(dot(LIGHT, H), 0.0, 1.0);
		float f0 = 0.028;
		float F = f0 + (1.0 - f0) * pow(1.0 - ldh, 5.0);
		float r1 = ROUGHNESS;
		float r2 = ROUGHNESS * 0.55;
		float D = 0.88 * d_ggx(ndh, r1) + 0.12 * d_ggx(ndh, r2);
		float V = v_smith(nl, ndv, mix(r1, r2, 0.15));
		SPECULAR_LIGHT += LIGHT_COLOR * ATTENUATION * (F * D * V * nl) * v_oil;
	}
	// peach fuzz: the fine vellus hair catches light at grazing angles on the lit side
	float graze = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 4.0);
	DIFFUSE_LIGHT += LIGHT_COLOR * ATTENUATION * graze * nl * 0.12 / PI_;
}
