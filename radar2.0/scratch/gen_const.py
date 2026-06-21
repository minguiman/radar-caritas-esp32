import json
import urllib.request

# 1. Download Constellation Lines (d3-celestial)
url_lines = 'https://raw.githubusercontent.com/ofrohn/d3-celestial/master/data/constellations.lines.json'
req = urllib.request.Request(url_lines, headers={'User-Agent': 'Mozilla/5.0'})
with urllib.request.urlopen(req) as response:
    lines_data = json.loads(response.read().decode())

# Hardcode the brightest 50+ stars in the sky
named_stars = [
    (101.28, -16.71, -14, "SIRIO"),
    (95.98, -52.69, -7, "CANOPUS"),
    (213.91, 19.18, -0, "ARTURO"),
    (219.90, -60.83, -0, "ALFA CENTAURI"),
    (279.23, 38.78, 0, "VEGA"),
    (79.17, 45.99, 0, "CAPELLA"),
    (78.63, -8.20, 1, "RIGEL"),
    (114.82, 5.22, 4, "PROCYON"),
    (24.42, -57.23, 4, "ACHERNAR"),
    (88.79, 7.41, 5, "BETELGEUSE"),
    (210.95, -60.37, 6, "HACADAR"), # Hadar
    (297.69, 8.86, 7, "ALTAIR"),
    (201.29, -11.16, 9, "ESPICA"), # Spica
    (247.35, -26.43, 10, "ANTARES"),
    (116.32, 28.02, 11, "POLUX"), # Pollux
    (344.41, -29.62, 11, "FOMALHAUT"),
    (310.35, 45.28, 12, "DENEB"),
    (206.88, -59.54, 12, "MIMOSA"),
    (152.09, 11.96, 13, "REGULO"), # Regulus
    (102.45, -28.97, 15, "ADHARA"),
    (112.28, 31.88, 15, "CASTOR"),
    (258.11, -37.10, 16, "SHAULA"),
    (101.07, -26.39, 17, "WEZEN"),
    (186.65, -59.69, 17, "GACRUX"),
    (81.28, 6.34, 16, "BELLATRIX"),
    (83.00, -0.29, 16, "ALNILAM"),
    (145.42, -8.65, 19, "ALFARD"),
    (86.93, -9.66, 20, "SAIPH"),
    (317.06, 57.17, 22, "ALDERAMIN"),
    (165.46, 61.75, 17, "DUBHE"),
    (183.86, 57.03, 18, "ALIOTH"),
    (206.89, 49.31, 18, "ALKAID"),
    (165.93, 56.38, 23, "MERAK"),
    (200.98, 54.92, 22, "MIZAR"),
    (193.51, 55.95, 24, "ALCOR"),
    (37.95, 89.26, 19, "POLAR"),
    (46.03, 40.95, 17, "MIRFAK"),
    (84.05, -1.20, 17, "ALNITAK"),
    (263.40, -37.10, 18, "SARGAS"),
    (14.17, 60.71, 22, "CASIOPEA A"),
    (10.12, 56.53, 22, "CAPH"),
    (68.98, 16.50, 8, "ALDEBARAN"),
]

unique_stars = set()
for r, d, m, n in named_stars:
    unique_stars.add((round(r*100), round(d*100)))

segments = []
labels = []

# Top constellations to include
const_names = {
    "And": "ANDROMEDA", "Aql": "AGUILA", "Aqr": "ACUARIO", "Ari": "ARIES",
    "Aur": "AURIGA", "Boo": "BOOTES", "Cnc": "CANCER", "CMa": "CAN MAYOR",
    "CMi": "CAN MENOR", "Cap": "CAPRICORNIO", "Cas": "CASIOPEA", "Cep": "CEFEO",
    "Cet": "BALLENA", "CrB": "CORONA B", "Crv": "CUERVO", "Cyg": "CISNE",
    "Del": "DELFIN", "Dra": "DRAGON", "Eri": "ERIDANO", "Gem": "GEMINIS",
    "Her": "HERCULES", "Hya": "HIDRA", "Leo": "LEO", "Lep": "LIEBRE",
    "Lib": "LIBRA", "Lyr": "LIRA", "Oph": "OFIUCO", "Ori": "ORION",
    "Peg": "PEGASO", "Per": "PERSEO", "Psc": "PISCIS", "PsA": "PEZ AUSTR",
    "Pup": "POPA", "Sgr": "SAGITARIO", "Sco": "ESCORPIO", "Sct": "ESCUDO",
    "Ser": "SERPIENTE", "Tau": "TAURO", "UMa": "OSA MAYOR", "UMi": "OSA MENOR",
    "Vir": "VIRGO", "Vul": "ZORRA", "Car": "QUILLA", "Cru": "CRUZ SUR",
    "Cen": "CENTAURO", "Pav": "PAVO", "TrA": "TRIANGULO A"
}

group_id = 0
for feature in lines_data['features']:
    cid = feature['id']
    if cid not in const_names:
        continue 
    
    geom = feature['geometry']
    coords = geom['coordinates']
    
    ra_sum = 0
    dec_sum = 0
    pt_count = 0
    
    for line in coords:
        for i in range(len(line)-1):
            ra1, dec1 = line[i]
            ra2, dec2 = line[i+1]
            
            ra1 = (ra1 + 360) % 360
            ra2 = (ra2 + 360) % 360
            
            r1c = round(ra1 * 100)
            d1c = round(dec1 * 100)
            r2c = round(ra2 * 100)
            d2c = round(dec2 * 100)
            
            unique_stars.add((r1c, d1c))
            unique_stars.add((r2c, d2c))
            
            segments.append((r1c, d1c, r2c, d2c, group_id))
            
            ra_sum += ra1
            dec_sum += dec1
            pt_count += 1
            ra_sum += ra2
            dec_sum += dec2
            pt_count += 1
            
    if pt_count > 0:
        labels.append((round((ra_sum/pt_count)*100), round((dec_sum/pt_count)*100), const_names[cid]))
    
    group_id += 1

labels.append((27000, -2500, "VIA LACTEA"))
labels.append((4500, 5500, "VIA LACTEA"))

out = ""

# kSkyStars
out += f"constexpr SkyStar kSkyStars[{len(unique_stars)}] = {{\n"
stars_output = 0
for r, d, m, n in named_stars:
    out += f'    {{{r:.2f}f, {d:.2f}f, {m}, "{n}"}},\n'
    stars_output += 1

for r, d in unique_stars:
    is_named = False
    for nr, nd, nm, nn in named_stars:
        if abs(nr*100 - r) < 150 and abs(nd*100 - d) < 150:
            is_named = True
            break
    if not is_named:
        out += f'    {{{r/100.0:.2f}f, {d/100.0:.2f}f, 25, nullptr}},\n'
        stars_output += 1

out = out.rstrip(",\n") + "\n};\n\n"
print(f"Total stars: {stars_output}")

# kSkyConstellationLabels
out += f"constexpr SkyLabel kSkyConstellationLabels[{len(labels)}] = {{\n"
for r, d, n in labels:
    out += f'    {{{r}, {d}, "{n}"}},\n'
out = out.rstrip(",\n") + "\n};\n\n"

# kSkySegments
out += f"constexpr SkySegment kSkySegments[{len(segments)}] = {{\n"
for r1, d1, r2, d2, g in segments:
    out += f'    {{{r1}, {d1}, {r2}, {d2}, {g % 2}}},\n'
out = out.rstrip(",\n") + "\n};\n\n"

with open("scratch/const_out.txt", "w") as f:
    f.write(out)
print("Done!")
