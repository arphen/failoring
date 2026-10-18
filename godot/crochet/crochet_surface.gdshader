// Procedural crochet stitches on the simulated surface (docs/CROCHET_PLAN.md section 8.3). The mesh is rendered as two shells
// (the right side outside, the wrong side inside, each offset by half the fabric thickness along the normal), so the bag has
// thickness. Inputs per vertex: UV = stitch coordinates (u along the row, v up the rows, one unit per stitch), COLOR.r = stitch type
// (0.2 ch, 0.4 sl st, 0.6 sc, 0.7 hdc, 0.8 dc, 1.0 tr), COLOR.g = insertion (0 both, 0.33 blo, 0.66 flo, 1 space), COLOR.b = 1 on a RS row.
// Per stitch cell: on the RS a "V" of two tilted ellipses on top of a post, fine diagonal yarn twist; on the WS horizontal bars (the
// back of the V) and the post; a BLO row adds a raised horizontal ridge on the RS (the free front loop), a FLO row one on the WS.
shader_type spatial;
render_mode cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec4 albedo : source_color = vec4(0.71, 0.32, 0.23, 1.0);
uniform float side = 0.0;          // 0 = the right side shell, 1 = the wrong side shell
uniform float rough = 0.85;        // cotton matte 0.9, acrylic 0.6, wool 1.0
uniform float sheen = 0.2;         // rim light: acrylic a little, wool a lot (fuzz)
uniform float relief = 1.0;        // 0 turns the relief off
uniform float twist = 1.0;

const float PI2 = 6.2831853;

float ellipse(vec2 p, vec2 c, vec2 r, float ang) {
	vec2 q = p - c;
	float ca = cos(ang), sa = sin(ang);
	q = vec2(ca * q.x + sa * q.y, -sa * q.x + ca * q.y);
	float d = length(q / r);
	return smoothstep(1.0, 0.35, d);
}

// height of one stitch cell (0..1 in both coordinates), v = row direction
float stitch_height(vec2 c, float type, float ins, float rs_row) {
	float h = 0.0;
	float tall = clamp((type - 0.6) * 2.5, 0.0, 1.0);          // hdc..tr: longer post
	float post_top = 0.42 + 0.25 * tall;
	if (type < 0.3) {                                            // chain: a small oval
		h = ellipse(c, vec2(0.5, 0.5), vec2(0.3, 0.2), 0.0);
	} else if (type < 0.5) {                                     // slip stitch: a flat bar
		h = 0.7 * ellipse(c, vec2(0.5, 0.5), vec2(0.42, 0.14), 0.0);
	} else if (side < 0.5) {                                     // RS: the V on top of the post
		float v1 = ellipse(c, vec2(0.3, 0.7), vec2(0.30, 0.15), 0.9);
		float v2 = ellipse(c, vec2(0.7, 0.7), vec2(0.30, 0.15), -0.9);
		float post = smoothstep(0.1, 0.0, abs(c.x - 0.5) - 0.11) * smoothstep(0.0, 0.12, c.y) * (1.0 - smoothstep(post_top - 0.12, post_top, c.y));
		h = max(max(v1, v2), 0.7 * post);
	} else {                                                     // WS: horizontal bars (the back of the stitch)
		float bar = smoothstep(0.9, 0.35, abs(c.x - 0.5) / 0.46) * smoothstep(0.3, 0.0, abs(c.y - 0.55) - 0.1);
		float post = smoothstep(0.1, 0.0, abs(c.x - 0.5) - 0.11) * smoothstep(0.0, 0.12, c.y) * (1.0 - smoothstep(0.3, 0.4, c.y));
		h = max(0.8 * bar, 0.5 * post);
	}
	return h;
}

void fragment() {
	vec2 uv = UV;
	vec2 cell = fract(uv);
	float type = COLOR.r;
	float ins = COLOR.g;
	float rs_row = COLOR.b;
	float h = stitch_height(cell, type, ins, rs_row);
	// yarn twist: fine diagonal stripes along the stitch
	float tw = 0.5 + 0.5 * sin(PI2 * (cell.x * 3.0 + cell.y * 5.0) + 6.0 * h);
	h += 0.12 * twist * tw * smoothstep(0.1, 0.5, h);
	// BLO leaves the front loop of the target row standing: a ridge along the row on the RS; FLO on the WS
	float ridge_here = (ins > 0.2 && ins < 0.5) ? 1.0 - side : ((ins > 0.5 && ins < 0.8) ? side : 0.0);
	h += ridge_here * 0.45 * smoothstep(0.12, 0.0, abs(cell.y - 0.06));
	// bump from the screen-space derivatives of the height (no tangents needed)
	vec3 dpdx = dFdx(VERTEX);
	vec3 dpdy = dFdy(VERTEX);
	float dhdx = dFdx(h);
	float dhdy = dFdy(h);
	vec3 r1 = cross(dpdy, NORMAL);
	vec3 r2 = cross(NORMAL, dpdx);
	float det = dot(dpdx, r1);
	vec3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
	float px = max(length(fwidth(uv)), 1e-6);
	float fade = clamp(1.0 - (px - 0.2) * 3.0, 0.0, 1.0) * relief;  // fade the relief out when a stitch is under a few pixels
	NORMAL = normalize(abs(det) * NORMAL - 0.018 * fade * grad);
	// colour: darker in the gaps between the stitches, the WS a little darker
	float shade = mix(0.62, 1.0, clamp(h * 1.4, 0.0, 1.0));
	vec3 col = albedo.rgb * shade * mix(1.0, 0.82, side);
	ALBEDO = col;
	ROUGHNESS = rough;
	SPECULAR = 0.25;
	RIM = sheen;
	RIM_TINT = 0.6;
}
