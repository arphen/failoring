// Procedural PBR surfaces for the tailor's studio (studio.gd, studio_props.gd): floor boards, plaster, wool felt, furniture
// wood, woven cloth, a cutting mat, worn paint and backdrop paper. No texture files: every surface computes albedo,
// roughness and a bump from its position, with variation at several scales, wear (traffic round the platform, edges),
// dust (rougher, paler, in the low corners) and a fade of the fine detail that would shimmer at distance.
shader_type spatial;
render_mode cull_back, diffuse_burley, specular_schlick_ggx;

// kind: 0 floor boards, 1 plaster, 2 wool felt / carpet, 3 furniture wood, 4 woven cloth, 5 cutting mat, 6 worn paint,
//       7 backdrop paper, 8 brushed enamel metal, 9 oriental rug (pattern plane = x, y of the object; dims = its size),
//       10 paper tag with scribbled writing
uniform int kind = 0;
uniform vec3 col_a : source_color = vec3(0.5);
uniform vec3 col_b : source_color = vec3(0.4);
uniform vec3 col_c : source_color = vec3(0.3);
uniform float scale = 1.0;             // pattern scale (kind specific: board size, grain frequency, thread count)
uniform float wear = 0.5;              // 0 new .. 1 old
uniform float dust = 0.5;              // 0 clean .. 1 dusty
uniform float seed = 0.0;
uniform bool use_world = true;         // pattern in world space (room, continuous across meshes) or object space (props)
uniform vec3 grain_axis = vec3(1.0, 0.0, 0.0);
uniform int pattern = 0;               // woven cloth: 0 plain, 1 stripes, 2 check, 3 pinstripe, 4 herringbone
uniform float pat_size = 0.02;         // stripe or check size (m)
uniform float bump = 1.0;
uniform float sheen = 0.25;
uniform vec2 dims = vec2(4.0, 3.0);      // rug size (m)

varying vec3 v_p;
varying vec3 v_n;
varying vec3 v_wp;

void vertex() {
	vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_wp = wp;
	v_p = use_world ? wp : VERTEX;
	v_n = use_world ? normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz) : NORMAL;
}

float hash11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

float hash21(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x), mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

// fractal noise whose octaves fade out once one cycle gets close to a pixel (the footprint is measured in the noise's own,
// possibly stretched, coordinates: a floor seen at a grazing angle aliases in one direction long before the other)
float fbm(vec2 p) {
	float pw = max(fwidth(p.x), fwidth(p.y));
	float s = 0.0;
	float a = 0.5;
	float f = 1.0;
	float norm = 0.0;
	for (int i = 0; i < 6; i++) {
		float vis = 1.0 - smoothstep(0.12, 0.55, f * pw);
		s += a * vis * vnoise(p * f + float(i) * 17.3);
		norm += a * vis;
		f *= 2.03;
		a *= 0.5;
	}
	// the faded octaves leave the mean at 0.5 * the visible weight: keep the mean at 0.5
	return s + 0.5 * (0.984375 - norm);
}

// plane coordinates of the dominant axis of the normal
vec2 proj(vec3 p, vec3 n) {
	vec3 a = abs(n);
	if (a.y >= a.x && a.y >= a.z) {
		return p.xz;
	}
	if (a.x >= a.z) {
		return vec2(p.z, p.y);
	}
	return p.xy;
}

// (along the grain, across it) coordinates of a furniture face
vec2 grain_uv(vec3 p, vec3 n) {
	vec3 ax = normalize(grain_axis);
	vec3 a = abs(n);
	vec3 nd = a.y >= a.x && a.y >= a.z ? vec3(0.0, 1.0, 0.0) : (a.x >= a.z ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0));
	vec3 perp = cross(nd, ax);
	if (dot(perp, perp) < 0.01) {
		perp = vec3(nd.y, nd.z, nd.x);
		return vec2(dot(p, perp), length(p - nd * dot(p, nd)));   // end grain: rings round the axis
	}
	return vec2(dot(p, ax), dot(p, normalize(perp)));
}

