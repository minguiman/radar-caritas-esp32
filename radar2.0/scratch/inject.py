import re

def inject_arrays():
    with open('scratch/const_out.txt', 'r') as f:
        const_data = f.read()
    
    with open('src/RadarRenderer.cpp', 'r') as f:
        cpp_code = f.read()
    
    # Replace kSkyStars
    stars_match = re.search(r'constexpr SkyStar kSkyStars\[.*?\]?\s*=\s*\{.*?\};\n', cpp_code, re.DOTALL)
    new_stars = re.search(r'(constexpr SkyStar kSkyStars\[.*?\]?\s*=\s*\{.*?\};\n)', const_data, re.DOTALL).group(1)
    cpp_code = cpp_code[:stars_match.start()] + new_stars + cpp_code[stars_match.end():]
    
    # Replace kSkySegments
    seg_match = re.search(r'constexpr SkySegment kSkySegments\[.*?\]?\s*=\s*\{.*?\};\n', cpp_code, re.DOTALL)
    new_seg = re.search(r'(constexpr SkySegment kSkySegments\[.*?\]?\s*=\s*\{.*?\};\n)', const_data, re.DOTALL).group(1)
    cpp_code = cpp_code[:seg_match.start()] + new_seg + cpp_code[seg_match.end():]
    
    # Replace kSkyConstellationLabels
    lbl_match = re.search(r'constexpr SkyLabel kSkyConstellationLabels\[.*?\]?\s*=\s*\{.*?\};\n', cpp_code, re.DOTALL)
    new_lbl = re.search(r'(constexpr SkyLabel kSkyConstellationLabels\[.*?\]?\s*=\s*\{.*?\};\n)', const_data, re.DOTALL).group(1)
    cpp_code = cpp_code[:lbl_match.start()] + new_lbl + cpp_code[lbl_match.end():]
    
    with open('src/RadarRenderer.cpp', 'w') as f:
        f.write(cpp_code)

inject_arrays()
