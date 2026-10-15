// The studio as seen from the mannequin, for ambient light and reflections only (the background is the cyc geometry,
// studio.gd). Dark walls and ceiling, the lit floor below, and each lamp's diffuser as a bright patch in its real
// direction and size, so glossy fabric and the eyes reflect the softboxes where they are.
shader_type sky;

uniform vec3 wall_col = vec3(0.035, 0.034, 0.033);
uniform vec3 top_col = vec3(0.012, 0.012, 0.013);
uniform vec3 floor_col = vec3(0.10, 0.095, 0.09);
uniform int lamp_n = 0;
uniform vec3 lamp_dir[8];     // unit vector from the subject to the lamp
uniform vec3 lamp_up[8];      // the lamp's up direction, across lamp_dir
uniform vec3 lamp_half[8];    // half width and half height over the distance (tangent plane), shape (0 rect, 1 octagon)
uniform vec3 lamp_col[8];     // diffuser radiance

void sky() {
	vec3 d = EYEDIR;
	vec3 c = mix(wall_col, top_col, smoothstep(0.15, 0.85, d.y));
	c = mix(c, floor_col, smoothstep(-0.02, -0.3, d.y));
	for (int i = 0; i < 8; i++) {
		if (i >= lamp_n) {
			break;
		}
		vec3 f = lamp_dir[i];
		float cd = dot(d, f);
		if (cd <= 0.0) {
			continue;
		}
		vec3 q = d / cd - f;
		vec3 up = lamp_up[i];
		vec3 rt = normalize(cross(f, up));
		vec2 lp = vec2(dot(q, rt), dot(q, up)) / lamp_half[i].xy;
		float shape;
		if (lamp_half[i].z > 0.5) {
			// regular octagon of circumradius 1
			vec2 a = abs(lp);
			shape = max(max(a.x, a.y), (a.x + a.y) * 0.70710678) / 0.9238795;
		} else {
			shape = max(abs(lp.x), abs(lp.y));
		}
		float m = 1.0 - smoothstep(0.92, 1.0, shape);
		// a diffuser is brightest in the middle and falls off a little towards its rim
		c += lamp_col[i] * m * (0.8 + 0.2 * (1.0 - shape * shape));
	}
	COLOR = c;
}
