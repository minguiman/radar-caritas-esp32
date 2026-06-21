import textwrap

def gen_planet(name, N_0, N_1, i_0, i_1, w_0, w_1, a_0, a_1, e_0, e_1, M_0, M_1, color, label, size):
    return f"""
    {{ // {name}
        const float N = normalizeDegrees({N_0}f + {N_1}f * daysSinceJ2000);
        const float i = {i_0}f + {i_1}f * daysSinceJ2000;
        const float w = normalizeDegrees({w_0}f + {w_1}f * daysSinceJ2000);
        const float a = {a_0}f + {a_1}f * daysSinceJ2000;
        const float e = {e_0}f + {e_1}f * daysSinceJ2000;
        const float M = normalizeDegrees({M_0}f + {M_1}f * daysSinceJ2000);
        
        const float E_approx = M + radToDeg(e * sinf(degToRad(M)) * (1.0f + e * cosf(degToRad(M))));
        const float xv = a * (cosf(degToRad(E_approx)) - e);
        const float yv = a * (sqrtf(1.0f - e*e) * sinf(degToRad(E_approx)));
        
        const float v = radToDeg(atan2f(yv, xv));
        const float r = sqrtf(xv*xv + yv*yv);
        
        const float xh = r * (cosf(degToRad(N)) * cosf(degToRad(v+w)) - sinf(degToRad(N)) * sinf(degToRad(v+w)) * cosf(degToRad(i)));
        const float yh = r * (sinf(degToRad(N)) * cosf(degToRad(v+w)) + cosf(degToRad(N)) * sinf(degToRad(v+w)) * cosf(degToRad(i)));
        const float zh = r * (sinf(degToRad(v+w)) * sinf(degToRad(i)));
        
        // Geocentric coordinates
        const float xg = xh + sunX;
        const float yg = yh + sunY;
        const float zg = zh + sunZ;
        
        // Equatorial coordinates
        const float xe = xg;
        const float ye = yg * cosf(obliquityRad) - zg * sinf(obliquityRad);
        const float ze = yg * sinf(obliquityRad) + zg * cosf(obliquityRad);
        
        const float ra = normalizeDegrees(radToDeg(atan2f(ye, xe)));
        const float dec = radToDeg(atan2f(ze, sqrtf(xe*xe + ye*ye)));
        
        ProjectedStar p{{}};
        projectRaDec(ra, dec, p);
        if (p.visible) {{
            fillCircle(p.x, p.y, {size}, {color[0]}, {color[1]}, {color[2]}, 1.0f);
            drawText(std::clamp(p.x + {size+3}, 4, static_cast<int>(m_width) - measureText("{label}") - 4),
                     std::clamp(p.y - 4, 34, static_cast<int>(m_height) - 18),
                     "{label}", {color[0]}, {color[1]}, {color[2]}, 0.90f);
        }} else {{
            drawEdgeIndicator(p, "{label}", {color[0]}, {color[1]}, {color[2]});
        }}
    }}
"""

out = ""
# Mercury
out += gen_planet("Mercury", 48.3313, 0.0000324587, 7.0047, 0.0000000500, 29.1241, 0.0000101444, 0.387098, 0, 0.205635, 0.0000000006, 168.6562, 4.0923344368, (180, 180, 180), "MERCURIO", 4)
# Venus
out += gen_planet("Venus", 76.6799, 0.0000246590, 3.3946, 0.0000000275, 54.8910, 0.0000138374, 0.723330, 0, 0.006773, -0.0000000013, 48.0052, 1.6021302244, (255, 230, 180), "VENUS", 6)
# Mars
out += gen_planet("Mars", 49.5574, 0.0000211081, 1.8497, -0.0000000178, 286.5016, 0.0000292961, 1.523688, 0, 0.093405, 0.0000000025, 18.6021, 0.5240207766, (255, 100, 80), "MARTE", 5)
# Jupiter
out += gen_planet("Jupiter", 100.4542, 0.0000276854, 1.3030, -0.0000000155, 273.8777, 0.0000164505, 5.20256, 0, 0.048498, 0.0000000044, 19.8950, 0.0830853001, (240, 200, 150), "JUPITER", 7)
# Saturn
out += gen_planet("Saturn", 113.6634, 0.0000238980, 2.4886, -0.0000000108, 339.3939, 0.0000297661, 9.55475, 0, 0.055546, -0.0000000094, 316.9670, 0.0334442282, (250, 240, 200), "SATURNO", 6)

print(out)
