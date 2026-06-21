import math

# North Galactic Pole (NGP)
ra_G = math.radians(192.85948)
dec_G = math.radians(27.12825)
l_omega = math.radians(33.0)  # ascending node is 33 degrees

points = []
for l_deg in range(0, 360, 5):
    l = math.radians(l_deg)
    
    # Formula for b = 0
    sin_dec = math.cos(dec_G) * math.sin(l - l_omega)
    dec = math.asin(sin_dec)
    
    y = math.cos(l - l_omega)
    x = -math.sin(dec_G) * math.sin(l - l_omega)
    
    ra_diff = math.atan2(y, x)
    ra = ra_G + ra_diff
    
    # Normalize RA to [0, 2pi]
    ra = ra % (2 * math.pi)
    
    ra_deg = math.degrees(ra)
    dec_deg = math.degrees(dec)
    points.append((ra_deg, dec_deg))

print("constexpr float kMilkyWayPoints[][2] = {")
for ra_d, dec_d in points:
    print(f"    {{{ra_d:.2f}f, {dec_d:.2f}f}},")
print("};")