// Perturbs the view-space normal by the gradient of a height field (Mikkelsen's bump mapping, no tangents needed)
vec3 bump_normal(vec3 n, vec3 pos, float h, float k) {
	vec3 dpx = dFdx(pos);
	vec3 dpy = dFdy(pos);
	float dhx = dFdx(h);
	float dhy = dFdy(h);
	vec3 r1 = cross(dpy, n);
	vec3 r2 = cross(n, dpx);
	float det = dot(dpx, r1);
	vec3 g = k * sign(det) * (dhx * r1 + dhy * r2);
	// at a grazing angle or across a groove the height changes by more than a pixel can show: limit the tilt to about 30 degrees
	g *= min(1.0, 0.55 * abs(det) / max(length(g), 1e-12));
	return normalize(abs(det) * n - g);
}

void fragment() {
	vec3 n0 = normalize(v_n);
	vec2 p = proj(v_p, n0);
	float pw = max(fwidth(p.x) + fwidth(p.y), 1e-5);   // the pattern plane's pixel footprint (cloth weave and printed lines)
	vec3 alb = col_a;
	float rough = 0.7;
	float spec = 0.5;
	float h = 0.0;
	float ao = 1.0;
	float rim = 0.0;
	float metal = 0.0;
	// how worn this spot is: traffic round the platform and along the runway, plus slow patches
	float patch_w = fbm(v_wp.xz * 0.8 + seed);
	float traffic = exp(-dot(v_wp.xz, v_wp.xz) / 5.0) + 0.7 * smoothstep(0.8, 0.2, abs(v_wp.x)) * step(0.0, v_wp.z) * step(v_wp.z, 3.8);
	float worn = clamp(wear * (0.45 * traffic + 0.9 * patch_w - 0.2), 0.0, 1.0);
	float low = smoothstep(0.45, -0.1, v_wp.y);        // dust and grime gather low down
	float dust_k = dust * (0.35 * low + 0.65 * fbm(v_wp.xz * 1.7 + 9.0) * 0.5);

	if (kind == 0) {
		// floor boards: long, offset boards with grooves between them, grain streaks, scuffs where it is trodden
		float W = 0.155 * scale;
		float L = 1.85 * scale;
		float row = floor(p.y / W);
		float x = p.x + hash11(row + seed) * L;
		float colu = floor(x / L);
		vec2 id = vec2(colu, row);
		float fx = x - colu * L;
		float fy = p.y - row * W;
		float d = min(min(fy, W - fy), min(fx, L - fx));
		float gw = 0.0042 + pw;                                    // a groove narrower than a pixel fades instead of dotting
		float groove = (1.0 - smoothstep(0.0008, gw, d)) * (0.0042 / gw);
		float tone = hash21(id + seed);
		vec3 base = mix(col_a, col_b, tone);
		vec2 q = vec2(x * 1.3 + tone * 40.0, p.y * 46.0);
		float g = fbm(q + vec2(fbm(vec2(x * 0.9, p.y * 9.0)) * 3.0, 0.0));
		float rings = 0.5 + 0.5 * sin(p.y * 70.0 + g * 7.0 + tone * 25.0);
		float grain = mix(g, rings, 0.3);
		base *= 0.74 + 0.5 * grain;
		float scuff = smoothstep(0.7, 0.95, fbm(vec2(x * 7.0, p.y * 90.0) + id * 3.1));
		alb = mix(base, base * 1.45 + 0.03, worn * (0.4 + 0.6 * scuff));
		alb = mix(alb, col_c, dust_k * 0.55);
		alb *= 1.0 - 0.7 * groove;
		rough = clamp(0.5 + 0.22 * grain + 0.3 * worn + 0.15 * scuff + 0.35 * groove + 0.2 * dust_k, 0.3, 0.95);
		spec = 0.45;
		h = (grain * 0.00025 - groove * 0.0007 - scuff * 0.0002) * bump;
		ao = 1.0 - 0.65 * groove;
	} else if (kind == 1) {
		// plaster: soft mottling, vertical streaks, grime near the floor, fine grit
		float m = fbm(p * 0.7 * scale + seed);
		float streak = fbm(vec2(p.x * 2.2, p.y * 0.18) + seed);
		float stain = smoothstep(0.55, 0.85, fbm(p * 0.35 + 21.0));
		alb = mix(col_a, col_b, clamp(m * 0.55 + streak * 0.25 + stain * 0.5 * wear + 0.5 * dust_k, 0.0, 1.0));
		alb *= 0.94 + 0.1 * fbm(p * 38.0);
		rough = 0.86 + 0.1 * m;
		spec = 0.3;
		h = (fbm(p * 90.0) * 0.0005 + fbm(p * 6.0) * 0.0012) * bump;
	} else if (kind == 2) {
		// wool felt or carpet: matted fibres, a faint weave, paler where it is walked on
		float fib = hash21(floor(p * 700.0));
		float fib2 = fbm(p * 50.0);
		float weave = 0.5 + 0.5 * sin(p.x * 260.0) * sin(p.y * 260.0);
		float wv = 1.0 - smoothstep(0.1, 0.35, pw * 260.0);
		alb = col_a * (0.8 + 0.3 * fib2 + 0.1 * fib) * (1.0 + 0.12 * wv * (weave - 0.5));
		alb = mix(alb, col_b, worn * 0.5 + dust_k * 0.35);
		rough = 0.96;
		spec = 0.25;
		rim = 0.35;
		h = (fib2 * 0.0006 + wv * weave * 0.0004) * bump;
	} else if (kind == 3) {
		// furniture wood: grain along grain_axis, a satin finish that has been handled
		vec2 gu = grain_uv(v_p, n0) * scale;
		float tone = hash21(floor(gu * 0.05) + seed);
		float g = fbm(vec2(gu.x * 1.2, gu.y * 38.0 + tone * 30.0));
		float ring = 0.5 + 0.5 * sin(gu.y * 55.0 + fbm(vec2(gu.x * 2.0, gu.y * 8.0)) * 9.0);
		float grain = mix(g, ring, 0.45);
		alb = mix(col_a, col_b, 0.35 * tone + 0.65 * grain);
		alb = mix(alb, alb * 1.35 + 0.02, worn * 0.35);
		alb = mix(alb, col_c, dust_k * 0.4);
		rough = clamp(0.42 + 0.25 * grain + 0.25 * worn + 0.2 * dust_k, 0.3, 0.9);
		spec = 0.5;
		h = (grain * 0.0003 + fbm(gu * 90.0) * 0.00015) * bump;
	} else if (kind == 4) {
		// woven cloth: plain weave threads, an optional stripe, check or pinstripe, fibres standing proud
		vec2 u = p * scale;                                   // scale: threads per metre
		float vis = clamp(1.0 - pw * scale * 0.9, 0.0, 1.0);
		float wv = 0.5 + 0.5 * sin(u.x * 3.14159265) * sin(u.y * 3.14159265);
		float fibre = hash21(floor(u)) * 0.5 + 0.5 * fbm(p * 80.0);
		float pa = 0.0;
		float pb = 0.0;
		if (pattern == 1) {
			pa = step(0.5, fract(p.x / pat_size));
		} else if (pattern == 2) {
			pa = step(0.5, fract(p.x / pat_size));
			pb = step(0.5, fract(p.y / pat_size));
		} else if (pattern == 3) {
			pa = 1.0 - smoothstep(0.0003, 0.0003 + pw * 1.2, abs(fract(p.x / pat_size + 0.5) - 0.5) * pat_size);
		} else if (pattern == 4) {
			float k = floor(u.x / 8.0);
			pa = step(0.5, fract((u.y + k * 4.0 * (mod(k, 2.0) * 2.0 - 1.0)) / 8.0));
		}
		vec3 tint = mix(col_a, col_b, pa);
		tint = mix(tint, col_c, pb * 0.8);
		alb = tint * (0.86 + 0.26 * mix(0.5, wv, vis) + 0.12 * (fibre - 0.5));
		alb = mix(alb, col_c * 0.5 + 0.3, dust_k * 0.15);
		rough = 0.9;
		spec = 0.2;
		rim = sheen;
		h = (wv * vis * 0.0004 + fibre * 0.0002) * bump;
	} else if (kind == 5) {
		// self-healing cutting mat: printed 1 cm and 5 cm grid, a border, knife scars
		float f1 = 1.0 - clamp(pw / 0.01, 0.0, 1.0);
		vec2 g1 = abs(fract(p / 0.01 + 0.5) - 0.5) * 0.01;
		vec2 g5 = abs(fract(p / 0.05 + 0.5) - 0.5) * 0.05;
		float l1 = (1.0 - smoothstep(0.00025, 0.00025 + pw, min(g1.x, g1.y))) * f1;
		float l5 = 1.0 - smoothstep(0.0004, 0.0004 + pw, min(g5.x, g5.y));
		float diag = 1.0 - smoothstep(0.0006, 0.0006 + pw, abs(p.x - p.y) * 0.7071);
		float scar = smoothstep(0.93, 1.0, fbm(vec2(p.x * 14.0 + p.y * 3.0, p.y * 160.0))) * wear;
		float scar2 = smoothstep(0.93, 1.0, fbm(vec2(p.y * 11.0 - p.x * 2.0, p.x * 150.0) + 5.0)) * wear;
		alb = col_a * (0.92 + 0.12 * fbm(p * 30.0));
		alb = mix(alb, col_b, clamp(l1 * 0.35 + l5 * 0.7 + diag * 0.25, 0.0, 1.0));
		alb = mix(alb, col_b * 0.85, clamp(scar + scar2, 0.0, 1.0) * 0.55);
		rough = 0.58 + 0.2 * (scar + scar2) + 0.15 * dust_k;
		spec = 0.35;
		h = -(scar + scar2) * 0.0004 * bump;
	} else if (kind == 6) {
		// painted wood: satin paint, chipped to bare wood and grubby where it is knocked
		float c = fbm(p * 22.0 + seed);
		float chip = smoothstep(0.66, 0.78, c) * wear;
		float grime = fbm(p * 3.0 + 4.0) * dust_k;
		alb = mix(col_a, col_b, chip);
		alb = mix(alb, col_c, grime * 0.5);
		rough = 0.5 + 0.3 * chip + 0.2 * grime;
		spec = 0.45;
		h = (-chip * 0.0006 + fbm(p * 120.0) * 0.0002) * bump;
	} else if (kind == 7) {
		// backdrop paper: smooth, a little fibre, faint vertical ripples and a soft vertical gradient
		float fibre = fbm(vec2(p.x * 60.0, p.y * 8.0));
		float ripple = fbm(vec2(p.x * 3.0, p.y * 0.4) + seed);
		alb = col_a * (0.93 + 0.12 * ripple + 0.04 * fibre);
		alb = mix(alb, col_b, smoothstep(0.5, 0.0, v_wp.y) * 0.5 + 0.5 * dust_k * 0.4);
		rough = 0.9;
		spec = 0.25;
		h = (ripple * 0.0015 + fibre * 0.0002) * bump;
	} else if (kind == 9) {
		// oriental rug: fringe, bands, a patterned border, a lozenge medallion on a field of small diamonds, worn wool
		vec2 hs = dims * 0.5;
		float de = min(hs.x - abs(p.x), hs.y - abs(p.y));
		vec3 red = col_a;
		vec3 navy = col_b;
		vec3 cream = col_c;
		vec3 gold = vec3(0.62, 0.42, 0.12);
		float along = abs(p.x) / hs.x > abs(p.y) / hs.y ? p.y : p.x;            // the coordinate that runs along the border
		vec3 c = red;
		float field = smoothstep(0.46, 0.5, de);
		// border: cream edge, navy band, red main stripe with a repeated motif, navy band, gold line
		float mot = abs(sin(along * 13.0));
		vec3 bord = red;
		if (de < 0.05) { bord = cream; }
		else if (de < 0.11) { bord = navy; }
		else if (de < 0.15) { bord = gold; }
		else if (de < 0.36) { bord = mix(red, cream, smoothstep(0.55, 0.65, mot) * smoothstep(0.17, 0.2, de) * (1.0 - smoothstep(0.31, 0.34, de))); bord = mix(bord, navy, step(0.9, mot) * step(0.2, de) * step(de, 0.3)); }
		else if (de < 0.42) { bord = navy; }
		else if (de < 0.46) { bord = gold; }
		else { bord = cream; }
		// field: small diamonds on red, a big lozenge medallion with rings, corner spandrels
		vec2 g = abs(fract(p / 0.2) - 0.5);
		float dia = step(g.x + g.y, 0.2);
		vec3 fld = mix(red, mix(navy, red * 1.3, 0.35), dia);
		float lz = abs(p.x) / (hs.x * 0.55) + abs(p.y) / (hs.y * 0.62);
		float rings = fract(lz * 5.0);
		fld = mix(fld, navy, step(lz, 1.0));
		fld = mix(fld, mix(cream, gold, step(0.5, rings)), step(lz, 1.0) * step(0.78, rings) * step(0.45, lz));
		fld = mix(fld, red * 1.2, step(lz, 0.38));
		fld = mix(fld, cream, step(lz, 0.12));
		float corner = step(abs(p.x) / hs.x + abs(p.y) / hs.y, 0.0) + step(1.5, abs(p.x) / hs.x + abs(p.y) / hs.y);
		fld = mix(fld, navy, corner * 0.8);
		c = mix(bord, fld, field);
		// wool: knots, uneven dye (abrash), wear along the middle
		float knot = hash21(floor(p * 160.0));
		float abrash = fbm(p * 1.3 + seed);
		alb = c * (0.82 + 0.3 * knot * 0.5 + 0.2 * abrash);
		alb = mix(alb, alb * 1.25 + 0.03, wear * smoothstep(0.35, 0.0, abs(p.x)) * 0.5 * fbm(p * 5.0));
		alb = mix(alb, vec3(0.45, 0.4, 0.34), dust_k * 0.35);
		rough = 0.96;
		spec = 0.2;
		rim = 0.3;
		h = (knot * 0.0004 + fbm(p * 40.0) * 0.0004) * bump;
	} else if (kind == 10) {
		// a paper tag: card colour, a punched hole, and illegible scribbled writing in ink
		float rowh = 0.011;
		float row = floor((p.y + 0.5) / rowh);
		float ry = fract((p.y + 0.5) / rowh) - 0.5;
		float lines = step(0.2, hash11(row + seed * 7.0)) * step(abs(p.x), 0.4) * step(row, 6.0) * step(1.0, row);
		float wob = vnoise(vec2(p.x * 70.0 + row * 13.0, row)) - 0.5;
		float stroke = smoothstep(0.3, 0.0, abs(ry - wob * 0.8 - 0.1 * sin(p.x * 130.0 + row))) * lines;
		float ink = stroke * (0.55 + 0.45 * vnoise(vec2(p.x * 90.0, row * 5.0)));
		alb = mix(col_a * (0.94 + 0.08 * fbm(p * 20.0)), vec3(0.05, 0.06, 0.12), clamp(ink, 0.0, 0.95));
		float hole = 1.0 - smoothstep(0.012, 0.016, length(p - vec2(0.0, 0.5 - 0.04)));
		alb = mix(alb, col_c * 0.4, hole);
		rough = 0.85;
		spec = 0.25;
		h = fbm(p * 80.0) * 0.0002 * bump;
	} else {
		// enamel or brushed metal: streaks along grain_axis
		vec2 gu = grain_uv(v_p, n0);
		float br = fbm(vec2(gu.x * 2.0, gu.y * 400.0));
		alb = col_a * (0.9 + 0.2 * br);
		alb = mix(alb, col_b, worn * 0.5);
		rough = 0.28 + 0.3 * br + 0.25 * worn;
		metal = 0.9;
		spec = 0.5;
		h = br * 0.00008 * bump;
	}

	ALBEDO = alb;
	ROUGHNESS = clamp(rough, 0.04, 1.0);
	SPECULAR = spec;
	METALLIC = metal;
	AO = ao;
	AO_LIGHT_AFFECT = 0.5;
	RIM = rim;
	RIM_TINT = 0.5;
	NORMAL = bump_normal(normalize(NORMAL), VERTEX, h, 1.0);
}
